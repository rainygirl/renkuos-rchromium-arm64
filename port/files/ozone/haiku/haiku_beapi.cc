// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/haiku_beapi.h"

// The BeAPI constants the translation below names: modifier keys and
// B_*_MOUSE_BUTTON from InterfaceDefs.h, B_ENTERED_VIEW and
// B_EXITED_VIEW from View.h. Nothing here derives from a BeAPI class;
// see haiku_shim.h.
#include <InterfaceDefs.h>
#include <View.h>

#include <memory>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/ozone/platform/haiku/haiku_window.h"

namespace ui {

namespace {

int EventFlagsFromModifiers(unsigned int mods) {
  int flags = EF_NONE;
  if (mods & B_SHIFT_KEY) {
    flags |= EF_SHIFT_DOWN;
  }
  if (mods & B_CONTROL_KEY) {
    flags |= EF_CONTROL_DOWN;
  }
  // Haiku's Command key sits where Alt does on a PC keyboard and is what
  // users press for shortcuts, so it maps to Control's role in Chromium
  // only on menus; here it stays Alt, matching what the key produces.
  if (mods & B_COMMAND_KEY) {
    flags |= EF_ALT_DOWN;
  }
  if (mods & B_OPTION_KEY) {
    flags |= EF_ALTGR_DOWN;
  }
  if (mods & B_CAPS_LOCK) {
    flags |= EF_CAPS_LOCK_ON;
  }
  return flags;
}

int EventFlagsFromButtons(int buttons) {
  int flags = EF_NONE;
  if (buttons & B_PRIMARY_MOUSE_BUTTON) {
    flags |= EF_LEFT_MOUSE_BUTTON;
  }
  if (buttons & B_SECONDARY_MOUSE_BUTTON) {
    flags |= EF_RIGHT_MOUSE_BUTTON;
  }
  if (buttons & B_TERTIARY_MOUSE_BUTTON) {
    flags |= EF_MIDDLE_MOUSE_BUTTON;
  }
  return flags;
}

// Maps the B_* named keys Haiku reports in bytes[0] onto Chromium key codes.
KeyboardCode KeyboardCodeFromByte(char c) {
  switch (c) {
    case B_BACKSPACE:
      return VKEY_BACK;
    case B_RETURN:
      return VKEY_RETURN;
    case B_TAB:
      return VKEY_TAB;
    case B_ESCAPE:
      return VKEY_ESCAPE;
    case B_SPACE:
      return VKEY_SPACE;
    case B_LEFT_ARROW:
      return VKEY_LEFT;
    case B_RIGHT_ARROW:
      return VKEY_RIGHT;
    case B_UP_ARROW:
      return VKEY_UP;
    case B_DOWN_ARROW:
      return VKEY_DOWN;
    case B_INSERT:
      return VKEY_INSERT;
    case B_DELETE:
      return VKEY_DELETE;
    case B_HOME:
      return VKEY_HOME;
    case B_END:
      return VKEY_END;
    case B_PAGE_UP:
      return VKEY_PRIOR;
    case B_PAGE_DOWN:
      return VKEY_NEXT;
    default:
      break;
  }
  if (c >= 'a' && c <= 'z') {
    return static_cast<KeyboardCode>(VKEY_A + (c - 'a'));
  }
  if (c >= 'A' && c <= 'Z') {
    return static_cast<KeyboardCode>(VKEY_A + (c - 'A'));
  }
  if (c >= '0' && c <= '9') {
    return static_cast<KeyboardCode>(VKEY_0 + (c - '0'));
  }
  return VKEY_UNKNOWN;
}

// The first code point of what the key produced, or 0 for the B_* named keys.
// Haiku hands KeyDown the characters the keymap generated, already shifted, as
// UTF-8; the named keys arrive as single control bytes, which belong to
// KeyboardCodeFromByte instead.
char32_t CharacterFromBytes(const char* bytes, int num_bytes) {
  if (bytes == nullptr || num_bytes < 1) {
    return 0;
  }
  size_t index = 0;
  base_icu::UChar32 code_point = 0;
  if (!base::ReadUnicodeCharacter(
          std::string_view(bytes, static_cast<size_t>(num_bytes)), &index,
          &code_point)) {
    return 0;
  }
  if (code_point < 0x20 || code_point == 0x7f) {
    return 0;
  }
  return static_cast<char32_t>(code_point);
}

}  // namespace

HaikuEventBridge::HaikuEventBridge(
    base::WeakPtr<HaikuWindow> window,
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner)
    : window_(std::move(window)),
      ui_task_runner_(std::move(ui_task_runner)) {}

HaikuEventBridge::~HaikuEventBridge() = default;

void HaikuEventBridge::OnMouseDown(float x,
                                   float y,
                                   unsigned int modifiers,
                                   int buttons,
                                   int clicks) {
  const int button_flags = EventFlagsFromButtons(buttons);
  int flags = EventFlagsFromModifiers(modifiers) | button_flags;
  // The click count rides in `flags`, where MouseEvent::GetClickCount() reads
  // it. The changed-button flags may hold mouse-button bits only:
  // MouseEvent's constructor DCHECKs that, and passing `flags` there made
  // every double-click abort the browser.
  if (clicks == 2) {
    flags |= EF_IS_DOUBLE_CLICK;
  } else if (clicks >= 3) {
    flags |= EF_IS_TRIPLE_CLICK;
  }
  gfx::PointF location(x, y);
  auto event = std::make_unique<MouseEvent>(EventType::kMousePressed, location,
                                            location, EventTimeForNow(), flags,
                                            button_flags);
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnEventFromWindowThread, window_,
                                std::move(event)));
}

void HaikuEventBridge::OnMouseUp(float x,
                                 float y,
                                 unsigned int modifiers,
                                 int buttons) {
  // On release Haiku reports the buttons still held, which is empty for a
  // simple click, so name the button from what the message carried.
  int changed = EventFlagsFromButtons(buttons);
  if (changed == EF_NONE) {
    changed = EF_LEFT_MOUSE_BUTTON;
  }
  const int flags = EventFlagsFromModifiers(modifiers) | changed;
  gfx::PointF location(x, y);
  auto event = std::make_unique<MouseEvent>(EventType::kMouseReleased, location,
                                            location, EventTimeForNow(), flags,
                                            changed);
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnEventFromWindowThread, window_,
                                std::move(event)));
}

void HaikuEventBridge::OnMouseMoved(float x,
                                    float y,
                                    unsigned int modifiers,
                                    int buttons,
                                    unsigned int transit) {
  const int flags =
      EventFlagsFromModifiers(modifiers) | EventFlagsFromButtons(buttons);
  EventType type =
      buttons ? EventType::kMouseDragged : EventType::kMouseMoved;
  if (transit == B_EXITED_VIEW) {
    type = EventType::kMouseExited;
  } else if (transit == B_ENTERED_VIEW) {
    type = EventType::kMouseEntered;
  }
  gfx::PointF location(x, y);
  auto event = std::make_unique<MouseEvent>(type, location, location,
                                            EventTimeForNow(), flags, EF_NONE);
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnEventFromWindowThread, window_,
                                std::move(event)));
}

void HaikuEventBridge::OnMouseWheel(float x,
                                    float y,
                                    unsigned int modifiers,
                                    float delta_x,
                                    float delta_y) {
  // Haiku counts notches with positive meaning down/right; Chromium's offset
  // is in kWheelDelta units per notch with positive meaning up/left.
  const gfx::Vector2d offset(
      static_cast<int>(-delta_x * MouseWheelEvent::kWheelDelta),
      static_cast<int>(-delta_y * MouseWheelEvent::kWheelDelta));
  if (offset.IsZero()) {
    return;
  }
  const int flags = EventFlagsFromModifiers(modifiers);
  gfx::Point location(static_cast<int>(x), static_cast<int>(y));
  auto event = std::make_unique<MouseWheelEvent>(
      offset, location, location, EventTimeForNow(), flags, EF_NONE);
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnEventFromWindowThread, window_,
                                std::move(event)));
}

void HaikuEventBridge::OnKey(bool pressed,
                             const char* bytes,
                             int num_bytes,
                             unsigned int modifiers) {
  if (num_bytes < 1) {
    return;
  }
  const int flags = EventFlagsFromModifiers(modifiers);
  KeyboardCode code = KeyboardCodeFromByte(bytes[0]);
  DomCode dom_code = DomCode::NONE;
  DomKey dom_key = DomKey::NONE;

  // Name the character on the event. Left unset, the DomKey is derived from
  // the key code through the stub US layout, which can only answer for
  // letters, digits and the shifted digits -- every punctuation key then
  // arrived as VKEY_UNKNOWN with no character and nothing was inserted, so an
  // email address or a password could not be typed. Chromium wants the
  // US-layout code and key code that would have produced the character, and
  // Haiku has already applied the keymap, so map back through that layout.
  // Skipped while a shortcut modifier is held: Alt+C must not also type a "c".
  const char32_t character =
      (flags & (EF_CONTROL_DOWN | EF_ALT_DOWN | EF_COMMAND_DOWN)) != 0
          ? 0
          : CharacterFromBytes(bytes, num_bytes);
  if (character != 0) {
    dom_key = DomKey::FromCharacter(character);
    dom_code = UsLayoutDomKeyToDomCode(dom_key);
    if (code == VKEY_UNKNOWN) {
      code = DomCodeToUsLayoutNonLocatedKeyboardCode(dom_code);
    }
  }

  auto event = std::make_unique<KeyEvent>(
      pressed ? EventType::kKeyPressed : EventType::kKeyReleased, code,
      dom_code, flags, dom_key, EventTimeForNow());
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnEventFromWindowThread, window_,
                                std::move(event)));
}

void HaikuEventBridge::OnViewResized(float width, float height) {
  // BView::FrameResized hands over the new width/height in Be's inclusive
  // pixel convention; the origin is unknown here and stays what it was.
  gfx::Size size(static_cast<int>(width) + 1, static_cast<int>(height) + 1);
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnSizeChangedFromWindowThread,
                                window_, size));
}

void HaikuEventBridge::OnQuitRequested() {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnCloseRequestedFromWindowThread,
                                window_));
}

void HaikuEventBridge::OnActivated(bool active) {
  ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&HaikuWindow::OnActivationChangedFromWindowThread, window_,
                     active));
}

void HaikuEventBridge::OnFrameMoved(float x,
                                    float y,
                                    float width,
                                    float height) {
  gfx::Rect bounds(static_cast<int>(x), static_cast<int>(y),
                   static_cast<int>(width), static_cast<int>(height));
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnBoundsChangedFromWindowThread,
                                window_, bounds));
}

void HaikuEventBridge::OnNavigateBack() {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarBack, window_));
}

void HaikuEventBridge::OnNavigateForward() {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarForward, window_));
}

void HaikuEventBridge::OnReloadOrStop() {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarReloadOrStop, window_));
}

void HaikuEventBridge::OnInstall() {
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarInstall, window_));
}

void HaikuEventBridge::OnExtensionActionClicked(const char* id,
                                                float x,
                                                float y,
                                                float width,
                                                float height) {
  // The pointer is only valid during this call, so copy before posting.
  std::string extension_id(id != nullptr ? id : "");
  const gfx::Rect anchor(static_cast<int>(x), static_cast<int>(y),
                         static_cast<int>(width), static_cast<int>(height));
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarExtensionAction,
                                window_, std::move(extension_id), anchor));
}

void HaikuEventBridge::OnNavigateToURL(const char* utf8) {
  // The pointer is only valid during this call, so copy before posting.
  std::string text(utf8 != nullptr ? utf8 : "");
  ui_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&HaikuWindow::OnToolbarNavigateToURL, window_,
                                std::move(text)));
}

}  // namespace ui
