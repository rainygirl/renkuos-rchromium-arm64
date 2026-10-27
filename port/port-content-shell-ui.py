#!/usr/bin/env python3
"""Select R Chromium's Haiku ShellPlatformDelegate in content_shell, and force
software compositing in-process on Haiku.

1. content/shell/BUILD.gn compiles exactly one shell_platform_delegate_*.cc,
   chosen by shell_use_toolkit_views. On Haiku toolkit_views is forced true (the
   desktop-platform assert in tools/grit/grit_args.gni lists is_haiku), so the
   views delegate would be picked -- drawing an in-content Back/Forward/Reload
   toolbar on top of R Chromium's own native BeAPI toolbar. So select the Haiku
   delegate first, before the views branch, and take the aura content-view
   delegate with it.

2. content/shell/app/shell_main_delegate.cc: force --in-process-gpu and
   --disable-gpu on Haiku. Haiku has no GL, so the page composites in software,
   and the display compositor must run in the browser process (where the BWindow
   / HaikuWindowManager lives). Without --in-process-gpu the software output
   device is built in a separate viz process whose HaikuSurfaceFactory has no
   window manager, so CreateCanvasForWidget() returns null and
   CHECK(surface_ozone) aborts (output_surface_provider_impl.cc:167). Without
   --disable-gpu the in-process compositor still tries to create a GL context and
   hits a NOTREACHED in gl_factory_ozone.cc:62. With both, news.naver.com and
   other composited pages render instead of crashing/hanging (verified on the
   renku-arm64 desktop, 2026-09-14).

3. Same file: child processes run with Haiku's debugger disabled, so a
   crashed renderer dies and is replaced instead of sitting suspended behind
   debug_server's alert.

Every step is idempotent.
"""
import sys


def _patch_build_gn(src):
    p = src + "/content/shell/BUILD.gn"
    s = open(p).read()
    if "shell_platform_delegate_haiku.cc" in s:
        print("  content_shell UI: already selected")
        return
    old = (
        '    if (shell_use_toolkit_views) {\n'
        '      sources += [\n'
        '        "browser/shell_platform_delegate_views.cc",\n'
        '        "browser/shell_web_contents_view_delegate_views.cc",\n'
        '      ]\n'
        '      public_deps += [ "//ui/base/mojom:ui_base_types" ]\n'
        '      deps += [\n'
        '        "//ui/color:color_headers",\n'
        '        "//ui/resources",\n'
        '        "//ui/views:test_support",\n'
        '        "//ui/views/controls/webview",\n'
        '        "//ui/wm:test_support",\n'
        '      ]\n'
        '    } else {\n'
        '      sources += [\n'
        '        "browser/shell_platform_delegate_aura.cc",\n'
        '        "browser/shell_web_contents_view_delegate_aura.cc",\n'
        '      ]\n'
        '    }\n'
    )
    new = (
        '    if (is_haiku) {\n'
        '      # R Chromium draws its own native BeAPI toolbar in the ozone\n'
        '      # layer, so content_shell takes the aura content path and this\n'
        '      # Haiku delegate, never the in-content views toolbar -- even\n'
        '      # though toolkit_views is forced true for Haiku.\n'
        '      sources += [\n'
        '        "browser/shell_platform_delegate_haiku.cc",\n'
        '        "browser/shell_web_contents_view_delegate_aura.cc",\n'
        '      ]\n'
        '    } else if (shell_use_toolkit_views) {\n'
        '      sources += [\n'
        '        "browser/shell_platform_delegate_views.cc",\n'
        '        "browser/shell_web_contents_view_delegate_views.cc",\n'
        '      ]\n'
        '      public_deps += [ "//ui/base/mojom:ui_base_types" ]\n'
        '      deps += [\n'
        '        "//ui/color:color_headers",\n'
        '        "//ui/resources",\n'
        '        "//ui/views:test_support",\n'
        '        "//ui/views/controls/webview",\n'
        '        "//ui/wm:test_support",\n'
        '      ]\n'
        '    } else {\n'
        '      sources += [\n'
        '        "browser/shell_platform_delegate_aura.cc",\n'
        '        "browser/shell_web_contents_view_delegate_aura.cc",\n'
        '      ]\n'
        '    }\n'
    )
    n = s.count(old)
    if n != 1:
        print("  content_shell UI: DRIFTED (anchor found %d times)" % n)
        sys.exit(1)
    open(p, "w").write(s.replace(old, new, 1))
    print("  content_shell UI: haiku delegate selected (is_haiku first)")


def _patch_main_delegate(src):
    p = src + "/content/shell/app/shell_main_delegate.cc"
    s = open(p).read()
    if "BUILDFLAG(IS_HAIKU)" in s and "switches::kInProcessGPU" in s:
        print("  content_shell UI: haiku compositing switches already set")
        return
    anchor = (
        '    command_line.AppendSwitch(switches::kRunWebTests);\n'
        '  }\n'
    )
    block = (
        '\n'
        '#if BUILDFLAG(IS_HAIKU)\n'
        '  // Haiku has no GL, so the page must composite in software, and the\n'
        '  // display compositor has to run in the browser process -- that is\n'
        '  // where the BWindow (HaikuWindowManager) lives. Without\n'
        '  // --in-process-gpu the software output device is built in a separate\n'
        '  // viz process whose HaikuSurfaceFactory has no window manager, so\n'
        '  // CreateCanvasForWidget() returns null and CHECK(surface_ozone)\n'
        '  // aborts (output_surface_provider_impl.cc). Without --disable-gpu the\n'
        '  // in-process compositor still tries to create a GL context and hits a\n'
        '  // NOTREACHED in gl_factory_ozone.cc. Set both so every page\n'
        '  // composites instead of crashing.\n'
        '  if (!command_line.HasSwitch(switches::kInProcessGPU)) {\n'
        '    command_line.AppendSwitch(switches::kInProcessGPU);\n'
        '  }\n'
        '  if (!command_line.HasSwitch(switches::kDisableGpu)) {\n'
        '    command_line.AppendSwitch(switches::kDisableGpu);\n'
        '  }\n'
        '#endif  // BUILDFLAG(IS_HAIKU)\n'
    )
    n = s.count(anchor)
    if n != 1:
        print("  content_shell UI: main-delegate DRIFTED (anchor found %d times)" % n)
        sys.exit(1)
    open(p, "w").write(s.replace(anchor, anchor + block, 1))
    print("  content_shell UI: haiku compositing switches set "
          "(--in-process-gpu --disable-gpu)")


def _patch_child_debugger(src):
    p = src + "/content/shell/app/shell_main_delegate.cc"
    s = open(p).read()
    if "disable_debugger(" in s:
        print("  content_shell UI: child crash dialog already off")
        return
    inc_anchor = '#include "build/build_config.h"\n'
    inc = (
        inc_anchor +
        '\n'
        '#if BUILDFLAG(IS_HAIKU)\n'
        '#include <OS.h>\n'
        '#include <stdlib.h>\n'
        '#endif\n'
    )
    anchor = '#endif  // BUILDFLAG(IS_HAIKU)\n'
    block = (
        anchor +
        '\n'
        '#if BUILDFLAG(IS_HAIKU)\n'
        '  // A crashed child (renderer, utility) should just die: the browser\n'
        '  // notices and shows the page as crashed, as on any other system. Haiku\n'
        '  // instead hands every crashing team to debug_server, whose modal\n'
        '  // "has encountered an error" alert keeps the child suspended until\n'
        '  // someone answers -- one alert per renderer, and a relaunched\n'
        '  // renderer that crashes again brings the next. The browser process\n'
        '  // keeps the alert. RCH_CHILD_DEBUGGER=1 keeps it for children too,\n'
        '  // for debugging.\n'
        '  if (command_line.HasSwitch(switches::kProcessType) &&\n'
        '      !getenv("RCH_CHILD_DEBUGGER")) {\n'
        '    disable_debugger(1);\n'
        '  }\n'
        '#endif  // BUILDFLAG(IS_HAIKU)\n'
    )
    for a in (inc_anchor, anchor):
        n = s.count(a)
        if n != 1:
            print("  content_shell UI: child-debugger DRIFTED (%r found %d times)"
                  % (a.strip(), n))
            sys.exit(1)
    s = s.replace(inc_anchor, inc, 1).replace(anchor, block, 1)
    open(p, "w").write(s)
    print("  content_shell UI: crash alert off in child processes")


def main(src):
    _patch_build_gn(src)
    _patch_main_delegate(src)
    _patch_child_debugger(src)


if __name__ == "__main__":
    main(sys.argv[1])
