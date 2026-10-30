#!/usr/bin/env python3
"""Hand links of schemes the browser does not serve to Haiku's handler.

content_shell inherits ContentBrowserClient::HandleExternalProtocol, which
answers "handled" and does nothing, so a page that goes to slack://...,
mailto:... or any other scheme outside the network stack just stops. Slack's
browser sign-in ends exactly that way: it opens slack://<team>/magic-login/...
for the desktop app to pick up.

On Haiku the application for such a link is the preferred application of
application/x-vnd.Be.URL.<scheme>, and /bin/open is what looks it up and
starts it (BUrl::OpenWithPreferredApplication) -- the same thing WebPositive
does when a page fails on a scheme it does not know. Running /bin/open keeps
the Haiku API out of content/, which only reaches it through the ozone shim.

Only the page itself or a user's click may start an application: a subframe
of some page cannot.

Usage: port-shell-external-protocol.py <chromium src>. Idempotent.
"""
import sys

src = sys.argv[1]

h = "%s/content/shell/browser/shell_content_browser_client.h" % src
s = open(h).read()
if "HandleExternalProtocol" in s:
    print("  shell_content_browser_client.h: already applied")
else:
    anchor = "  std::string GetUserAgent() override;\n"
    assert s.count(anchor) == 1, s.count(anchor)
    s = s.replace(anchor, anchor + (
        "#if BUILDFLAG(IS_HAIKU)\n"
        "  bool HandleExternalProtocol(\n"
        "      const GURL& url,\n"
        "      base::RepeatingCallback<WebContents*()> web_contents_getter,\n"
        "      FrameTreeNodeId frame_tree_node_id,\n"
        "      NavigationUIData* navigation_data,\n"
        "      bool is_primary_main_frame,\n"
        "      bool is_in_fenced_frame_tree,\n"
        "      network::mojom::WebSandboxFlags sandbox_flags,\n"
        "      ui::PageTransition page_transition,\n"
        "      bool has_user_gesture,\n"
        "      const std::optional<url::Origin>& initiating_origin,\n"
        "      RenderFrameHost* initiator_document,\n"
        "      const net::IsolationInfo& isolation_info,\n"
        "      mojo::PendingRemote<network::mojom::URLLoaderFactory>* "
        "out_factory)\n"
        "      override;\n"
        "#endif\n"), 1)
    open(h, "w").write(s)
    print("  shell_content_browser_client.h: HandleExternalProtocol")

cc = "%s/content/shell/browser/shell_content_browser_client.cc" % src
s = open(cc).read()
if "ShellContentBrowserClient::HandleExternalProtocol" in s:
    print("  shell_content_browser_client.cc: already applied")
    raise SystemExit

anchor = '#include "base/path_service.h"\n'
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, anchor + (
    '#include "base/process/launch.h"\n'
    '#include "base/task/thread_pool.h"\n'), 1)

end = "}  // namespace content"
at = s.rfind(end)
assert at >= 0
s = s[:at] + (
    "#if BUILDFLAG(IS_HAIKU)\n"
    "// A link whose scheme the browser does not serve (slack:, mailto:, ...)\n"
    "// goes to the preferred application of application/x-vnd.Be.URL.<scheme>,\n"
    "// which /bin/open looks up, as WebPositive does. Only for the page itself\n"
    "// or a click: a subframe does not get to start applications.\n"
    "bool ShellContentBrowserClient::HandleExternalProtocol(\n"
    "    const GURL& url,\n"
    "    base::RepeatingCallback<WebContents*()> web_contents_getter,\n"
    "    FrameTreeNodeId frame_tree_node_id,\n"
    "    NavigationUIData* navigation_data,\n"
    "    bool is_primary_main_frame,\n"
    "    bool is_in_fenced_frame_tree,\n"
    "    network::mojom::WebSandboxFlags sandbox_flags,\n"
    "    ui::PageTransition page_transition,\n"
    "    bool has_user_gesture,\n"
    "    const std::optional<url::Origin>& initiating_origin,\n"
    "    RenderFrameHost* initiator_document,\n"
    "    const net::IsolationInfo& isolation_info,\n"
    "    mojo::PendingRemote<network::mojom::URLLoaderFactory>* out_factory) {\n"
    "  if (!url.is_valid() || (!is_primary_main_frame && !has_user_gesture)) {\n"
    "    return true;\n"
    "  }\n"
    "  base::ThreadPool::PostTask(\n"
    "      FROM_HERE,\n"
    "      {base::MayBlock(), base::WithBaseSyncPrimitives(),\n"
    "       base::TaskPriority::USER_VISIBLE},\n"
    "      base::BindOnce(\n"
    "          [](std::string spec) {\n"
    "            base::Process process = base::LaunchProcess(\n"
    "                std::vector<std::string>{\"/bin/open\", spec},\n"
    "                base::LaunchOptions());\n"
    # /bin/open returns as soon as the application is started; waiting for
    # it is what reaps it.
    "            int exit_code;\n"
    "            if (process.IsValid()) {\n"
    "              process.WaitForExit(&exit_code);\n"
    "            }\n"
    "          },\n"
    "          url.spec()));\n"
    "  return true;\n"
    "}\n"
    "#endif\n"
    "\n") + s[at:]
open(cc, "w").write(s)
print("  shell_content_browser_client.cc: external protocols go to /bin/open")
