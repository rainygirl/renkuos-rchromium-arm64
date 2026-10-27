// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_TOOLBAR_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_TOOLBAR_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/shell/browser/extensions/shell_action_api.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace extensions {

class ExtensionPopup;

// R Chromium: puts every enabled extension's action in the native BeAPI
// toolbar (icon, badge, tooltip, for the active tab) and handles clicks: the
// action's popup opens in a borderless window under the button, or, without
// a popup, action.onClicked fires. One per browser; each toolbar window
// registered with AddWindow() shows the state for its own tab.
class ShellActionToolbar : public ShellActions::Observer,
                           public ExtensionRegistryObserver {
 public:
  explicit ShellActionToolbar(content::BrowserContext* context);
  ShellActionToolbar(const ShellActionToolbar&) = delete;
  ShellActionToolbar& operator=(const ShellActionToolbar&) = delete;
  ~ShellActionToolbar() override;

  // A toolbar window, by its AcceleratedWidget, and the tab it shows.
  void AddWindow(gfx::AcceleratedWidget widget, content::WebContents* tab);
  void RemoveWindow(gfx::AcceleratedWidget widget);

  // A tab became active, or (`navigated`) committed a new document: its
  // tab-specific action state from the previous page is dropped, as Chrome
  // does.
  void OnTabChanged(content::WebContents* tab, bool navigated);

  // A click on extension `id`'s button in the window showing `tab`; `anchor`
  // is in screen coordinates.
  void OnActionClicked(content::WebContents* tab,
                       const std::string& id,
                       const gfx::Rect& anchor);

  // ShellActions::Observer:
  void OnActionUpdated(const ExtensionId& extension_id, int tab_id) override;
  bool OnOpenPopupRequested(const ExtensionId& extension_id) override;

  // ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionReason reason) override;

 private:
  // Re-renders the icon when it finishes loading.
  class IconObserver : public ExtensionActionIconFactory::Observer {
   public:
    IconObserver(ShellActionToolbar* toolbar, const ExtensionId& id)
        : toolbar_(toolbar), id_(id) {}
    void OnIconUpdated() override;

   private:
    raw_ptr<ShellActionToolbar> toolbar_;
    ExtensionId id_;
  };

  struct Entry {
    Entry();
    ~Entry();
    std::unique_ptr<IconObserver> icon_observer;
    std::unique_ptr<ExtensionActionIconFactory> icon_factory;
  };

  // Shows extension `id`'s button as it should look now, in every window
  // (or only those showing tab `only_tab_id`).
  void Push(const ExtensionId& id, int only_tab_id = -1);
  void PushAll();
  void OpenPopup(const Extension& extension,
                 const GURL& url,
                 const gfx::Rect& anchor);
  void ClosePopup();

  raw_ptr<content::BrowserContext> context_;
  std::map<gfx::AcceleratedWidget, raw_ptr<content::WebContents>> windows_;
  std::map<ExtensionId, Entry> entries_;
  // The open popup, if any, and the action button it belongs to.
  std::unique_ptr<ExtensionPopup> popup_;
  ExtensionId popup_extension_id_;
  // Where the last click on each button was, for openPopup().
  std::map<ExtensionId, gfx::Rect> anchors_;

  base::ScopedObservation<ExtensionRegistry, ExtensionRegistryObserver>
      registry_observation_{this};
  base::WeakPtrFactory<ShellActionToolbar> weak_factory_{this};
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_ACTION_TOOLBAR_H_
