// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_TOOLBAR_BRIDGE_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_TOOLBAR_BRIDGE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

// The narrow seam between content_shell's Haiku delegate and the ozone Haiku
// window, so the delegate never has to include an ozone platform-internal
// header. Everything here is addressed by AcceleratedWidget and is
// UI-thread-only: the window posts toolbar events to the UI thread before
// calling an observer, and the delegate pushes state from the UI thread.

namespace ui {

// Implemented by content_shell's delegate; called when the user drives the
// native toolbar. On the UI thread.
class HaikuToolbarObserver {
 public:
  virtual void OnNavigateBack() = 0;
  virtual void OnNavigateForward() = 0;
  virtual void OnReloadOrStop() = 0;
  virtual void OnNavigateToURL(const std::string& text) = 0;
  // The install button, shown only while HaikuToolbarSetInstallable()
  // says the page carries a usable web app manifest.
  virtual void OnInstall() = 0;
  // An extension's toolbar button. `anchor` is its frame in screen
  // coordinates, for placing the popup.
  virtual void OnExtensionActionClicked(const std::string& id,
                                        const gfx::Rect& anchor) {}

 protected:
  ~HaikuToolbarObserver() = default;
};

// Register/clear the observer for a window. The delegate calls these in
// CreatePlatformWindow / CleanUp.
void SetHaikuToolbarObserver(gfx::AcceleratedWidget widget,
                             HaikuToolbarObserver* observer);

// State pushed from the delegate into the toolbar. No-ops if the widget has no
// window or the window has no toolbar.
void HaikuToolbarSetAddress(gfx::AcceleratedWidget widget,
                            const std::string& url);
void HaikuToolbarSetLoading(gfx::AcceleratedWidget widget, bool loading);
void HaikuToolbarSetNavigationEnabled(gfx::AcceleratedWidget widget,
                                      bool back,
                                      bool forward);
void HaikuToolbarSetTitle(gfx::AcceleratedWidget widget,
                          const std::string& title);
// Show or hide the install button; `app_name` goes in its tooltip.
void HaikuToolbarSetInstallable(gfx::AcceleratedWidget widget,
                                bool installable,
                                const std::string& app_name);

// One extension's toolbar button, as it should look for the window's tab.
struct HaikuToolbarAction {
  HaikuToolbarAction();
  HaikuToolbarAction(const HaikuToolbarAction&);
  HaikuToolbarAction& operator=(const HaikuToolbarAction&);
  ~HaikuToolbarAction();

  std::string id;  // The extension ID.
  // width*height non-premultiplied 0xAARRGGBB pixels (SkColor's layout);
  // empty for no icon.
  std::vector<uint32_t> argb;
  int width = 0;
  int height = 0;
  std::string badge_text;
  uint32_t badge_background = 0;
  uint32_t badge_text_color = 0;
  std::string tooltip;
  bool enabled = true;
};

// Add or update, and remove, an extension's toolbar button.
void HaikuToolbarSetAction(gfx::AcceleratedWidget widget,
                           const HaikuToolbarAction& action);
void HaikuToolbarRemoveAction(gfx::AcceleratedWidget widget,
                              const std::string& id);

// Write a file's Haiku icon attributes from a 32-bit ARGB image, so an
// installed web app's launcher carries the site's icon. `argb` is
// width*height non-premultiplied 0xAARRGGBB pixels, which is SkColor's
// layout: content_shell's delegate never names a BeAPI type and the shim
// never names a Skia one.
bool HaikuSetFileIcon(const std::string& path,
                      const uint32_t* argb,
                      int width,
                      int height);

// Called by HaikuWindow (on the UI thread) to reach the registered observer.
// Returns null if none is registered for the widget.
HaikuToolbarObserver* GetHaikuToolbarObserver(gfx::AcceleratedWidget widget);

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_TOOLBAR_BRIDGE_H_
