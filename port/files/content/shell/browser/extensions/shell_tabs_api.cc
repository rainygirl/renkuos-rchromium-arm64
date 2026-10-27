// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_tabs_api.h"

#include <string>
#include <utility>
#include <vector>

#include "base/strings/pattern.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "content/shell/browser/shell.h"
#include "content/shell/common/extensions/api/tabs.h"
#include "content/shell/common/extensions/api/web_navigation.h"
#include "content/shell/common/extensions/api/windows.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/common/extension.h"
#include "extensions/common/url_pattern.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace extensions {

namespace tabs = api::tabs;
namespace web_navigation = api::web_navigation;

namespace {

constexpr char kNoTabError[] = "No tab with id: *.";
constexpr char kNoCurrentTabError[] = "No current tab.";

std::string NoTabError(int tab_id) {
  std::string error = kNoTabError;
  error.replace(error.find('*'), 1, base::NumberToString(tab_id));
  return error;
}

// A tab ID argument, or the active tab when it is absent.
content::WebContents* ResolveTab(content::BrowserContext* context,
                                 const std::optional<int>& tab_id) {
  return tab_id ? ShellTabs::GetTabById(context, *tab_id)
                : ShellTabs::GetActiveTab(context);
}

// Relative URLs are resources of the calling extension, as in Chrome.
GURL ResolveURL(const std::string& url_string, const Extension* extension) {
  GURL url(url_string);
  if (!url.is_valid() && extension) {
    url = extension->GetResourceURL(url_string);
  }
  return url;
}

bool MatchesQuery(content::WebContents* tab,
                  const tabs::Query::Params::QueryInfo& query,
                  const tabs::Tab& tab_object,
                  int active_window_id,
                  const std::vector<URLPattern>& url_patterns) {
  const bool active = tab_object.active;
  if (query.active && *query.active != active) {
    return false;
  }
  if (query.highlighted && *query.highlighted != active) {
    return false;
  }
  if ((query.current_window && *query.current_window) ||
      (query.last_focused_window && *query.last_focused_window)) {
    if (tab_object.window_id != active_window_id) {
      return false;
    }
  }
  if (query.window_id) {
    const int window_id = *query.window_id == api::windows::WINDOW_ID_CURRENT
                              ? active_window_id
                              : *query.window_id;
    if (tab_object.window_id != window_id) {
      return false;
    }
  }
  if (query.index && *query.index != 0) {
    return false;
  }
  if (query.pinned && *query.pinned) {
    return false;
  }
  if (query.discarded && *query.discarded) {
    return false;
  }
  if (query.frozen && *query.frozen) {
    return false;
  }
  if (query.audible && *query.audible != tab->IsCurrentlyAudible()) {
    return false;
  }
  if (query.muted && *query.muted != tab->IsAudioMuted()) {
    return false;
  }
  if (query.group_id && *query.group_id != -1) {
    return false;
  }
  if (query.status != tabs::TabStatus::kNone &&
      query.status != tab_object.status) {
    return false;
  }
  // URL and title only match where the extension may see them.
  if (query.title &&
      (!tab_object.title || !base::MatchPattern(*tab_object.title,
                                                *query.title))) {
    return false;
  }
  if (!url_patterns.empty()) {
    if (!tab_object.url) {
      return false;
    }
    const GURL url(*tab_object.url);
    bool matched = false;
    for (const URLPattern& pattern : url_patterns) {
      matched = matched || pattern.MatchesURL(url);
    }
    if (!matched) {
      return false;
    }
  }
  return true;
}

}  // namespace

ExtensionFunction::ResponseAction TabsQueryFunction::Run() {
  std::optional<tabs::Query::Params> params =
      tabs::Query::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  const tabs::Query::Params::QueryInfo& query = params->query_info;

  std::vector<URLPattern> url_patterns;
  if (query.url) {
    std::vector<std::string> strings;
    if (query.url->as_string) {
      strings.push_back(*query.url->as_string);
    } else if (query.url->as_strings) {
      strings = *query.url->as_strings;
    }
    for (const std::string& string : strings) {
      URLPattern pattern(URLPattern::SCHEME_ALL);
      if (pattern.Parse(string) != URLPattern::ParseResult::kSuccess) {
        return RespondNow(Error("Invalid url pattern '" + string + "'"));
      }
      url_patterns.push_back(std::move(pattern));
    }
  }

  content::WebContents* active =
      ShellTabs::GetActiveTab(browser_context());
  const int active_window_id = active ? ShellTabs::GetWindowId(active) : -1;
  std::vector<tabs::Tab> result;
  for (content::WebContents* tab : ShellTabs::GetTabs(browser_context())) {
    tabs::Tab tab_object = ShellTabs::CreateTabObject(tab, extension());
    if (MatchesQuery(tab, query, tab_object, active_window_id, url_patterns)) {
      result.push_back(std::move(tab_object));
    }
  }
  return RespondNow(ArgumentList(tabs::Query::Results::Create(result)));
}

ExtensionFunction::ResponseAction TabsGetFunction::Run() {
  std::optional<tabs::Get::Params> params = tabs::Get::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  content::WebContents* tab =
      ShellTabs::GetTabById(browser_context(), params->tab_id);
  if (!tab) {
    return RespondNow(Error(NoTabError(params->tab_id)));
  }
  return RespondNow(ArgumentList(
      tabs::Get::Results::Create(ShellTabs::CreateTabObject(tab, extension()))));
}

ExtensionFunction::ResponseAction TabsGetCurrentFunction::Run() {
  // The calling page's own tab; nothing for a service worker or popup.
  content::WebContents* sender = GetSenderWebContents();
  if (!sender || ShellTabs::GetTabId(sender) < 0) {
    return RespondNow(NoArguments());
  }
  return RespondNow(ArgumentList(tabs::GetCurrent::Results::Create(
      ShellTabs::CreateTabObject(sender, extension()))));
}

ExtensionFunction::ResponseAction TabsCreateFunction::Run() {
  std::optional<tabs::Create::Params> params =
      tabs::Create::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  const auto& properties = params->create_properties;

  GURL url = properties.url ? ResolveURL(*properties.url, extension())
                            : GURL("about:blank");
  if (!url.is_valid()) {
    return RespondNow(Error("Invalid url: \"" + *properties.url + "\"."));
  }
  // Every tab is its own window.
  content::Shell* shell = content::Shell::CreateNewWindow(
      browser_context(), url, nullptr, gfx::Size());
  return RespondNow(ArgumentList(tabs::Create::Results::Create(
      ShellTabs::CreateTabObject(shell->web_contents(), extension()))));
}

ExtensionFunction::ResponseAction TabsUpdateFunction::Run() {
  std::optional<tabs::Update::Params> params =
      tabs::Update::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  content::WebContents* tab = ResolveTab(browser_context(), params->tab_id);
  if (!tab) {
    return RespondNow(params->tab_id ? Error(NoTabError(*params->tab_id))
                                     : Error(kNoCurrentTabError));
  }
  const auto& properties = params->update_properties;
  if (properties.url) {
    GURL url = ResolveURL(*properties.url, extension());
    if (!url.is_valid()) {
      return RespondNow(Error("Invalid url: \"" + *properties.url + "\"."));
    }
    content::NavigationController::LoadURLParams load_params(url);
    load_params.transition_type = ui::PAGE_TRANSITION_FROM_API;
    tab->GetController().LoadURLWithParams(load_params);
  }
  if (properties.muted) {
    tab->SetAudioMuted(*properties.muted);
  }
  if (properties.active && *properties.active) {
    tab->Focus();
  }
  return RespondNow(ArgumentList(tabs::Update::Results::Create(
      ShellTabs::CreateTabObject(tab, extension()))));
}

ExtensionFunction::ResponseAction TabsReloadFunction::Run() {
  std::optional<tabs::Reload::Params> params =
      tabs::Reload::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  content::WebContents* tab = ResolveTab(browser_context(), params->tab_id);
  if (!tab) {
    return RespondNow(params->tab_id ? Error(NoTabError(*params->tab_id))
                                     : Error(kNoCurrentTabError));
  }
  const bool bypass_cache = params->reload_properties &&
                            params->reload_properties->bypass_cache &&
                            *params->reload_properties->bypass_cache;
  tab->GetController().Reload(bypass_cache
                                  ? content::ReloadType::BYPASSING_CACHE
                                  : content::ReloadType::NORMAL,
                              /*check_for_repost=*/true);
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction TabsRemoveFunction::Run() {
  std::optional<tabs::Remove::Params> params =
      tabs::Remove::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  std::vector<int> tab_ids;
  if (params->tab_ids.as_integer) {
    tab_ids.push_back(*params->tab_ids.as_integer);
  } else if (params->tab_ids.as_integers) {
    tab_ids = *params->tab_ids.as_integers;
  }
  for (int tab_id : tab_ids) {
    content::WebContents* tab =
        ShellTabs::GetTabById(browser_context(), tab_id);
    if (!tab) {
      return RespondNow(Error(NoTabError(tab_id)));
    }
    if (content::Shell* shell = content::Shell::FromWebContents(tab)) {
      shell->Close();
    }
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction WebNavigationGetFrameFunction::Run() {
  std::optional<web_navigation::GetFrame::Params> params =
      web_navigation::GetFrame::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  const auto& details = params->details;

  content::RenderFrameHost* frame = nullptr;
  if (details.document_id) {
    ExtensionApiFrameIdMap::DocumentId document_id =
        ExtensionApiFrameIdMap::DocumentIdFromString(*details.document_id);
    if (!document_id) {
      return RespondNow(Error("Invalid documentId."));
    }
    frame = ExtensionApiFrameIdMap::Get()->GetRenderFrameHostByDocumentId(
        document_id);
  } else if (details.tab_id && details.frame_id) {
    content::WebContents* tab =
        ShellTabs::GetTabById(browser_context(), *details.tab_id);
    if (tab) {
      frame = ExtensionApiFrameIdMap::GetRenderFrameHostById(
          tab, *details.frame_id);
    }
  } else {
    return RespondNow(Error("Either documentId or both tabId and frameId must "
                            "be specified."));
  }
  if (!frame ||
      (details.tab_id &&
       ShellTabs::GetTabId(content::WebContents::FromRenderFrameHost(frame)) !=
           *details.tab_id)) {
    return RespondNow(WithArguments(base::Value()));
  }

  web_navigation::GetFrame::Results::Details result;
  result.error_occurred = frame->IsErrorDocument();
  result.url = frame->GetLastCommittedURL().spec();
  result.parent_frame_id = ExtensionApiFrameIdMap::GetParentFrameId(frame);
  result.document_id = ExtensionApiFrameIdMap::GetDocumentId(frame).ToString();
  if (content::RenderFrameHost* parent = frame->GetParentOrOuterDocument()) {
    result.parent_document_id =
        ExtensionApiFrameIdMap::GetDocumentId(parent).ToString();
  }
  result.frame_type = ExtensionApiFrameIdMap::GetFrameType(frame);
  result.document_lifecycle =
      ExtensionApiFrameIdMap::GetDocumentLifecycle(frame);
  return RespondNow(
      ArgumentList(web_navigation::GetFrame::Results::Create(result)));
}

ExtensionFunction::ResponseAction WebNavigationGetAllFramesFunction::Run() {
  std::optional<web_navigation::GetAllFrames::Params> params =
      web_navigation::GetAllFrames::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);
  content::WebContents* tab =
      ShellTabs::GetTabById(browser_context(), params->details.tab_id);
  if (!tab) {
    return RespondNow(WithArguments(base::Value()));
  }
  std::vector<web_navigation::GetAllFrames::Results::DetailsType> result;
  tab->GetPrimaryMainFrame()->ForEachRenderFrameHost(
      [&result](content::RenderFrameHost* frame) {
        if (!frame->IsRenderFrameLive()) {
          return;
        }
        web_navigation::GetAllFrames::Results::DetailsType details;
        details.error_occurred = frame->IsErrorDocument();
        details.process_id = frame->GetProcess()->GetDeprecatedID();
        details.frame_id = ExtensionApiFrameIdMap::GetFrameId(frame);
        details.parent_frame_id =
            ExtensionApiFrameIdMap::GetParentFrameId(frame);
        details.url = frame->GetLastCommittedURL().spec();
        details.document_id =
            ExtensionApiFrameIdMap::GetDocumentId(frame).ToString();
        if (content::RenderFrameHost* parent =
                frame->GetParentOrOuterDocument()) {
          details.parent_document_id =
              ExtensionApiFrameIdMap::GetDocumentId(parent).ToString();
        }
        details.frame_type = ExtensionApiFrameIdMap::GetFrameType(frame);
        details.document_lifecycle =
            ExtensionApiFrameIdMap::GetDocumentLifecycle(frame);
        result.push_back(std::move(details));
      });
  return RespondNow(
      ArgumentList(web_navigation::GetAllFrames::Results::Create(result)));
}

ExtensionFunction::ResponseAction
ExtensionIsAllowedIncognitoAccessFunction::Run() {
  // Extensions do not run in the off-the-record context.
  return RespondNow(WithArguments(false));
}

ExtensionFunction::ResponseAction
ExtensionIsAllowedFileSchemeAccessFunction::Run() {
  return RespondNow(WithArguments(
      ExtensionPrefs::Get(browser_context())->AllowFileAccess(extension_id())));
}

}  // namespace extensions
