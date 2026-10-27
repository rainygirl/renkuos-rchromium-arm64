// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_action_api.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "content/public/common/color_parser.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/icon_util.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "url/gurl.h"

namespace extensions {

namespace {

constexpr char kNoExtensionActionError[] =
    "This extension has no action specified.";
constexpr char kNoTabError[] = "No tab with id: *.";
constexpr size_t kMaxBadgeTextSize = 100;

base::ObserverList<ShellActions::Observer>& Observers() {
  static base::NoDestructor<base::ObserverList<ShellActions::Observer>>
      observers;
  return *observers;
}

// A CSS color string, or [r, g, b, a].
bool ParseColor(const base::Value& value, SkColor& color) {
  if (value.is_string()) {
    return content::ParseCssColorString(value.GetString(), &color);
  }
  if (!value.is_list()) {
    return false;
  }
  const base::ListValue& list = value.GetList();
  if (list.size() != 4 ||
      std::ranges::any_of(list, [](const auto& v) { return !v.is_int(); })) {
    return false;
  }
  color = SkColorSetARGB(list[3].GetInt(), list[0].GetInt(), list[1].GetInt(),
                         list[2].GetInt());
  return true;
}

base::ListValue ColorList(SkColor color) {
  base::ListValue list;
  list.Append(static_cast<int>(SkColorGetR(color)));
  list.Append(static_cast<int>(SkColorGetG(color)));
  list.Append(static_cast<int>(SkColorGetB(color)));
  list.Append(static_cast<int>(SkColorGetA(color)));
  return list;
}

}  // namespace

bool ShellActions::Observer::OnOpenPopupRequested(
    const ExtensionId& extension_id) {
  return false;
}

// static
void ShellActions::AddObserver(Observer* observer) {
  Observers().AddObserver(observer);
}

// static
void ShellActions::RemoveObserver(Observer* observer) {
  Observers().RemoveObserver(observer);
}

// static
void ShellActions::NotifyActionUpdated(const ExtensionId& extension_id,
                                       int tab_id) {
  for (Observer& observer : Observers()) {
    observer.OnActionUpdated(extension_id, tab_id);
  }
}

// static
bool ShellActions::RequestOpenPopup(const ExtensionId& extension_id) {
  for (Observer& observer : Observers()) {
    if (observer.OnOpenPopupRequested(extension_id)) {
      return true;
    }
  }
  return false;
}

ExtensionFunction::ResponseAction ShellActionFunction::Run() {
  action_ = ExtensionActionManager::Get(browser_context())
                ->GetExtensionAction(*extension());
  if (!action_) {
    return RespondNow(Error(kNoExtensionActionError));
  }
  EXTENSION_FUNCTION_VALIDATE(ExtractDataFromArguments());
  if (tab_id_ != ExtensionAction::kDefaultTabId &&
      !ShellTabs::GetTabById(browser_context(), tab_id_)) {
    return RespondNow(Error(kNoTabError, base::NumberToString(tab_id_)));
  }
  return RunAction();
}

bool ShellActionFunction::ExtractDataFromArguments() {
  if (args().empty()) {
    return true;
  }
  const base::Value& first_arg = args()[0];
  switch (first_arg.type()) {
    case base::Value::Type::INTEGER:
      tab_id_ = first_arg.GetInt();
      return true;
    case base::Value::Type::DICT: {
      details_ = &first_arg.GetDict();
      const base::Value* tab_id = details_->Find("tabId");
      if (!tab_id || tab_id->is_none()) {
        return true;
      }
      if (!tab_id->is_int()) {
        return false;
      }
      tab_id_ = tab_id->GetInt();
      return true;
    }
    case base::Value::Type::NONE:
      return true;
    default:
      return false;
  }
}

void ShellActionFunction::NotifyChange() {
  ShellActions::NotifyActionUpdated(extension_id(), tab_id_);
}

ExtensionFunction::ResponseAction ActionSetIconFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  if (const base::DictValue* canvas_set = details_->FindDict("imageData")) {
    gfx::ImageSkia icon;
    EXTENSION_FUNCTION_VALIDATE(ParseIconFromCanvasDictionary(
                                    *canvas_set, &icon) ==
                                IconParseResult::kSuccess);
    if (icon.isNull()) {
      return RespondNow(Error("Icon invalid."));
    }
    action_->SetIcon(tab_id_, gfx::Image(icon));
  } else if (details_->FindInt("iconIndex")) {
    // Deprecated; Chrome ignores it too.
    return RespondNow(NoArguments());
  } else {
    // A "path" argument is turned into imageData by the renderer bindings.
    EXTENSION_FUNCTION_VALIDATE(false);
  }
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionSetTitleFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  const std::string* title = details_->FindString("title");
  EXTENSION_FUNCTION_VALIDATE(title);
  action_->SetTitle(tab_id_, *title);
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionGetTitleFunction::RunAction() {
  return RespondNow(WithArguments(action_->GetTitle(tab_id_)));
}

ExtensionFunction::ResponseAction ActionSetPopupFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  const std::string* popup = details_->FindString("popup");
  EXTENSION_FUNCTION_VALIDATE(popup);
  GURL popup_url;
  if (!popup->empty()) {
    popup_url = extension()->ResolveExtensionURL(*popup);
    if (!popup_url.is_valid()) {
      return RespondNow(Error(manifest_errors::kInvalidExtensionPopupPath));
    }
  }
  action_->SetPopupUrl(tab_id_, popup_url);
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionGetPopupFunction::RunAction() {
  return RespondNow(WithArguments(action_->GetPopupUrl(tab_id_).spec()));
}

ExtensionFunction::ResponseAction ActionSetBadgeTextFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  if (const std::string* text = details_->FindString("text")) {
    if (text->length() > kMaxBadgeTextSize) {
      return RespondNow(Error(base::StringPrintf(
          "Badge text size is %zu bytes which exceeds the limit of %zu bytes.",
          text->length(), kMaxBadgeTextSize)));
    }
    action_->SetBadgeText(tab_id_, *text);
  } else {
    action_->ClearBadgeText(tab_id_);
  }
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionGetBadgeTextFunction::RunAction() {
  return RespondNow(WithArguments(action_->GetDisplayBadgeText(tab_id_)));
}

ExtensionFunction::ResponseAction
ActionSetBadgeBackgroundColorFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  const base::Value* color_value = details_->Find("color");
  EXTENSION_FUNCTION_VALIDATE(color_value);
  SkColor color = 0;
  if (!ParseColor(*color_value, color)) {
    return RespondNow(Error(extension_misc::kInvalidColorError));
  }
  action_->SetBadgeBackgroundColor(tab_id_, color);
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction
ActionGetBadgeBackgroundColorFunction::RunAction() {
  return RespondNow(
      WithArguments(ColorList(action_->GetBadgeBackgroundColor(tab_id_))));
}

ExtensionFunction::ResponseAction ActionSetBadgeTextColorFunction::RunAction() {
  EXTENSION_FUNCTION_VALIDATE(details_);
  const base::Value* color_value = details_->Find("color");
  EXTENSION_FUNCTION_VALIDATE(color_value);
  SkColor color = 0;
  if (!ParseColor(*color_value, color) ||
      SkColorGetA(color) == SK_AlphaTRANSPARENT) {
    return RespondNow(Error(extension_misc::kInvalidColorError));
  }
  action_->SetBadgeTextColor(tab_id_, color);
  NotifyChange();
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionGetBadgeTextColorFunction::RunAction() {
  return RespondNow(
      WithArguments(ColorList(action_->GetBadgeTextColor(tab_id_))));
}

ExtensionFunction::ResponseAction ActionEnableFunction::RunAction() {
  if (action_->SetIsVisible(tab_id_, true)) {
    NotifyChange();
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionDisableFunction::RunAction() {
  if (action_->SetIsVisible(tab_id_, false)) {
    NotifyChange();
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction ActionIsEnabledFunction::RunAction() {
  return RespondNow(
      WithArguments(action_->GetIsVisibleIgnoringDeclarative(tab_id_)));
}

ExtensionFunction::ResponseAction ActionGetUserSettingsFunction::Run() {
  // Every action is shown in the toolbar: there is no overflow menu.
  base::DictValue settings;
  settings.Set("isOnToolbar", true);
  return RespondNow(WithArguments(std::move(settings)));
}

ExtensionFunction::ResponseAction ActionOpenPopupFunction::Run() {
  if (!ShellActions::RequestOpenPopup(extension_id())) {
    return RespondNow(Error("Failed to open popup."));
  }
  return RespondNow(NoArguments());
}

}  // namespace extensions
