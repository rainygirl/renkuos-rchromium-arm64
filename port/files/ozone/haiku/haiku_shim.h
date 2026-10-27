// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_SHIM_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_SHIM_H_

// The boundary between Chromium and the BeAPI.
//
// Nothing on the Chromium side of this header may derive from a BeAPI class.
// Chromium carries its own C++ runtime (libc++), and libbe is built against
// the system one (libstdc++). The two agree on how a type_info object is laid
// out but not on the vtable its pointer names, so when libbe runs
// dynamic_cast -- BWindow::_DetermineTarget() does it for every message a
// window dispatches -- it reads a Chromium subclass's type_info through
// libstdc++'s vtable and calls the wrong virtual slot. The process dies in
// __dynamic_cast before it draws anything.
//
// So the subclasses live in libchromium_haiku.so, which is built with the
// same gcc that built libbe and therefore agrees with it about RTTI. This
// header is all the two sides share: plain virtual interfaces, which both
// compilers lay out identically, and no RTTI or exceptions across the line.

class BBitmap;

namespace haiku_shim {

// Implemented by Chromium, called by the shim. Every method runs on the
// BWindow's own thread, never on Chromium's UI thread.
class Delegate {
 public:
  // `modifiers` and `buttons` are the BeAPI values, untranslated: the
  // mapping to Chromium's event flags needs ui/events, which this side of
  // the boundary cannot see.
  virtual void OnMouseDown(float x,
                           float y,
                           unsigned int modifiers,
                           int buttons,
                           int clicks) = 0;
  virtual void OnMouseUp(float x,
                         float y,
                         unsigned int modifiers,
                         int buttons) = 0;
  virtual void OnMouseMoved(float x,
                            float y,
                            unsigned int modifiers,
                            int buttons,
                            unsigned int transit) = 0;
  virtual void OnKey(bool pressed,
                     const char* bytes,
                     int num_bytes,
                     unsigned int modifiers) = 0;
  virtual void OnViewResized(float width, float height) = 0;
  virtual void OnQuitRequested() = 0;
  virtual void OnActivated(bool active) = 0;
  virtual void OnFrameMoved(float x, float y, float width, float height) = 0;

  // R Chromium native toolbar events. Fired on the BWindow's own thread,
  // like the input callbacks above. Non-pure with empty defaults so a
  // delegate that does not draw a toolbar (the plain ozone event bridge)
  // need not implement them.
  virtual void OnNavigateBack() {}
  virtual void OnNavigateForward() {}
  // Reload when idle, Stop when loading; the shim tells them apart from the
  // loading state last pushed through NativeWindow::SetLoadingState.
  virtual void OnReloadOrStop() {}
  virtual void OnNavigateToURL(const char* utf8) {}
  // The install button, which only exists while the page has a usable
  // web app manifest. See HaikuShimWindow::SetInstallable().
  virtual void OnInstall() {}

  // The mouse wheel, and two-finger scrolling on a touchpad or through QEMU's
  // tablet. The BeAPI deltas, untranslated: notches, positive meaning down
  // (or right), fractional from smooth-scrolling devices. Appended after the
  // toolbar callbacks and non-pure, so the vtable slots above keep their
  // places; the shim and Chromium still have to be rebuilt together.
  virtual void OnMouseWheel(float x,
                            float y,
                            unsigned int modifiers,
                            float delta_x,
                            float delta_y) {}

  // An extension's toolbar button was clicked. `x`, `y`, `width`, `height`
  // are its frame in screen coordinates, to anchor the extension's popup
  // under it. Appended after the callbacks above, like OnMouseWheel.
  virtual void OnExtensionActionClicked(const char* id,
                                        float x,
                                        float y,
                                        float width,
                                        float height) {}

 protected:
  // Not virtual on purpose: the shim never owns or destroys a delegate.
  ~Delegate() {}
};

// Implemented by the shim, called by Chromium from any thread. Each method
// takes the window's lock itself. The names carry a Window suffix so that
// none of them accidentally overrides a BWindow virtual of the same shape in
// the implementation, which inherits from both.
class NativeWindow {
 public:
  virtual void ShowWindow(bool inactive) = 0;
  virtual void HideWindow() = 0;
  virtual void SetWindowBounds(float x, float y, float width, float height) = 0;
  // Writes x, y, width, height.
  virtual void GetWindowFrame(float out_xywh[4]) = 0;
  virtual void SetWindowTitle(const char* utf8) = 0;
  virtual void ZoomWindow() = 0;
  virtual void MinimizeWindow(bool minimize) = 0;
  virtual void ActivateWindow(bool active) = 0;
  // Takes ownership of `bitmap` and drops the one the view held.
  virtual void PresentBitmap(BBitmap* bitmap,
                       float x,
                       float y,
                       float width,
                       float height) = 0;
  // R Chromium native toolbar state, pushed from Chromium's UI thread. Each
  // takes the window lock itself. No-ops when the window was created without
  // a toolbar.
  virtual void SetAddressText(const char* utf8) = 0;
  // The page title, filed by the star button when a page is bookmarked.
  virtual void SetPageTitleText(const char* utf8) = 0;
  virtual void SetLoadingState(bool loading) = 0;
  virtual void SetNavigationEnabled(bool back, bool forward) = 0;
  // Show or hide the install button. `app_name` is for its tooltip.
  virtual void SetInstallable(bool installable, const char* app_name) = 0;

  // Quits the window thread, which deletes the BWindow and this object with
  // it. No delegate call can arrive after this returns.
  virtual void DestroyWindow() = 0;

  // Extension toolbar buttons, appended after DestroyWindow so the slots
  // above keep their places. Adds the button for extension `id`, or updates
  // it: `argb` is width*height non-premultiplied 0xAARRGGBB pixels (null for
  // a blank button), `badge_text` empty for no badge. No-ops without a
  // toolbar.
  virtual void SetExtensionAction(const char* id,
                                  const unsigned int* argb,
                                  int width,
                                  int height,
                                  const char* badge_text,
                                  unsigned int badge_background_argb,
                                  unsigned int badge_text_argb,
                                  const char* tooltip,
                                  bool enabled) = 0;
  virtual void RemoveExtensionAction(const char* id) = 0;

 protected:
  ~NativeWindow() {}
};

extern "C" {

// Starts the BApplication if it is not running, and returns once be_app is
// set. Safe to call more than once. BWindow's constructor requires be_app, so
// this has to happen before any window is built.
void HaikuShimEnsureApp();

// Builds the window and its view and leaves the window hidden and running on
// its own thread.
NativeWindow* HaikuShimCreateWindow(float x,
                              float y,
                              float width,
                              float height,
                              bool has_frame,
                              bool with_toolbar,
                              Delegate* delegate);

// The main screen's frame, as x, y, width, height.
void HaikuShimScreenFrame(float out_xywh[4]);

// Write a file's Haiku icon attributes from a 32-bit ARGB image, so an
// installed web app's launcher carries the site's own icon. `argb` is
// width*height non-premultiplied 0xAARRGGBB pixels -- SkColor's layout --
// which is how the caller avoids naming a BeAPI type and this file avoids
// naming a Skia one. Scaled here to 32x32 and 16x16.
bool HaikuShimSetFileIcon(const char* path,
                          const unsigned int* argb,
                          int width,
                          int height);

}  // extern "C"

}  // namespace haiku_shim

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_SHIM_H_
