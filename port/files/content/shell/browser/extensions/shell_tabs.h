// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_H_

#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "content/shell/common/extensions/api/tabs.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace extensions {

class Extension;
class ScriptExecutor;

// R Chromium: the extension API's notion of tabs. Each Shell window is one
// window holding one tab, its WebContents. Tab IDs come from
// sessions::SessionTabHelper, as //extensions itself reads them; every tab
// also gets its own window ID. AttachTab() adds the tab-side services: a
// ScriptExecutor (chrome.scripting) and the observer that dispatches the
// tabs.* and webNavigation.* events.
class ShellTabs {
 public:
  ShellTabs() = delete;

  static void AttachTab(content::WebContents* web_contents);

  // -1 if `web_contents` is not a tab.
  static int GetTabId(const content::WebContents* web_contents);
  static int GetWindowId(const content::WebContents* web_contents);

  // The tabs of `context`, oldest first.
  static std::vector<content::WebContents*> GetTabs(
      content::BrowserContext* context);
  static content::WebContents* GetTabById(content::BrowserContext* context,
                                          int tab_id);
  // The most recently focused tab: what "active" and "current window" mean
  // here.
  static content::WebContents* GetActiveTab(content::BrowserContext* context);

  static ScriptExecutor* GetScriptExecutor(content::WebContents* web_contents);

  // Called when a tab becomes active, and (navigated == true) when one
  // commits a new main-frame document. Null to stop.
  using TabChangedCallback =
      base::RepeatingCallback<void(content::WebContents*, bool navigated)>;
  static void SetTabChangedCallback(TabChangedCallback callback);

  // The tabs.Tab for `web_contents`. URL, title and favicon are left out
  // unless `extension` may see them (the "tabs" permission or host access);
  // a null `extension` sees everything.
  static api::tabs::Tab CreateTabObject(content::WebContents* web_contents,
                                        const Extension* extension);
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_H_
