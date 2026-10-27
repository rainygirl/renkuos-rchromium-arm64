// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_tabs.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/shell/common/extensions/api/web_navigation.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/script_executor.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/api_permission_id.mojom-shared.h"
#include "extensions/common/mojom/event_dispatcher.mojom.h"
#include "extensions/common/permissions/permissions_data.h"
#include "net/base/net_errors.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/rect.h"

namespace extensions {

namespace tabs = api::tabs;
namespace web_navigation = api::web_navigation;

namespace {

// Every tab, oldest first, and the most recently focused one.
std::vector<raw_ptr<content::WebContents>>& Tabs() {
  static base::NoDestructor<std::vector<raw_ptr<content::WebContents>>> tabs;
  return *tabs;
}
raw_ptr<content::WebContents> g_active_tab = nullptr;

ShellTabs::TabChangedCallback& TabChanged() {
  static base::NoDestructor<ShellTabs::TabChangedCallback> callback;
  return *callback;
}

void NotifyTabChanged(content::WebContents* tab, bool navigated) {
  if (TabChanged()) {
    TabChanged().Run(tab, navigated);
  }
}

double Now() {
  return base::Time::Now().InMillisecondsFSinceUnixEpoch();
}

bool MaySeeTabDetails(const Extension* extension,
                      int tab_id,
                      const GURL& url) {
  if (!extension) {
    return true;
  }
  const PermissionsData* permissions = extension->permissions_data();
  return permissions->HasAPIPermissionForTab(tab_id,
                                             mojom::APIPermissionID::kTab) ||
         permissions->HasHostPermission(url);
}

// An event for every extension with a listener. A tab argument at
// `tab_index` is rebuilt for each extension, scrubbed to what it may see.
void DispatchEvent(content::WebContents* web_contents,
                   events::HistogramValue histogram_value,
                   const std::string& event_name,
                   base::ListValue args,
                   std::optional<size_t> tab_index = std::nullopt,
                   const GURL* filter_url = nullptr) {
  content::BrowserContext* context = web_contents->GetBrowserContext();
  EventRouter* router = EventRouter::Get(context);
  if (!router) {
    return;
  }
  auto event = std::make_unique<Event>(histogram_value, event_name,
                                       std::move(args), context);
  if (filter_url) {
    event->filter_info = mojom::EventFilteringInfo::New();
    event->filter_info->url = *filter_url;
  }
  if (tab_index) {
    // The callback does not see the original args, so it keeps a copy.
    base::ListValue original = event->event_args.Clone();
    event->will_dispatch_callback = base::BindRepeating(
        [](base::WeakPtr<content::WebContents> web_contents, size_t index,
           const base::ListValue& original, content::BrowserContext*,
           mojom::ContextType, const Extension* extension,
           const base::DictValue*,
           std::optional<base::ListValue>& event_args_out,
           mojom::EventFilteringInfoPtr&, bool*) {
          if (!web_contents) {
            return true;
          }
          base::ListValue args = original.Clone();
          args[index] = base::Value(
              ShellTabs::CreateTabObject(web_contents.get(), extension)
                  .ToValue());
          event_args_out = std::move(args);
          return true;
        },
        web_contents->GetWeakPtr(), *tab_index, std::move(original));
  }
  router->BroadcastEvent(std::move(event));
}

std::string TransitionTypeString(ui::PageTransition transition) {
  if (ui::PageTransitionCoreTypeIs(transition,
                                   ui::PAGE_TRANSITION_AUTO_TOPLEVEL)) {
    return "start_page";
  }
  return ui::PageTransitionGetCoreTransitionString(transition);
}

base::ListValue TransitionQualifiers(ui::PageTransition transition) {
  base::ListValue qualifiers;
  if (transition & ui::PAGE_TRANSITION_CLIENT_REDIRECT) {
    qualifiers.Append("client_redirect");
  }
  if (transition & ui::PAGE_TRANSITION_SERVER_REDIRECT) {
    qualifiers.Append("server_redirect");
  }
  if (transition & ui::PAGE_TRANSITION_FORWARD_BACK) {
    qualifiers.Append("forward_back");
  }
  if (transition & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR) {
    qualifiers.Append("from_address_bar");
  }
  return qualifiers;
}

// Fills the fields every webNavigation frame event shares.
template <typename Details>
void FillFrameDetails(Details& details,
                      int tab_id,
                      content::RenderFrameHost* frame,
                      const GURL& url) {
  details.tab_id = tab_id;
  details.url = url.spec();
  details.process_id = frame->GetProcess()->GetDeprecatedID();
  details.frame_id = ExtensionApiFrameIdMap::GetFrameId(frame);
  details.parent_frame_id = ExtensionApiFrameIdMap::GetParentFrameId(frame);
  details.document_id = ExtensionApiFrameIdMap::GetDocumentId(frame).ToString();
  if (content::RenderFrameHost* parent = frame->GetParentOrOuterDocument()) {
    details.parent_document_id =
        ExtensionApiFrameIdMap::GetDocumentId(parent).ToString();
  }
  details.frame_type = ExtensionApiFrameIdMap::GetFrameType(frame);
  details.document_lifecycle =
      ExtensionApiFrameIdMap::GetDocumentLifecycle(frame);
  details.time_stamp = Now();
}

// Per tab: the script executor and the tabs/webNavigation event source.
class ShellTabHelper : public content::WebContentsObserver,
                       public content::WebContentsUserData<ShellTabHelper> {
 public:
  ~ShellTabHelper() override = default;

  ScriptExecutor* script_executor() { return &script_executor_; }

  void DispatchCreated() {
    base::ListValue args;
    args.Append(ShellTabs::CreateTabObject(web_contents(), nullptr).ToValue());
    DispatchEvent(web_contents(), events::TABS_ON_CREATED,
                  tabs::OnCreated::kEventName, std::move(args), 0);
  }

  void Activate() {
    if (g_active_tab == web_contents()) {
      return;
    }
    g_active_tab = web_contents();
    tabs::OnActivated::ActiveInfo info;
    info.tab_id = ShellTabs::GetTabId(web_contents());
    info.window_id = ShellTabs::GetWindowId(web_contents());
    DispatchEvent(web_contents(), events::TABS_ON_ACTIVATED,
                  tabs::OnActivated::kEventName,
                  tabs::OnActivated::Create(info));
    NotifyTabChanged(web_contents(), /*navigated=*/false);
  }

 private:
  friend class content::WebContentsUserData<ShellTabHelper>;

  explicit ShellTabHelper(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        content::WebContentsUserData<ShellTabHelper>(*web_contents),
        script_executor_(web_contents) {
    Tabs().push_back(web_contents);
  }

  void DispatchUpdated(tabs::OnUpdated::ChangeInfo change_info) {
    const int tab_id = ShellTabs::GetTabId(web_contents());
    base::ListValue args;
    args.Append(tab_id);
    args.Append(change_info.ToValue());
    args.Append(ShellTabs::CreateTabObject(web_contents(), nullptr).ToValue());
    DispatchEvent(web_contents(), events::TABS_ON_UPDATED,
                  tabs::OnUpdated::kEventName, std::move(args), 2);
  }

  // content::WebContentsObserver:
  void OnWebContentsFocused(content::RenderWidgetHost*) override {
    Activate();
  }

  void DidStartLoading() override {
    tabs::OnUpdated::ChangeInfo change_info;
    change_info.status = tabs::TabStatus::kLoading;
    DispatchUpdated(std::move(change_info));
  }

  void DidStopLoading() override {
    tabs::OnUpdated::ChangeInfo change_info;
    change_info.status = tabs::TabStatus::kComplete;
    DispatchUpdated(std::move(change_info));
  }

  void TitleWasSet(content::NavigationEntry* entry) override {
    tabs::OnUpdated::ChangeInfo change_info;
    change_info.title = base::UTF16ToUTF8(web_contents()->GetTitle());
    DispatchUpdated(std::move(change_info));
  }

  void DidStartNavigation(content::NavigationHandle* handle) override {
    if (handle->IsSameDocument() || !IsPrimary(handle)) {
      return;
    }
    web_navigation::OnBeforeNavigate::Details details;
    details.tab_id = ShellTabs::GetTabId(web_contents());
    details.url = handle->GetURL().spec();
    details.process_id = -1;
    details.frame_id = ExtensionApiFrameIdMap::GetFrameId(handle);
    details.parent_frame_id = ExtensionApiFrameIdMap::GetParentFrameId(handle);
    if (content::RenderFrameHost* parent =
            handle->GetParentFrameOrOuterDocument()) {
      details.parent_document_id =
          ExtensionApiFrameIdMap::GetDocumentId(parent).ToString();
    }
    details.frame_type = ExtensionApiFrameIdMap::GetFrameType(handle);
    details.document_lifecycle =
        ExtensionApiFrameIdMap::GetDocumentLifecycle(handle);
    details.time_stamp = Now();
    const GURL url = handle->GetURL();
    DispatchEvent(web_contents(), events::WEB_NAVIGATION_ON_BEFORE_NAVIGATE,
                  web_navigation::OnBeforeNavigate::kEventName,
                  web_navigation::OnBeforeNavigate::Create(details),
                  std::nullopt, &url);
  }

  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (!IsPrimary(handle)) {
      return;
    }
    if (!handle->HasCommitted()) {
      if (handle->GetNetErrorCode() != net::OK &&
          handle->GetNetErrorCode() != net::ERR_ABORTED &&
          handle->GetRenderFrameHost()) {
        web_navigation::OnErrorOccurred::Details details;
        FillFrameDetails(details, ShellTabs::GetTabId(web_contents()),
                         handle->GetRenderFrameHost(), handle->GetURL());
        details.error = net::ErrorToString(handle->GetNetErrorCode());
        const GURL url = handle->GetURL();
        DispatchEvent(web_contents(), events::WEB_NAVIGATION_ON_ERROR_OCCURRED,
                      web_navigation::OnErrorOccurred::kEventName,
                      web_navigation::OnErrorOccurred::Create(details),
                      std::nullopt, &url);
      }
      return;
    }

    if (handle->IsInPrimaryMainFrame() && !handle->IsSameDocument()) {
      tabs::OnUpdated::ChangeInfo change_info;
      change_info.url = handle->GetURL().spec();
      DispatchUpdated(std::move(change_info));
      NotifyTabChanged(web_contents(), /*navigated=*/true);
    }
    if (handle->IsSameDocument()) {
      return;
    }

    // onCommitted carries its transition as strings, as Chrome builds it.
    content::RenderFrameHost* frame = handle->GetRenderFrameHost();
    ui::PageTransition transition = handle->GetPageTransition();
    if (handle->WasServerRedirect()) {
      transition = ui::PageTransitionFromInt(
          transition | ui::PAGE_TRANSITION_SERVER_REDIRECT);
    }
    base::DictValue dict;
    dict.Set("tabId", ShellTabs::GetTabId(web_contents()));
    dict.Set("url", handle->GetURL().spec());
    dict.Set("processId", frame->GetProcess()->GetDeprecatedID());
    dict.Set("frameId", ExtensionApiFrameIdMap::GetFrameId(frame));
    dict.Set("parentFrameId", ExtensionApiFrameIdMap::GetParentFrameId(frame));
    dict.Set("documentId",
             ExtensionApiFrameIdMap::GetDocumentId(frame).ToString());
    if (content::RenderFrameHost* parent = frame->GetParentOrOuterDocument()) {
      dict.Set("parentDocumentId",
               ExtensionApiFrameIdMap::GetDocumentId(parent).ToString());
    }
    dict.Set("frameType", api::extension_types::ToString(
                              ExtensionApiFrameIdMap::GetFrameType(frame)));
    dict.Set("documentLifecycle",
             api::extension_types::ToString(
                 ExtensionApiFrameIdMap::GetDocumentLifecycle(frame)));
    dict.Set("transitionType", TransitionTypeString(transition));
    dict.Set("transitionQualifiers", TransitionQualifiers(transition));
    dict.Set("timeStamp", Now());
    base::ListValue args;
    args.Append(std::move(dict));
    const GURL url = handle->GetURL();
    DispatchEvent(web_contents(), events::WEB_NAVIGATION_ON_COMMITTED,
                  web_navigation::OnCommitted::kEventName, std::move(args),
                  std::nullopt, &url);
  }

  void DOMContentLoaded(content::RenderFrameHost* frame) override {
    if (!frame->GetPage().IsPrimary()) {
      return;
    }
    web_navigation::OnDOMContentLoaded::Details details;
    const GURL url = frame->GetLastCommittedURL();
    FillFrameDetails(details, ShellTabs::GetTabId(web_contents()), frame, url);
    DispatchEvent(web_contents(), events::WEB_NAVIGATION_ON_DOM_CONTENT_LOADED,
                  web_navigation::OnDOMContentLoaded::kEventName,
                  web_navigation::OnDOMContentLoaded::Create(details),
                  std::nullopt, &url);
  }

  void DidFinishLoad(content::RenderFrameHost* frame,
                     const GURL& validated_url) override {
    if (!frame->GetPage().IsPrimary()) {
      return;
    }
    web_navigation::OnCompleted::Details details;
    FillFrameDetails(details, ShellTabs::GetTabId(web_contents()), frame,
                     validated_url);
    DispatchEvent(web_contents(), events::WEB_NAVIGATION_ON_COMPLETED,
                  web_navigation::OnCompleted::kEventName,
                  web_navigation::OnCompleted::Create(details), std::nullopt,
                  &validated_url);
  }

  void WebContentsDestroyed() override {
    tabs::OnRemoved::RemoveInfo info;
    info.window_id = ShellTabs::GetWindowId(web_contents());
    info.is_window_closing = true;
    DispatchEvent(web_contents(), events::TABS_ON_REMOVED,
                  tabs::OnRemoved::kEventName,
                  tabs::OnRemoved::Create(ShellTabs::GetTabId(web_contents()),
                                          info));
    std::erase(Tabs(), web_contents());
    if (g_active_tab == web_contents()) {
      g_active_tab = Tabs().empty() ? nullptr : Tabs().back().get();
      if (g_active_tab) {
        NotifyTabChanged(g_active_tab, /*navigated=*/false);
      }
    }
  }

  static bool IsPrimary(content::NavigationHandle* handle) {
    return handle->IsInPrimaryMainFrame() ||
           (handle->GetParentFrameOrOuterDocument() &&
            handle->GetParentFrameOrOuterDocument()->GetPage().IsPrimary());
  }

  ScriptExecutor script_executor_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ShellTabHelper);

}  // namespace

// static
void ShellTabs::AttachTab(content::WebContents* web_contents) {
  if (ShellTabHelper::FromWebContents(web_contents)) {
    return;
  }
  sessions::SessionTabHelper::CreateForWebContents(
      web_contents,
      base::BindRepeating([](content::WebContents*)
                              -> sessions::SessionTabHelperDelegate* {
        return nullptr;
      }));
  // One tab per window: the window ID is as unique as the tab.
  sessions::SessionTabHelper::FromWebContents(web_contents)
      ->SetWindowID(SessionID::NewUnique());

  ShellTabHelper::CreateForWebContents(web_contents);
  ShellTabHelper* helper = ShellTabHelper::FromWebContents(web_contents);
  helper->DispatchCreated();
  helper->Activate();
}

// static
void ShellTabs::SetTabChangedCallback(TabChangedCallback callback) {
  TabChanged() = std::move(callback);
}

// static
int ShellTabs::GetTabId(const content::WebContents* web_contents) {
  return sessions::SessionTabHelper::IdForTab(web_contents).id();
}

// static
int ShellTabs::GetWindowId(const content::WebContents* web_contents) {
  return sessions::SessionTabHelper::IdForWindowContainingTab(web_contents)
      .id();
}

// static
std::vector<content::WebContents*> ShellTabs::GetTabs(
    content::BrowserContext* context) {
  std::vector<content::WebContents*> result;
  for (content::WebContents* tab : Tabs()) {
    if (tab->GetBrowserContext() == context) {
      result.push_back(tab);
    }
  }
  return result;
}

// static
content::WebContents* ShellTabs::GetTabById(content::BrowserContext* context,
                                            int tab_id) {
  for (content::WebContents* tab : GetTabs(context)) {
    if (GetTabId(tab) == tab_id) {
      return tab;
    }
  }
  return nullptr;
}

// static
content::WebContents* ShellTabs::GetActiveTab(
    content::BrowserContext* context) {
  if (g_active_tab && g_active_tab->GetBrowserContext() == context) {
    return g_active_tab;
  }
  std::vector<content::WebContents*> tabs = GetTabs(context);
  return tabs.empty() ? nullptr : tabs.back();
}

// static
ScriptExecutor* ShellTabs::GetScriptExecutor(
    content::WebContents* web_contents) {
  ShellTabHelper* helper = ShellTabHelper::FromWebContents(web_contents);
  return helper ? helper->script_executor() : nullptr;
}

// static
tabs::Tab ShellTabs::CreateTabObject(content::WebContents* web_contents,
                                     const Extension* extension) {
  tabs::Tab tab;
  const int tab_id = GetTabId(web_contents);
  tab.id = tab_id;
  tab.index = 0;
  tab.window_id = GetWindowId(web_contents);
  tab.group_id = -1;
  const bool active = web_contents == GetActiveTab(
                                          web_contents->GetBrowserContext());
  tab.active = active;
  tab.selected = active;
  tab.highlighted = active;
  tab.pinned = false;
  tab.frozen = false;
  tab.discarded = false;
  tab.auto_discardable = true;
  tab.incognito = web_contents->GetBrowserContext()->IsOffTheRecord();
  tab.last_accessed =
      web_contents->GetLastActiveTime().InMillisecondsFSinceUnixEpoch();
  tab.status = web_contents->IsLoading() ? tabs::TabStatus::kLoading
                                         : tabs::TabStatus::kComplete;
  const gfx::Rect bounds = web_contents->GetContainerBounds();
  tab.width = bounds.width();
  tab.height = bounds.height();

  const GURL url = web_contents->GetLastCommittedURL();
  if (MaySeeTabDetails(extension, tab_id, url)) {
    tab.url = url.spec();
    tab.title = base::UTF16ToUTF8(web_contents->GetTitle());
    content::NavigationEntry* entry =
        web_contents->GetController().GetLastCommittedEntry();
    if (entry && entry->GetFavicon().valid) {
      tab.fav_icon_url = entry->GetFavicon().url.spec();
    }
    content::NavigationEntry* pending =
        web_contents->GetController().GetPendingEntry();
    if (pending) {
      tab.pending_url = pending->GetVirtualURL().spec();
    }
  }
  return tab;
}

}  // namespace extensions
