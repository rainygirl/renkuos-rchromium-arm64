// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/haiku_input_method.h"

#include <Input.h>

#include <map>
#include <tuple>

#include "base/no_destructor.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/ime_text_span.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/range/range.h"

namespace ui {

namespace {

// One input method per window, found by the window's widget when an event
// arrives. Touched only on the UI thread.
std::map<gfx::AcceleratedWidget, InputMethodHaiku*>& Registry() {
  static base::NoDestructor<std::map<gfx::AcceleratedWidget, InputMethodHaiku*>>
      registry;
  return *registry;
}

}  // namespace

InputMethodHaiku::InputMethodHaiku(
    ImeKeyEventDispatcher* ime_key_event_dispatcher,
    gfx::AcceleratedWidget widget)
    : InputMethodBase(ime_key_event_dispatcher), widget_(widget) {
  Registry()[widget_] = this;
}

InputMethodHaiku::~InputMethodHaiku() {
  auto it = Registry().find(widget_);
  if (it != Registry().end() && it->second == this) {
    Registry().erase(it);
  }
}

// static
InputMethodHaiku* InputMethodHaiku::ForWidget(gfx::AcceleratedWidget widget) {
  auto it = Registry().find(widget);
  return it == Registry().end() ? nullptr : it->second;
}

void InputMethodHaiku::DispatchProcessKey() {
  // What other systems send for a key an input method has taken: keydown
  // with key "Process" and keyCode 229, isComposing while a composition is
  // open. Dispatched before the composition changes, as there.
  KeyEvent event(EventType::kKeyPressed, VKEY_PROCESSKEY, DomCode::NONE,
                 EF_NONE, DomKey::PROCESS, EventTimeForNow());
  std::ignore = DispatchKeyEventPostIME(&event);
}

void InputMethodHaiku::OnHaikuImeEvent(const HaikuImeEvent& event,
                                       base::RepeatingClosure cancel) {
  cancel_ = std::move(cancel);

  switch (event.opcode) {
    case B_INPUT_METHOD_STARTED:
      return;

    case B_INPUT_METHOD_CHANGED: {
      if (event.key_mark == 1 || (event.key_mark < 0 && !event.confirmed)) {
        DispatchProcessKey();
      }
      // The keydown may have moved focus; look the client up afterwards.
      TextInputClient* client = GetTextInputClient();
      if (client == nullptr || IsTextInputTypeNone()) {
        composing_ = false;
        return;
      }
      if (event.confirmed) {
        if (event.text.empty()) {
          if (composing_) {
            client->ClearCompositionText();
          }
        } else {
          client->InsertText(
              event.text,
              TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
        }
        composing_ = false;
        return;
      }
      CompositionText composition;
      composition.text = event.text;
      composition.selection = gfx::Range(event.text.length());
      composition.ime_text_spans.push_back(
          ImeTextSpan(ImeTextSpan::Type::kComposition, 0, event.text.length(),
                      ImeTextSpan::Thickness::kThin,
                      ImeTextSpan::UnderlineStyle::kSolid,
                      SK_ColorTRANSPARENT));
      client->SetCompositionText(composition);
      composing_ = true;
      return;
    }

    case B_INPUT_METHOD_STOPPED: {
      TextInputClient* client = GetTextInputClient();
      if (composing_ && client != nullptr && client->HasCompositionText()) {
        client->ConfirmCompositionText(/*keep_selection=*/false);
      }
      composing_ = false;
      return;
    }

    default:
      return;
  }
}

void InputMethodHaiku::StopHaikuComposition() {
  if (!composing_) {
    return;
  }
  composing_ = false;
  if (!cancel_.is_null()) {
    cancel_.Run();
  }
}

ui::EventDispatchDetails InputMethodHaiku::DispatchKeyEvent(
    ui::KeyEvent* event) {
  DCHECK(event->type() == EventType::kKeyPressed ||
         event->type() == EventType::kKeyReleased);

  if (!GetTextInputClient()) {
    return DispatchKeyEventPostIME(event);
  }

  ui::EventDispatchDetails dispatch_details = DispatchKeyEventPostIME(event);
  if (!event->stopped_propagation() && !dispatch_details.dispatcher_destroyed &&
      event->type() == EventType::kKeyPressed && GetTextInputClient()) {
    const uint16_t ch = event->GetCharacter();
    if (ch) {
      GetTextInputClient()->InsertChar(*event);
      event->StopPropagation();
    }
  }
  return dispatch_details;
}

void InputMethodHaiku::OnCaretBoundsChanged(const TextInputClient* client) {}

void InputMethodHaiku::CancelComposition(const TextInputClient* client) {
  if (IsTextInputClientFocused(client)) {
    StopHaikuComposition();
  }
}

bool InputMethodHaiku::IsCandidatePopupOpen() const {
  return false;
}

void InputMethodHaiku::OnWillChangeFocusedClient(
    TextInputClient* focused_before,
    TextInputClient* focused) {
  StopHaikuComposition();
}

}  // namespace ui
