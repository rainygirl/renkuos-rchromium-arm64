#!/usr/bin/env python3
"""Keep one Haiku sources block in ui/gfx/BUILD.gn.

haiku-port.diff adds ui/gfx's Haiku block, Skia's font fallback included. A
tree ported before that block existed already has a shorter one, from the
autofix loop, and `patch --forward` cannot tell the two apart, so it adds the
second next to the first. gn then stops on animation_haiku.o and
font_render_params_haiku.o being built twice. Run after haiku-port.diff: the
shorter block goes when the full one is there, and a clean checkout, which
only ever gets the full one, is left alone.
"""
import os
import sys

SHORT = (
    "  if (is_haiku) {\n"
    "    sources += [\n"
    "      \"animation/animation_haiku.cc\",\n"
    "      \"font_render_params_haiku.cc\",\n"
    "    ]\n"
    "  }\n"
    "\n"
)
FULL = (
    "  if (is_haiku) {\n"
    "    sources += [\n"
    "      \"animation/animation_haiku.cc\",\n"
    "      \"font_fallback_skia.cc\",\n"
)


def main(src):
    path = os.path.join(src, "ui/gfx/BUILD.gn")
    with open(path) as f:
        text = f.read()
    if FULL in text and SHORT in text:
        with open(path, "w") as f:
            f.write(text.replace(SHORT, "", 1))
        print("  ui/gfx Haiku sources: dropped the older block")
    else:
        print("  ui/gfx Haiku sources: one block, nothing to do")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
