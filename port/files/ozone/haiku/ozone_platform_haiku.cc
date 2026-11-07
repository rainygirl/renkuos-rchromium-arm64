// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/ozone_platform_haiku.h"

#include <memory>

#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/platform/haiku/haiku_app.h"
#include "ui/ozone/platform/haiku/haiku_input_method.h"
#include "ui/ozone/platform/haiku/haiku_screen.h"
#include "ui/ozone/platform/haiku/haiku_surface_factory.h"
#include "ui/ozone/platform/haiku/haiku_window.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {

namespace {

// Haiku delivers input through each BWindow's own thread rather than a single
// queue, so there is no source to pump here. The instance exists because
// callers expect PlatformEventSource::GetInstance() to be non-null.
class HaikuPlatformEventSource : public PlatformEventSource {
 public:
  HaikuPlatformEventSource() = default;

  HaikuPlatformEventSource(const HaikuPlatformEventSource&) = delete;
  HaikuPlatformEventSource& operator=(const HaikuPlatformEventSource&) = delete;

  ~HaikuPlatformEventSource() override = default;
};

class OzonePlatformHaiku : public OzonePlatform {
 public:
  OzonePlatformHaiku() = default;

  OzonePlatformHaiku(const OzonePlatformHaiku&) = delete;
  OzonePlatformHaiku& operator=(const OzonePlatformHaiku&) = delete;

  ~OzonePlatformHaiku() override = default;

  // OzonePlatform:
  ui::SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    return surface_factory_.get();
  }
  OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }
  CursorFactory* GetCursorFactory() override { return cursor_factory_.get(); }
  InputController* GetInputController() override {
    return input_controller_.get();
  }
  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }
  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    return std::make_unique<HaikuWindow>(delegate, window_manager_.get(),
                                         properties.bounds,
                                         !properties.remove_standard_frame);
  }
  bool IsWindowCompositingSupported() const override { return false; }
  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }
  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<HaikuScreen>(window_manager_.get());
  }
  void InitScreen(PlatformScreen* screen) override {}
  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodHaiku>(ime_key_event_dispatcher,
                                              widget);
  }

  bool InitializeUI(const InitParams& params) override {
    // Must come first: BWindow's constructor requires be_app.
    HaikuApp::EnsureStarted();

    window_manager_ = std::make_unique<HaikuWindowManager>();
    surface_factory_ =
        std::make_unique<HaikuSurfaceFactory>(window_manager_.get());

    if (!PlatformEventSource::GetInstance()) {
      platform_event_source_ = std::make_unique<HaikuPlatformEventSource>();
    }
    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        keyboard_layout_engine_.get());

    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    gpu_platform_support_host_.reset(CreateStubGpuPlatformSupportHost());

    return true;
  }

  void InitializeGPU(const InitParams& params) override {
    // The GPU process does not talk to app_server: rasterisation happens
    // here, and the browser process is what hands pixels to a window. So it
    // needs a surface factory but no window manager.
    if (!surface_factory_) {
      surface_factory_ = std::make_unique<HaikuSurfaceFactory>(nullptr);
    }
  }

 private:
  std::unique_ptr<KeyboardLayoutEngine> keyboard_layout_engine_;
  std::unique_ptr<HaikuWindowManager> window_manager_;
  std::unique_ptr<HaikuSurfaceFactory> surface_factory_;
  std::unique_ptr<PlatformEventSource> platform_event_source_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_platform_support_host_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformHaiku() {
  return new OzonePlatformHaiku;
}

}  // namespace ui
