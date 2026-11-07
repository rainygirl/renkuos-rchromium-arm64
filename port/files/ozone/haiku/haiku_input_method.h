// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_HAIKU_HAIKU_INPUT_METHOD_H_
#define UI_OZONE_PLATFORM_HAIKU_HAIKU_INPUT_METHOD_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/base/ime/input_method_base.h"
#include "ui/gfx/native_ui_types.h"

namespace ui {

// What a Haiku input method sent the content view (B_INPUT_METHOD_EVENT),
// carried from the window thread to the UI thread.
struct HaikuImeEvent {
  int opcode = 0;  // B_INPUT_METHOD_STARTED, _CHANGED or _STOPPED
  std::u16string text;
  bool confirmed = false;
  // -1: the method does not say which changes come from keys, so every
  // unconfirmed change counts as one. 1: the first change a key caused
  // (koreanime marks it). 0: a later change from the same key, or none.
  int key_mark = -1;
};

// Turns Haiku input method events into a composition on the focused text
// input client, so that pages get compositionstart/update/end and keydown
// with key "Process" (keyCode 229) and isComposing -- the way they do on
// other systems. Keys the input method lets through are handled as
// InputMethodMinimal handles them.
class InputMethodHaiku : public InputMethodBase {
 public:
  InputMethodHaiku(ImeKeyEventDispatcher* ime_key_event_dispatcher,
                   gfx::AcceleratedWidget widget);
  InputMethodHaiku(const InputMethodHaiku&) = delete;
  InputMethodHaiku& operator=(const InputMethodHaiku&) = delete;
  ~InputMethodHaiku() override;

  // The input method of `widget`'s window, or null. UI thread only.
  static InputMethodHaiku* ForWidget(gfx::AcceleratedWidget widget);

  // `cancel` tells the Haiku input method to drop its composition; kept for
  // when Chromium ends one itself (a click, a focus change).
  void OnHaikuImeEvent(const HaikuImeEvent& event,
                       base::RepeatingClosure cancel);

  // InputMethod:
  ui::EventDispatchDetails DispatchKeyEvent(ui::KeyEvent* event) override;
  void OnCaretBoundsChanged(const TextInputClient* client) override;
  void CancelComposition(const TextInputClient* client) override;
  bool IsCandidatePopupOpen() const override;

 protected:
  // InputMethodBase:
  void OnWillChangeFocusedClient(TextInputClient* focused_before,
                                 TextInputClient* focused) override;

 private:
  void DispatchProcessKey();
  void StopHaikuComposition();

  gfx::AcceleratedWidget widget_;
  bool composing_ = false;
  base::RepeatingClosure cancel_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_HAIKU_HAIKU_INPUT_METHOD_H_
