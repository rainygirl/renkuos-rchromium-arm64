// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"

#include <map>

#include "base/no_destructor.h"

namespace ui {

namespace {

// UI-thread-only: registration and lookup both happen on the UI thread, so no
// lock. Keyed by the AcceleratedWidget the delegate and the window share.
std::map<gfx::AcceleratedWidget, HaikuToolbarObserver*>& ObserverMap() {
  static base::NoDestructor<std::map<gfx::AcceleratedWidget, HaikuToolbarObserver*>>
      map;
  return *map;
}

}  // namespace

HaikuToolbarAction::HaikuToolbarAction() = default;
HaikuToolbarAction::HaikuToolbarAction(const HaikuToolbarAction&) = default;
HaikuToolbarAction& HaikuToolbarAction::operator=(const HaikuToolbarAction&) =
    default;
HaikuToolbarAction::~HaikuToolbarAction() = default;

void SetHaikuToolbarObserver(gfx::AcceleratedWidget widget,
                             HaikuToolbarObserver* observer) {
  if (observer)
    ObserverMap()[widget] = observer;
  else
    ObserverMap().erase(widget);
}

HaikuToolbarObserver* GetHaikuToolbarObserver(gfx::AcceleratedWidget widget) {
  auto it = ObserverMap().find(widget);
  return it == ObserverMap().end() ? nullptr : it->second;
}

}  // namespace ui
