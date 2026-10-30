#!/usr/bin/env python3
"""--rch-load-first=<https URL>: load a page out of sight, then open the window.

R Slack signs in by loading Slack's auth.loginMagicBulk once in its profile.
The session cookie Slack sets there is SameSite=Lax, so it only counts from a
top-level navigation -- an iframe or a fetch() would not keep it -- and doing
it in a window of its own showed the user a page of JSON for several seconds.

With this switch content_shell loads <URL> in a WebContents that has no
window, in the same browser context, and opens the startup window only when
that load has stopped (or after 30 s). Only https URLs are taken.

Usage: port-shell-load-first.py <chromium src>. Idempotent.
"""
import sys

src = sys.argv[1]
cc = "%s/content/shell/browser/shell_browser_main_parts.cc" % src
s = open(cc).read()
if "rch-load-first" in s:
    print("  shell_browser_main_parts.cc: already applied")
    raise SystemExit

old = ('void ShellBrowserMainParts::InitializeMessageLoopContext() {\n'
       '  Shell::CreateNewWindow(browser_context_.get(), GetStartupURL(), nullptr,\n'
       '                         gfx::Size());\n'
       '}\n')
assert s.count(old) == 1, s.count(old)
new = ('#if BUILDFLAG(IS_HAIKU)\n'
       'namespace {\n'
       '\n'
       '// Loads one page in a WebContents without a window, then opens the\n'
       '// startup window and deletes itself.\n'
       'class LoadFirst : public WebContentsObserver {\n'
       ' public:\n'
       '  LoadFirst(BrowserContext* context, const GURL& first, const GURL& then)\n'
       '      : context_(context), then_(then) {\n'
       '    contents_ = WebContents::Create(WebContents::CreateParams(context));\n'
       '    Observe(contents_.get());\n'
       '    timeout_.Start(FROM_HERE, base::Seconds(30),\n'
       '                   base::BindOnce(&LoadFirst::Done, base::Unretained(this)));\n'
       '    contents_->GetController().LoadURL(first, Referrer(),\n'
       '                                       ui::PAGE_TRANSITION_AUTO_TOPLEVEL,\n'
       '                                       std::string());\n'
       '  }\n'
       '\n'
       '  void DidStopLoading() override { Done(); }\n'
       '\n'
       ' private:\n'
       '  void Done() {\n'
       '    if (done_) {\n'
       '      return;\n'
       '    }\n'
       '    done_ = true;\n'
       '    timeout_.Stop();\n'
       '    Observe(nullptr);\n'
       '    // Not from inside the observer call: post the rest.\n'
       '    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(\n'
       '        FROM_HERE, base::BindOnce(\n'
       '                       [](LoadFirst* self) {\n'
       '                         Shell::CreateNewWindow(self->context_, self->then_,\n'
       '                                                nullptr, gfx::Size());\n'
       '                         delete self;\n'
       '                       },\n'
       '                       base::Unretained(this)));\n'
       '  }\n'
       '\n'
       '  raw_ptr<BrowserContext> context_;\n'
       '  GURL then_;\n'
       '  std::unique_ptr<WebContents> contents_;\n'
       '  base::OneShotTimer timeout_;\n'
       '  bool done_ = false;\n'
       '};\n'
       '\n'
       '}  // namespace\n'
       '#endif\n'
       '\n'
       'void ShellBrowserMainParts::InitializeMessageLoopContext() {\n'
       '#if BUILDFLAG(IS_HAIKU)\n'
       '  // --rch-load-first=<https URL>: load that page out of sight in this\n'
       '  // profile before the window opens (R Slack\'s sign-in).\n'
       '  const GURL first(base::CommandLine::ForCurrentProcess()\n'
       '                       ->GetSwitchValueASCII("rch-load-first"));\n'
       '  if (first.is_valid() && first.SchemeIs(url::kHttpsScheme)) {\n'
       '    new LoadFirst(browser_context_.get(), first, GetStartupURL());\n'
       '    return;\n'
       '  }\n'
       '#endif\n'
       '  Shell::CreateNewWindow(browser_context_.get(), GetStartupURL(), nullptr,\n'
       '                         gfx::Size());\n'
       '}\n')
s = s.replace(old, new, 1)

anchor = '#include "content/public/common/url_constants.h"\n'
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, anchor + (
    '#include "base/memory/raw_ptr.h"\n'
    '#include "base/task/sequenced_task_runner.h"\n'
    '#include "base/timer/timer.h"\n'
    '#include "content/public/browser/navigation_controller.h"\n'
    '#include "content/public/browser/web_contents.h"\n'
    '#include "content/public/browser/web_contents_observer.h"\n'
    '#include "content/public/common/referrer.h"\n'
    '#include "url/url_constants.h"\n'), 1)
open(cc, "w").write(s)
print("  shell_browser_main_parts.cc: --rch-load-first")
