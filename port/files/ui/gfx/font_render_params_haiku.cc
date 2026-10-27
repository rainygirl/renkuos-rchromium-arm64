// Copyright 2026 The RENKU project
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Text rendering parameters. The Linux implementation asks fontconfig; Haiku
// has no fontconfig, and its own preferences live in the app_server, which
// //ui/gfx cannot reach (libbe belongs to the ozone layer). These are the
// defaults Haiku itself ships with: greyscale antialiasing with hinting on,
// no subpixel (LCD) filtering.

#include "ui/gfx/font_render_params.h"

namespace gfx {

namespace {

FontRenderParams HaikuDefaults() {
  FontRenderParams params;
  params.antialiasing = true;
  params.use_bitmaps = false;
  params.hinting = FontRenderParams::HINTING_FULL;
  params.autohinter = false;
  params.subpixel_positioning = false;
  params.subpixel_rendering = FontRenderParams::SUBPIXEL_RENDERING_NONE;
  return params;
}

}  // namespace

FontRenderParams GetFontRenderParams(const FontRenderParamsQuery& query,
                                     std::string* family_out) {
  if (family_out && !query.families.empty()) {
    *family_out = query.families[0];
  }
  return HaikuDefaults();
}

// Haiku has one display scale, and fonts are rasterised for it by the
// compositor; nothing here varies with it.
float GetFontRenderParamsDeviceScaleFactor() {
  return 1.0f;
}

}  // namespace gfx
