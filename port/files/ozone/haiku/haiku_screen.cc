// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/haiku_screen.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <Screen.h>

#include <stdlib.h>

#include "ui/display/display.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/haiku/haiku_window_manager.h"

namespace ui {

namespace {

constexpr int64_t kHaikuDisplayId = 1;

// Haiku scales its UI by the default font size, 12 pt being 1x; RENKU's
// Retina mode sets 24 pt. Pages follow the same scale, or at 2x they came
// out at half the size of everything around them. RCH_DEVICE_SCALE
// overrides it.
float UiScale() {
  if (const char* forced = getenv("RCH_DEVICE_SCALE")) {
    const float value = atof(forced);
    if (value >= 0.5f && value <= 4.0f) {
      return value;
    }
  }
  if (be_plain_font == nullptr) {
    return 1.0f;
  }
  const float scale = be_plain_font->Size() / 12.0f;
  return scale < 1.0f ? 1.0f : scale;
}

}  // namespace

HaikuScreen::HaikuScreen(HaikuWindowManager* window_manager)
    : window_manager_(window_manager) {
  BScreen screen(B_MAIN_SCREEN_ID);
  BRect frame = screen.Frame();
  gfx::Rect bounds(0, 0, static_cast<int>(frame.Width()) + 1,
                   static_cast<int>(frame.Height()) + 1);

  // bounds are in pixels; the display's own bounds are in DIPs.
  display::Display display(kHaikuDisplayId);
  display.SetScaleAndBounds(UiScale(), bounds);
  display_list_.AddDisplay(display, display::DisplayList::Type::PRIMARY);
}

HaikuScreen::~HaikuScreen() = default;

const std::vector<display::Display>& HaikuScreen::GetAllDisplays() const {
  return display_list_.displays();
}

display::Display HaikuScreen::GetPrimaryDisplay() const {
  auto it = display_list_.GetPrimaryDisplayIterator();
  CHECK(it != display_list_.displays().end());
  return *it;
}

display::Display HaikuScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  return GetPrimaryDisplay();
}

gfx::Point HaikuScreen::GetCursorScreenPoint() const {
  // get_mouse() reports in screen pixels; this returns DIPs.
  BPoint location;
  uint32 buttons = 0;
  get_mouse(&location, &buttons);
  const float scale = GetPrimaryDisplay().device_scale_factor();
  return gfx::Point(static_cast<int>(location.x / scale),
                    static_cast<int>(location.y / scale));
}

gfx::AcceleratedWidget HaikuScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  // point is in DIPs, the windows' frames in pixels.
  const float scale = GetPrimaryDisplay().device_scale_factor();
  return window_manager_->GetAcceleratedWidgetAtScreenPoint(
      gfx::Point(static_cast<int>(point.x() * scale),
                 static_cast<int>(point.y() * scale)));
}

display::Display HaikuScreen::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return GetPrimaryDisplay();
}

display::Display HaikuScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  return GetPrimaryDisplay();
}

void HaikuScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void HaikuScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

}  // namespace ui
