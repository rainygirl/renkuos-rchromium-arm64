// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_API_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_API_H_

#include "base/memory/raw_ptr.h"
#include "base/observer_list_types.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_function.h"
#include "extensions/common/extension_id.h"

namespace content {
class BrowserContext;
}

// R Chromium: chrome.action over //extensions' ExtensionAction model,
// adapted from chrome/browser/extensions/api/extension_action. Whatever
// shows actions (the native toolbar) observes ShellActions.

namespace extensions {

class ShellActions {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // An action's icon, title, badge, popup or visibility changed, for one
    // tab or (tab_id == ExtensionAction::kDefaultTabId) for all of them.
    virtual void OnActionUpdated(const ExtensionId& extension_id,
                                 int tab_id) {}
    // chrome.action.openPopup(): show the popup for the active tab. Returns
    // true if the observer did.
    virtual bool OnOpenPopupRequested(const ExtensionId& extension_id);
  };

  ShellActions() = delete;

  static void AddObserver(Observer* observer);
  static void RemoveObserver(Observer* observer);
  static void NotifyActionUpdated(const ExtensionId& extension_id, int tab_id);
  static bool RequestOpenPopup(const ExtensionId& extension_id);
};

// The parts every action function shares: the action, and the optional tab
// ID (an integer argument, or details.tabId).
class ShellActionFunction : public ExtensionFunction {
 protected:
  ~ShellActionFunction() override = default;

  ResponseAction Run() override;
  virtual ResponseAction RunAction() = 0;

  void NotifyChange();

  raw_ptr<ExtensionAction> action_ = nullptr;
  int tab_id_ = ExtensionAction::kDefaultTabId;
  raw_ptr<const base::DictValue> details_ = nullptr;

 private:
  bool ExtractDataFromArguments();
};

#define V_CHROMIUM_ACTION_FUNCTION(ClassName, name, histogram) \
  class ClassName : public ShellActionFunction {               \
   public:                                                     \
    DECLARE_EXTENSION_FUNCTION(name, histogram)                \
                                                               \
   protected:                                                  \
    ~ClassName() override = default;                           \
    ResponseAction RunAction() override;                       \
  }

V_CHROMIUM_ACTION_FUNCTION(ActionSetIconFunction,
                           "action.setIcon",
                           ACTION_SETICON);
V_CHROMIUM_ACTION_FUNCTION(ActionSetTitleFunction,
                           "action.setTitle",
                           ACTION_SETTITLE);
V_CHROMIUM_ACTION_FUNCTION(ActionGetTitleFunction,
                           "action.getTitle",
                           ACTION_GETTITLE);
V_CHROMIUM_ACTION_FUNCTION(ActionSetPopupFunction,
                           "action.setPopup",
                           ACTION_SETPOPUP);
V_CHROMIUM_ACTION_FUNCTION(ActionGetPopupFunction,
                           "action.getPopup",
                           ACTION_GETPOPUP);
V_CHROMIUM_ACTION_FUNCTION(ActionSetBadgeTextFunction,
                           "action.setBadgeText",
                           ACTION_SETBADGETEXT);
V_CHROMIUM_ACTION_FUNCTION(ActionGetBadgeTextFunction,
                           "action.getBadgeText",
                           ACTION_GETBADGETEXT);
V_CHROMIUM_ACTION_FUNCTION(ActionSetBadgeBackgroundColorFunction,
                           "action.setBadgeBackgroundColor",
                           ACTION_SETBADGEBACKGROUNDCOLOR);
V_CHROMIUM_ACTION_FUNCTION(ActionGetBadgeBackgroundColorFunction,
                           "action.getBadgeBackgroundColor",
                           ACTION_GETBADGEBACKGROUNDCOLOR);
V_CHROMIUM_ACTION_FUNCTION(ActionSetBadgeTextColorFunction,
                           "action.setBadgeTextColor",
                           ACTION_SETBADGETEXTCOLOR);
V_CHROMIUM_ACTION_FUNCTION(ActionGetBadgeTextColorFunction,
                           "action.getBadgeTextColor",
                           ACTION_GETBADGETEXTCOLOR);
V_CHROMIUM_ACTION_FUNCTION(ActionEnableFunction,
                           "action.enable",
                           ACTION_ENABLE);
V_CHROMIUM_ACTION_FUNCTION(ActionDisableFunction,
                           "action.disable",
                           ACTION_DISABLE);
V_CHROMIUM_ACTION_FUNCTION(ActionIsEnabledFunction,
                           "action.isEnabled",
                           ACTION_ISENABLED);

#undef V_CHROMIUM_ACTION_FUNCTION

class ActionGetUserSettingsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.getUserSettings", ACTION_GETUSERSETTINGS)

 protected:
  ~ActionGetUserSettingsFunction() override = default;
  ResponseAction Run() override;
};

class ActionOpenPopupFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.openPopup", ACTION_OPENPOPUP)

 protected:
  ~ActionOpenPopupFunction() override = default;
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_API_H_
