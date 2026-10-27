// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/haiku_window.h"

// BBitmap is deleted here when a frame cannot be handed to the window.
#include <Bitmap.h>
#include <stdlib.h>

#include <map>
#include <memory>
#include <utility>

#include "base/no_destructor.h"

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/event.h"
#include "ui/ozone/platform/haiku/haiku_beapi.h"
#include "ui/ozone/platform/haiku/haiku_shim.h"
#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"
#include "base/synchronization/lock.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"

namespace ui {

namespace {

// widget -> HaikuWindow, so the toolbar-bridge free functions can find the
// window to push state into. UI-thread-only, like the bridge's observer map.
std::map<gfx::AcceleratedWidget, HaikuWindow*>& ToolbarWindowMap() {
  static base::NoDestructor<std::map<gfx::AcceleratedWidget, HaikuWindow*>> m;
  return *m;
}

// RCH_APP_NAME is how an installed web app's launcher says what it is called.
// It wins over whatever the page calls itself: an installed app is one
// application, and a window that renames itself as the user moves around
// inside it does not look like one.
const char* AppName() {
  const char* name = getenv("RCH_APP_NAME");
  return (name != nullptr && name[0] != '\0') ? name : nullptr;
}

}  // namespace

HaikuPresentTarget::HaikuPresentTarget(haiku_shim::NativeWindow* window)
    : window_(window) {}

HaikuPresentTarget::~HaikuPresentTarget() = default;

void HaikuPresentTarget::Present(BBitmap* bitmap, const gfx::Rect& damage) {
  base::AutoLock guard(lock_);
  if (!window_) {
    delete bitmap;
    return;
  }
  // The view owns the front buffer and repaints from it, so hand the new one
  // over and drop the old. PresentBitmap() locks the looper, so holding
  // `lock_` across it only orders us against Detach().
  window_->PresentBitmap(bitmap, damage.x(), damage.y(), damage.width(),
                         damage.height());
}

void HaikuPresentTarget::Detach() {
  base::AutoLock guard(lock_);
  window_ = nullptr;
}

HaikuWindow::HaikuWindow(PlatformWindowDelegate* delegate,
                         HaikuWindowManager* manager,
                         const gfx::Rect& bounds,
                         bool has_frame)
    : delegate_(delegate),
      manager_(manager),
      bounds_(bounds),
      ui_task_runner_(base::SingleThreadTaskRunner::GetCurrentDefault()) {
  widget_ = manager_->AddWindow(this);

  bridge_ = std::make_unique<HaikuEventBridge>(GetWeakPtr(), ui_task_runner_);
  // A framed top-level window carries the R Chromium native toolbar.
  // RCH_NO_TOOLBAR=1 is what an installed web app's launcher sets: an app is
  // a window onto one site and has no address to type.
  window_ = haiku_shim::HaikuShimCreateWindow(
      bounds.x(), bounds.y(), bounds.width(), bounds.height(), has_frame,
      /*with_toolbar=*/has_frame && getenv("RCH_NO_TOOLBAR") == nullptr,
      bridge_.get());
  if (const char* app_name = AppName(); window_ && app_name) {
    window_->SetWindowTitle(app_name);
  }
  present_target_ = base::MakeRefCounted<HaikuPresentTarget>(window_);
  ToolbarWindowMap()[widget_] = this;

  delegate_->OnAcceleratedWidgetAvailable(widget_);
}

HaikuWindow::~HaikuWindow() {
  // No delegate calls from here: the delegate is the WindowTreeHostPlatform
  // that is deleting this window, and it destroyed its compositor first.
  // Calling OnAcceleratedWidgetDestroyed() then made it release the widget
  // from a compositor that was gone, and every window close ended in a SEGV.
  // X11 and Wayland do not call it from their destructors either.
  if (present_target_) {
    // Stop the compositor thread reaching the window before it is torn down.
    present_target_->Detach();
  }
  if (window_) {
    // Quits the window thread, which deletes the BWindow and its view. No
    // callback can reach the bridge after this returns.
    window_->DestroyWindow();
    window_ = nullptr;
  }
  bridge_.reset();
  ToolbarWindowMap().erase(widget_);
  manager_->RemoveWindow(widget_, this);
}

void HaikuWindow::Show(bool inactive) {
  if (visible_) {
    return;
  }
  visible_ = true;
  if (window_) {
    window_->ShowWindow(inactive);
  }
}

void HaikuWindow::Hide() {
  if (!visible_) {
    return;
  }
  visible_ = false;
  if (window_) {
    window_->HideWindow();
  }
}

void HaikuWindow::Close() {
  delegate_->OnClosed();
}

bool HaikuWindow::IsVisible() const {
  return visible_;
}

void HaikuWindow::PrepareForShutdown() {}

void HaikuWindow::SetBoundsInPixels(const gfx::Rect& bounds) {
  const bool origin_changed = bounds_.origin() != bounds.origin();
  bounds_ = bounds;
  if (window_) {
    window_->SetWindowBounds(bounds.x(), bounds.y(), bounds.width(),
                             bounds.height());
  }
  delegate_->OnBoundsChanged({origin_changed});
}

gfx::Rect HaikuWindow::GetBoundsInPixels() const {
  return bounds_;
}

void HaikuWindow::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}

gfx::Rect HaikuWindow::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}

void HaikuWindow::SetTitle(const std::u16string& title) {
  if (window_) {
    const char* app_name = AppName();
    window_->SetWindowTitle(app_name ? app_name
                                     : base::UTF16ToUTF8(title).c_str());
  }
}

void HaikuWindow::SetCapture() {
  // Haiku's event mask is set per-view from the window thread; the pointer
  // is already delivered to the view that took the button down, which
  // covers the drag case Chromium uses capture for.
  has_capture_ = true;
}

void HaikuWindow::ReleaseCapture() {
  has_capture_ = false;
}

bool HaikuWindow::HasCapture() const {
  return has_capture_;
}

void HaikuWindow::SetFullscreen(bool fullscreen, int64_t target_display_id) {
  DCHECK_EQ(target_display_id, display::kInvalidDisplayId);
  if (!delegate_->CanFullscreen()) {
    return;
  }
  if (fullscreen) {
    if (window_state_ != PlatformWindowState::kFullScreen) {
      restored_bounds_ = bounds_;
    }
    float frame[4];
    haiku_shim::HaikuShimScreenFrame(frame);
    SetBoundsInPixels(gfx::Rect(static_cast<int>(frame[0]),
                                static_cast<int>(frame[1]),
                                static_cast<int>(frame[2]),
                                static_cast<int>(frame[3])));
    UpdateWindowState(PlatformWindowState::kFullScreen);
  } else {
    if (window_state_ != PlatformWindowState::kFullScreen) {
      return;
    }
    if (restored_bounds_) {
      SetBoundsInPixels(*restored_bounds_);
      restored_bounds_.reset();
    }
    UpdateWindowState(PlatformWindowState::kNormal);
  }
}

void HaikuWindow::Maximize() {
  if (!delegate_->CanMaximize()) {
    return;
  }
  if (window_state_ == PlatformWindowState::kMaximized) {
    return;
  }
  restored_bounds_ = bounds_;
  if (window_) {
    window_->ZoomWindow();
    float frame[4];
    window_->GetWindowFrame(frame);
    bounds_ = gfx::Rect(static_cast<int>(frame[0]), static_cast<int>(frame[1]),
                        static_cast<int>(frame[2]), static_cast<int>(frame[3]));
  }
  UpdateWindowState(PlatformWindowState::kMaximized);
}

void HaikuWindow::Minimize() {
  if (window_state_ == PlatformWindowState::kMinimized) {
    return;
  }
  if (window_) {
    window_->MinimizeWindow(true);
  }
  UpdateWindowState(PlatformWindowState::kMinimized);
}

void HaikuWindow::Restore() {
  if (window_state_ == PlatformWindowState::kNormal) {
    return;
  }
  if (window_ && window_state_ == PlatformWindowState::kMinimized) {
    window_->MinimizeWindow(false);
  }
  if (restored_bounds_) {
    SetBoundsInPixels(*restored_bounds_);
    restored_bounds_.reset();
  }
  UpdateWindowState(PlatformWindowState::kNormal);
}

PlatformWindowState HaikuWindow::GetPlatformWindowState() const {
  return window_state_;
}

void HaikuWindow::Activate() {
  if (window_) {
    window_->ActivateWindow(true);
  }
}

void HaikuWindow::Deactivate() {
  if (window_) {
    window_->ActivateWindow(false);
  }
}

void HaikuWindow::SetUseNativeFrame(bool use_native_frame) {}

bool HaikuWindow::ShouldUseNativeFrame() const {
  return true;
}

void HaikuWindow::SetCursor(scoped_refptr<PlatformCursor> cursor) {}

void HaikuWindow::MoveCursorTo(const gfx::Point& location) {}

void HaikuWindow::ConfineCursorToBounds(const gfx::Rect& bounds) {}

void HaikuWindow::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}

gfx::Rect HaikuWindow::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}

void HaikuWindow::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                 const gfx::ImageSkia& app_icon) {}

void HaikuWindow::SizeConstraintsChanged() {}


void HaikuWindow::OnEventFromWindowThread(std::unique_ptr<Event> event) {
  delegate_->DispatchEvent(event.get());
}

void HaikuWindow::OnBoundsChangedFromWindowThread(const gfx::Rect& bounds) {
  const bool origin_changed = bounds_.origin() != bounds.origin();
  bounds_ = bounds;
  delegate_->OnBoundsChanged({origin_changed});
}

void HaikuWindow::OnSizeChangedFromWindowThread(const gfx::Size& size) {
  if (bounds_.size() == size) {
    return;
  }
  bounds_.set_size(size);
  delegate_->OnBoundsChanged({/*origin_changed=*/false});
}

void HaikuWindow::OnCloseRequestedFromWindowThread() {
  delegate_->OnCloseRequest();
}

void HaikuWindow::OnActivationChangedFromWindowThread(bool active) {
  delegate_->OnActivationChanged(active);
}

void HaikuWindow::UpdateWindowState(PlatformWindowState new_state) {
  if (window_state_ == new_state) {
    return;
  }
  const PlatformWindowState old_state = window_state_;
  window_state_ = new_state;
  delegate_->OnWindowStateChanged(old_state, new_state);
}

void HaikuWindow::OnToolbarBack() {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnNavigateBack();
  }
}

void HaikuWindow::OnToolbarForward() {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnNavigateForward();
  }
}

void HaikuWindow::OnToolbarReloadOrStop() {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnReloadOrStop();
  }
}

void HaikuWindow::OnToolbarNavigateToURL(std::string text) {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnNavigateToURL(text);
  }
}

void HaikuWindow::OnToolbarInstall() {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnInstall();
  }
}

void HaikuWindow::OnToolbarExtensionAction(std::string id,
                                           const gfx::Rect& anchor) {
  if (HaikuToolbarObserver* obs = GetHaikuToolbarObserver(widget_)) {
    obs->OnExtensionActionClicked(id, anchor);
  }
}

void HaikuWindow::SetToolbarAddress(const std::string& url) {
  if (window_) {
    window_->SetAddressText(url.c_str());
  }
}

void HaikuWindow::SetToolbarTitle(const std::string& title) {
  if (window_) {
    window_->SetPageTitleText(title.c_str());
    // content_shell's aura path never calls PlatformWindow::SetTitle, so the
    // BWindow tab would read "Chromium" forever; show the page title there.
    const char* app_name = AppName();
    window_->SetWindowTitle(app_name        ? app_name
                            : title.empty() ? "R Chromium"
                                            : title.c_str());
  }
}

void HaikuWindow::SetToolbarLoading(bool loading) {
  if (window_) {
    window_->SetLoadingState(loading);
  }
}

void HaikuWindow::SetToolbarNavigationEnabled(bool back, bool forward) {
  if (window_) {
    window_->SetNavigationEnabled(back, forward);
  }
}

void HaikuWindow::SetToolbarInstallable(bool installable,
                                        const std::string& app_name) {
  if (window_) {
    window_->SetInstallable(installable, app_name.c_str());
  }
}

void HaikuWindow::SetToolbarAction(const HaikuToolbarAction& action) {
  if (window_) {
    const bool has_icon =
        action.width > 0 && action.height > 0 &&
        action.argb.size() ==
            static_cast<size_t>(action.width) * static_cast<size_t>(action.height);
    window_->SetExtensionAction(
        action.id.c_str(),
        has_icon ? reinterpret_cast<const unsigned int*>(action.argb.data())
                 : nullptr,
        has_icon ? action.width : 0, has_icon ? action.height : 0,
        action.badge_text.c_str(), action.badge_background,
        action.badge_text_color, action.tooltip.c_str(), action.enabled);
  }
}

void HaikuWindow::RemoveToolbarAction(const std::string& id) {
  if (window_) {
    window_->RemoveExtensionAction(id.c_str());
  }
}

// Toolbar-bridge state-push entry points (declared in haiku_toolbar_bridge.h).
// The window lookup lives here because this is where the widget->window map is.
void HaikuToolbarSetAddress(gfx::AcceleratedWidget widget,
                            const std::string& url) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarAddress(url);
  }
}

void HaikuToolbarSetLoading(gfx::AcceleratedWidget widget, bool loading) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarLoading(loading);
  }
}

void HaikuToolbarSetNavigationEnabled(gfx::AcceleratedWidget widget,
                                      bool back,
                                      bool forward) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarNavigationEnabled(back, forward);
  }
}

void HaikuToolbarSetTitle(gfx::AcceleratedWidget widget,
                          const std::string& title) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarTitle(title);
  }
}

void HaikuToolbarSetInstallable(gfx::AcceleratedWidget widget,
                                bool installable,
                                const std::string& app_name) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarInstallable(installable, app_name);
  }
}

void HaikuToolbarSetAction(gfx::AcceleratedWidget widget,
                           const HaikuToolbarAction& action) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->SetToolbarAction(action);
  }
}

void HaikuToolbarRemoveAction(gfx::AcceleratedWidget widget,
                              const std::string& id) {
  auto it = ToolbarWindowMap().find(widget);
  if (it != ToolbarWindowMap().end()) {
    it->second->RemoveToolbarAction(id);
  }
}

bool HaikuSetFileIcon(const std::string& path,
                      const uint32_t* argb,
                      int width,
                      int height) {
  return haiku_shim::HaikuShimSetFileIcon(path.c_str(),
                              reinterpret_cast<const unsigned int*>(argb),
                              width, height);
}

}  // namespace ui
