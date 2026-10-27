// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_BEAPI_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_BEAPI_H_

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/ozone/platform/haiku/haiku_shim.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace ui {

class HaikuWindow;

// Takes the BeAPI callbacks the shim delivers on the BWindow's own thread,
// turns them into ui::Events, and posts them to the HaikuWindow on Chromium's
// UI thread.
//
// Owned by the HaikuWindow. The window destroys the native window first, and
// the shim guarantees no callback arrives after that returns, so the bridge
// is safe to drop immediately afterwards.
class HaikuEventBridge : public haiku_shim::Delegate {
 public:
  HaikuEventBridge(base::WeakPtr<HaikuWindow> window,
                   scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);

  HaikuEventBridge(const HaikuEventBridge&) = delete;
  HaikuEventBridge& operator=(const HaikuEventBridge&) = delete;

  // Virtual only to satisfy -Wdelete-non-virtual-dtor: the class is
  // polymorphic, and haiku_shim::Delegate deliberately has no virtual
  // destructor because the shim never owns a delegate.
  virtual ~HaikuEventBridge();

  // haiku_shim::Delegate:
  void OnMouseDown(float x,
                   float y,
                   unsigned int modifiers,
                   int buttons,
                   int clicks) override;
  void OnMouseUp(float x,
                 float y,
                 unsigned int modifiers,
                 int buttons) override;
  void OnMouseMoved(float x,
                    float y,
                    unsigned int modifiers,
                    int buttons,
                    unsigned int transit) override;
  void OnKey(bool pressed,
             const char* bytes,
             int num_bytes,
             unsigned int modifiers) override;
  void OnViewResized(float width, float height) override;
  void OnQuitRequested() override;
  void OnActivated(bool active) override;
  void OnFrameMoved(float x, float y, float width, float height) override;
  // R Chromium native toolbar events, forwarded to the UI thread.
  void OnNavigateBack() override;
  void OnNavigateForward() override;
  void OnReloadOrStop() override;
  void OnNavigateToURL(const char* utf8) override;
  void OnInstall() override;
  void OnExtensionActionClicked(const char* id,
                                float x,
                                float y,
                                float width,
                                float height) override;
  void OnMouseWheel(float x,
                    float y,
                    unsigned int modifiers,
                    float delta_x,
                    float delta_y) override;

 private:
  base::WeakPtr<HaikuWindow> window_;
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_BEAPI_H_
