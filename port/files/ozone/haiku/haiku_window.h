// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"

#include "ui/ozone/platform/haiku/haiku_beapi.h"

class BBitmap;

namespace ui {

class HaikuWindowManager;

// What the compositor thread is allowed to hold on to.
//
// Presenting a frame crosses threads by design: viz composites in software on
// its own thread and hands the finished bitmap straight to the BWindow, whose
// PresentBitmap() takes the looper lock and is safe from anywhere. What was
// not safe was the handle. The surface held a base::WeakPtr<HaikuWindow>, and
// a WeakPtr belongs to the sequence that made it -- the UI thread -- so every
// frame dereferenced it from the wrong one. The dcheck_always_on build of
// 2026-09-20 stopped on the first frame with "base/sequence_checker.cc:21
// DCHECK failed: checker.CalledOnValidSequence" underneath
// HaikuCanvasSurface::PresentCanvas(), and in a release build the same code
// read HaikuWindow::window_ with no synchronisation at all while the UI
// thread could be in DestroyWindow().
//
// This is the handle instead: reference counted so the compositor cannot
// outlive it, and holding the one pointer it needs under a lock the UI thread
// takes once, in Detach(), before the window goes away.
class HaikuPresentTarget : public base::RefCountedThreadSafe<HaikuPresentTarget> {
 public:
  explicit HaikuPresentTarget(haiku_shim::NativeWindow* window);

  HaikuPresentTarget(const HaikuPresentTarget&) = delete;
  HaikuPresentTarget& operator=(const HaikuPresentTarget&) = delete;

  // Any thread. Takes ownership of `bitmap` whether or not it is drawn, which
  // is what the shim's PresentBitmap() promises.
  void Present(BBitmap* bitmap, const gfx::Rect& damage);

  // UI thread, before the BWindow is destroyed. After this every Present()
  // drops its bitmap instead of touching a dead window.
  void Detach();

 private:
  friend class base::RefCountedThreadSafe<HaikuPresentTarget>;
  ~HaikuPresentTarget();

  base::Lock lock_;
  raw_ptr<haiku_shim::NativeWindow> window_ GUARDED_BY(lock_);
};

// A PlatformWindow backed by a real BWindow.
//
// Threading is the whole story here. Haiku gives every BWindow its own
// thread, and BView hooks (Draw, MouseDown, KeyDown) run on that thread --
// never on Chromium's UI thread. So the view forwards input by posting to
// the task runner captured when the window was built, and everything that
// touches Chromium state stays on the UI thread. In the other direction,
// calls into the BWindow from Chromium threads take the window's lock.
class HaikuWindow : public PlatformWindow {
 public:
  HaikuWindow(PlatformWindowDelegate* delegate,
              HaikuWindowManager* manager,
              const gfx::Rect& bounds,
              bool has_frame);

  HaikuWindow(const HaikuWindow&) = delete;
  HaikuWindow& operator=(const HaikuWindow&) = delete;

  ~HaikuWindow() override;

  // PlatformWindow:
  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  bool HasCapture() const override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

  gfx::AcceleratedWidget widget() const { return widget_; }

  // The handle the surface presents through. Safe to call from the
  // compositor thread; see HaikuPresentTarget above.
  scoped_refptr<HaikuPresentTarget> present_target() const {
    return present_target_;
  }

  // Called on the UI thread, posted from the window thread.
  void OnEventFromWindowThread(std::unique_ptr<Event> event);
  void OnBoundsChangedFromWindowThread(const gfx::Rect& bounds);
  void OnSizeChangedFromWindowThread(const gfx::Size& size);
  void OnCloseRequestedFromWindowThread();
  void OnActivationChangedFromWindowThread(bool active);

  // R Chromium native toolbar. The first group runs on the UI thread
  // (posted by the bridge) and drives navigation through the registered
  // HaikuToolbarObserver; the second is called from the delegate on the
  // UI thread and pushes state down into the shim toolbar.
  void OnToolbarBack();
  void OnToolbarForward();
  void OnToolbarReloadOrStop();
  void OnToolbarNavigateToURL(std::string text);
  void OnToolbarInstall();
  void OnToolbarExtensionAction(std::string id, const gfx::Rect& anchor);
  void SetToolbarAddress(const std::string& url);
  void SetToolbarTitle(const std::string& title);
  void SetToolbarLoading(bool loading);
  void SetToolbarNavigationEnabled(bool back, bool forward);
  void SetToolbarInstallable(bool installable, const std::string& app_name);
  void SetToolbarAction(const HaikuToolbarAction& action);
  void RemoveToolbarAction(const std::string& id);

  base::WeakPtr<HaikuWindow> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner() const {
    return ui_task_runner_;
  }

 private:
  void UpdateWindowState(PlatformWindowState new_state);

  raw_ptr<PlatformWindowDelegate> delegate_;
  raw_ptr<HaikuWindowManager> manager_;
  gfx::Rect bounds_;
  gfx::AcceleratedWidget widget_;

  // The bridge has to outlive the native window, which calls into it from
  // its own thread, so it is declared first and destroyed last.
  std::unique_ptr<HaikuEventBridge> bridge_;

  // Owned by the shim: DestroyWindow() quits the BWindow thread, which
  // deletes both. Raw, and cleared in the destructor.
  raw_ptr<haiku_shim::NativeWindow> window_ = nullptr;

  // Handed to every canvas surface built for this window.
  scoped_refptr<HaikuPresentTarget> present_target_;

  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;

  bool visible_ = false;
  bool has_capture_ = false;
  std::optional<gfx::Rect> restored_bounds_;
  PlatformWindowState window_state_ = PlatformWindowState::kUnknown;

  base::WeakPtrFactory<HaikuWindow> weak_ptr_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_WINDOW_H_
