#!/usr/bin/env python3
"""Wire Chromium's extension system into content_shell for R Chromium.

The extension code itself lives in new files (port/files/content/shell/
{browser,common,renderer}/extensions/, copied by apply-haiku-port.sh); this
script makes content_shell call it, all behind SHELL_USE_BEAPI, which
step 1 defines for content_shell_lib when is_haiku:

1. content/shell/BUILD.gn: build the new files into content_shell_lib with
   their //extensions dependencies, and repack the extension resources into
   content_shell.pak.
2. ShellBrowserMainParts: install the clients before the browser contexts
   exist, start the extension system after, tear down around their
   destruction.
3. ShellBrowserContext: give each context a PrefService before its keyed
   services are created.
4. ShellContentBrowserClient: the URL loader factory, WebRequest proxy,
   process-model, interface-binder and navigation-throttle hooks, each a
   call into extensions::ShellExtensions.
5. ShellContentClient: register chrome-extension:// as Chrome does.
6. ShellContentRendererClient: create the extensions renderer client and
   forward frame, script-injection and service-worker hooks to it.

See docs/EXTENSIONS.md. Idempotent, and fails loudly (exit 1) if an anchor
has drifted.
"""
import os
import sys

MARK = "R Chromium (extensions)"


def edit(src, rel, old, new, label):
    path = os.path.join(src, rel)
    s = open(path).read()
    if new in s:
        print("  %s: already applied" % label)
        return
    n = s.count(old)
    if n != 1:
        print("  %s: DRIFTED (anchor found %d times in %s)" % (label, n, rel))
        sys.exit(1)
    open(path, "w").write(s.replace(old, new, 1))
    print("  %s: applied" % label)


def remove(src, rel, old, new, label):
    """Like edit(), for deletions: done once `old` is gone. (The shorter
    `new` can occur elsewhere in the file, so edit()'s check would lie.)"""
    path = os.path.join(src, rel)
    s = open(path).read()
    n = s.count(old)
    if n == 0:
        print("  %s: already applied" % label)
        return
    if n != 1:
        print("  %s: DRIFTED (anchor found %d times in %s)" % (label, n, rel))
        sys.exit(1)
    open(path, "w").write(s.replace(old, new, 1))
    print("  %s: applied" % label)


def _drop_stale_block(src, rel, start_marker, end_anchor):
    """Removes text from `start_marker` up to `end_anchor`, so edit() can
    insert the current version of a block whose contents changed."""
    path = os.path.join(src, rel)
    s = open(path).read()
    start = s.find(start_marker)
    if start < 0:
        return
    end = s.find(end_anchor, start)
    assert end > start, "stale block without its anchor in " + rel
    open(path, "w").write(s[:start] + s[end:])


def add_include(src, rel, after, label):
    edit(src, rel, after,
         after +
         "\n#if defined(SHELL_USE_BEAPI)\n"
         "#include \"content/shell/browser/extensions/shell_extensions.h\"\n"
         "#endif\n",
         label)


EXTENSION_SOURCES = [
    "browser/extensions/shell_action_api.cc",
    "browser/extensions/shell_action_api.h",
    "browser/extensions/shell_action_toolbar.cc",
    "browser/extensions/shell_action_toolbar.h",
    "browser/extensions/shell_cookies_api.cc",
    "browser/extensions/shell_cookies_api.h",
    "browser/extensions/shell_extension_system.cc",
    "browser/extensions/shell_extension_system.h",
    "browser/extensions/shell_extension_web_contents_observer.cc",
    "browser/extensions/shell_extension_web_contents_observer.h",
    "browser/extensions/shell_extensions_api_client.cc",
    "browser/extensions/shell_extensions_api_client.h",
    "browser/extensions/shell_extensions.cc",
    "browser/extensions/shell_extensions.h",
    "browser/extensions/shell_extensions_browser_api_provider.cc",
    "browser/extensions/shell_extensions_browser_api_provider.h",
    "browser/extensions/shell_extensions_browser_client.cc",
    "browser/extensions/shell_extensions_browser_client.h",
    "browser/extensions/shell_tabs.cc",
    "browser/extensions/shell_tabs.h",
    "browser/extensions/shell_tabs_api.cc",
    "browser/extensions/shell_tabs_api.h",
    "common/extensions/shell_extensions_api_provider.cc",
    "common/extensions/shell_extensions_api_provider.h",
    "common/extensions/shell_extensions_client.cc",
    "common/extensions/shell_extensions_client.h",
    "renderer/extensions/shell_extensions_renderer_client.cc",
    "renderer/extensions/shell_extensions_renderer_client.h",
]

EXTENSION_DEPS = [
    "//components/custom_handlers",
    "//components/enterprise/browser/promotion",
    "//components/keyed_service/content",
    "//components/pref_registry",
    "//components/prefs",
    "//components/sessions",
    "//components/user_prefs",
    "//components/value_store",
    "//content/shell/common/extensions/api",
    "//extensions/browser",
    "//extensions/browser/api",
    "//extensions/browser/api:api_provider",
    "//extensions/common",
    "//extensions/common/api",
    "//extensions/common:core_api_provider",
    "//extensions/renderer",
]

EXTENSION_PAKS = [
    "extensions/extensions_browser_resources_100_percent.pak",
    "extensions/extensions_renderer_generated_resources.pak",
    "extensions/extensions_renderer_resources.pak",
    "extensions/extensions_resources.pak",
    "extensions/strings/extensions_strings_en-US.pak",
]


def build_gn(src):
    rel = "content/shell/BUILD.gn"
    # content_shell_lib, after the platform delegates and before its
    # ChromeOS deps (port-content-shell-ui.py checks the delegate block whole).
    anchor = ("  if (is_chromeos) {\n"
              "    deps += [ \"//chromeos/ash/components/dbus\" ]\n"
              "  }\n")
    block = (
         "  if (is_haiku) {\n"
         "    # " + MARK + ": Chromium's extension system, see\n"
         "    # docs/EXTENSIONS.md in the R Chromium repository.\n"
         "    defines += [ \"SHELL_USE_BEAPI=1\" ]\n"
         "    sources += [\n" +
         "".join("      \"%s\",\n" % f for f in EXTENSION_SOURCES) +
         "    ]\n"
         "    deps += [\n" +
         "".join("      \"%s\",\n" % d for d in EXTENSION_DEPS) +
         "    ]\n"
         "  }\n"
         "\n")
    # A block from an older file list is replaced, not doubled.
    if block + anchor not in open(os.path.join(src, rel)).read():
        _drop_stale_block(src, rel,
                          "  if (is_haiku) {\n"
                          "    # " + MARK + ": Chromium's extension system",
                          anchor)
    edit(src, rel, anchor, block + anchor, "BUILD.gn extension sources")

    anchor = "  output = \"$root_out_dir/content_shell.pak\"\n}\n"
    edit(src, rel, anchor,
         "  if (is_haiku) {\n"
         "    # " + MARK + ": extension API bindings, strings and icons.\n"
         "    sources += [\n" +
         "".join("      \"$root_gen_dir/%s\",\n" % p for p in EXTENSION_PAKS) +
         "    ]\n"
         "    deps += [\n"
         "      \"//extensions:extensions_resources\",\n"
         "      \"//extensions/strings\",\n"
         "    ]\n"
         "  }\n" + anchor,
         "BUILD.gn extension resources in content_shell.pak")


def components_build_gn(src):
    # The component test targets assert that nothing they depend on links
    # //extensions, and they reach content_shell through
    # //content/test:test_support. Those tests are never built for R Chromium.
    anchor = ("]\n"
              "\n"
              "# To add a unit test to this target, make a \"unit_tests\" "
              "source_set in your\n")
    edit(src, "components/BUILD.gn", anchor,
         "]\n"
         "\n"
         "# " + MARK + ": content_shell links //extensions, and these\n"
         "# test targets reach it through //content/test:test_support. They are\n"
         "# not built for R Chromium, so drop the assertion there.\n"
         "if (is_haiku) {\n"
         "  disallowed_extension_deps_ = []\n"
         "}\n" + anchor[1:],
         "components/BUILD.gn extension assertion")


def extensions_renderer_build_gn(src):
    # //extensions/renderer lists //chrome:resources as a dependency but uses
    # nothing from it (no source includes a chrome/ header). With content_shell
    # linking //extensions, the leftover edge trips the component tests'
    # "nothing depends on //chrome" assertion.
    remove(src, "extensions/renderer/BUILD.gn",
         "    \"//build:chromeos_buildflags\",\n"
         "    \"//chrome:resources\",\n"
         "    \"//components/crx_file\",\n",
         "    \"//build:chromeos_buildflags\",\n"
         "    \"//components/crx_file\",\n",
         "extensions/renderer/BUILD.gn unused //chrome dependency")


def security_interstitials(src):
    # The extension embedder brings //components/security_interstitials into
    # the build, and LaunchDateAndTimeSettings() (the "fix your clock" button
    # on a certificate-date error page) has a branch per OS and an #error for
    # the rest. Haiku's equivalent is the Time preferences.
    edit(src, "components/security_interstitials/content/utils.cc",
         "#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_IOS)\n",
         "#elif BUILDFLAG(IS_HAIKU)\n"
         "  // " + MARK + ": Haiku's date and time settings.\n"
         "  base::LaunchOptions options;\n"
         "  options.wait = false;\n"
         "  base::LaunchProcess(\n"
         "      base::CommandLine(base::FilePath(\"/boot/system/preferences/Time\")),\n"
         "      options);\n"
         "\n"
         "#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_IOS)\n",
         "security_interstitials LaunchDateAndTimeSettings")


def extension_commands(src):
    # Which of a manifest's "commands" key bindings apply ("windows", "mac",
    # "chromeos", "linux"); every other OS is an #error. Haiku takes the
    # Linux ones, as desktop Android does upstream.
    edit(src, "extensions/common/command.cc",
         "#elif BUILDFLAG(IS_DESKTOP_ANDROID)\n",
         "#elif BUILDFLAG(IS_HAIKU)\n"
         "  // " + MARK + ": manifests have no Haiku key bindings.\n"
         "  return ui::kKeybindingPlatformLinux;\n"
         "#elif BUILDFLAG(IS_DESKTOP_ANDROID)\n",
         "extensions/common/command.cc key binding platform")


def system_cpu_build_gn(src):
    # chrome.system.cpu needs a per-OS CpuInfoProvider; Haiku's is in
    # port/files.
    edit(src, "extensions/browser/api/system_cpu/BUILD.gn",
         "  if (is_win) {\n"
         "    sources += [ \"cpu_info_provider_win.cc\" ]\n",
         "  if (is_haiku) {\n"
         "    sources += [ \"cpu_info_provider_haiku.cc\" ]\n"
         "  }\n"
         "\n"
         "  if (is_win) {\n"
         "    sources += [ \"cpu_info_provider_win.cc\" ]\n",
         "system_cpu BUILD.gn Haiku provider")


def dnr_keep_allowlisted_frames(src):
    # declarativeNetRequest decides allowAllRequests when a frame navigates
    # and remembers the frame in its RulesetMatcher. Updating a ruleset (any
    # updateDynamicRules() call) replaces the matcher and forgets those
    # frames, so a page that was exempted (Brave Shields "off" for a site)
    # gets its later requests blocked once the extension touches its rules --
    # which Brave Shields does every time its service worker wakes. Carry the
    # frames over to the replacement.
    d = "extensions/browser/api/declarative_net_request/"
    edit(src, d + "ruleset_matcher_base.h",
         "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
         "\n"
         "  // Returns the tracked highest priority matching allowsAllRequests action, if\n",
         "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
         "\n"
         "  // " + MARK + ": takes over the frames `other` allow-listed, when\n"
         "  // this matcher replaces it.\n"
         "  void InheritAllowlistedFrames(const RulesetMatcherBase& other);\n"
         "\n"
         "  // Returns the tracked highest priority matching allowsAllRequests action, if\n",
         "DNR: RulesetMatcherBase::InheritAllowlistedFrames declaration")
    edit(src, d + "ruleset_matcher_base.cc",
         "std::optional<RequestAction>\n"
         "RulesetMatcherBase::GetAllowlistedFrameActionForTesting(\n",
         "void RulesetMatcherBase::InheritAllowlistedFrames(\n"
         "    const RulesetMatcherBase& other) {\n"
         "  for (const auto& [frame_id, action] : other.allowlisted_frames_) {\n"
         "    allowlisted_frames_.emplace(frame_id, action.Clone());\n"
         "  }\n"
         "}\n"
         "\n"
         "std::optional<RequestAction>\n"
         "RulesetMatcherBase::GetAllowlistedFrameActionForTesting(\n",
         "DNR: RulesetMatcherBase::InheritAllowlistedFrames")
    edit(src, d + "ruleset_matcher.h",
         "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
         "\n"
         "  // ID of the ruleset.",
         "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
         "\n"
         "  // " + MARK + ": see RulesetMatcherBase.\n"
         "  void InheritAllowlistedFrames(const RulesetMatcher& other);\n"
         "\n"
         "  // ID of the ruleset.",
         "DNR: RulesetMatcher::InheritAllowlistedFrames declaration")
    edit(src, d + "ruleset_matcher.cc",
         "  url_matcher_.OnDidFinishNavigation(navigation_handle);\n"
         "  regex_matcher_.OnDidFinishNavigation(navigation_handle);\n"
         "}\n",
         "  url_matcher_.OnDidFinishNavigation(navigation_handle);\n"
         "  regex_matcher_.OnDidFinishNavigation(navigation_handle);\n"
         "}\n"
         "\n"
         "void RulesetMatcher::InheritAllowlistedFrames(const RulesetMatcher& other) {\n"
         "  url_matcher_.InheritAllowlistedFrames(other.url_matcher_);\n"
         "  regex_matcher_.InheritAllowlistedFrames(other.regex_matcher_);\n"
         "}\n",
         "DNR: RulesetMatcher::InheritAllowlistedFrames")
    edit(src, d + "composite_matcher.cc",
         "void CompositeMatcher::AddOrUpdateRulesets(MatcherList matchers) {\n"
         "  std::set<RulesetID> ids_to_remove;\n"
         "  for (const auto& matcher : matchers) {\n"
         "    ids_to_remove.insert(matcher->id());\n"
         "  }\n",
         "void CompositeMatcher::AddOrUpdateRulesets(MatcherList matchers) {\n"
         "  std::set<RulesetID> ids_to_remove;\n"
         "  for (const auto& matcher : matchers) {\n"
         "    ids_to_remove.insert(matcher->id());\n"
         "    // " + MARK + ": frames stay allow-listed across updates.\n"
         "    if (const RulesetMatcher* old = GetMatcherWithID(matcher->id())) {\n"
         "      matcher->InheritAllowlistedFrames(*old);\n"
         "    }\n"
         "  }\n",
         "DNR: keep allow-listed frames across ruleset updates")


def dnr_allowlist_at_ready_to_commit(src):
    # allowAllRequests is recorded for a frame in DidFinishNavigation, which
    # the browser handles when the renderer reports the commit. The new
    # document's first requests travel on another pipe and can be handled
    # before that (by 200 ms on a VM), so they are matched as if the site
    # were not exempted. Record the frame at ReadyToCommitNavigation too,
    # which comes before the renderer is told to commit; DidFinishNavigation
    # still has the last word.
    d = "extensions/browser/api/declarative_net_request/"
    nav = "  void OnReadyToCommitNavigation(content::NavigationHandle* navigation_handle);\n"
    edit(src, d + "ruleset_matcher_base.h",
         "  void InheritAllowlistedFrames(const RulesetMatcherBase& other);\n",
         "  void InheritAllowlistedFrames(const RulesetMatcherBase& other);\n"
         "  // " + MARK + ": records allowAllRequests for the frame a\n"
         "  // navigation is about to commit in, before its document can make\n"
         "  // requests.\n" + nav,
         "DNR: RulesetMatcherBase::OnReadyToCommitNavigation declaration")
    edit(src, d + "ruleset_matcher_base.cc",
         "#include \"extensions/browser/api/declarative_net_request/utils.h\"\n",
         "#include \"extensions/browser/api/declarative_net_request/utils.h\"\n"
         "#include \"content/public/browser/web_contents.h\"\n"
         "#include \"extensions/browser/extensions_browser_client.h\"\n"
         "#include \"extensions/common/api/declarative_net_request.h\"\n"
         "#include \"extensions/common/constants.h\"\n",
         "DNR: ruleset_matcher_base.cc includes")
    edit(src, d + "ruleset_matcher_base.cc",
         "void RulesetMatcherBase::InheritAllowlistedFrames(\n",
         "void RulesetMatcherBase::OnReadyToCommitNavigation(\n"
         "    content::NavigationHandle* navigation_handle) {\n"
         "  namespace dnr_api = api::declarative_net_request;\n"
         "  content::RenderFrameHost* host = navigation_handle->GetRenderFrameHost();\n"
         "  if (!host) {\n"
         "    return;\n"
         "  }\n"
         "  // As RequestParams(RenderFrameHost*) would see it after the commit.\n"
         "  content::RenderFrameHost* parent =\n"
         "      navigation_handle->GetParentFrameOrOuterDocument();\n"
         "  const GURL& url = navigation_handle->GetURL();\n"
         "  const url::Origin initiator =\n"
         "      parent ? parent->GetLastCommittedOrigin() : url::Origin();\n"
         "  const url::Origin top_origin =\n"
         "      parent ? parent->GetOutermostMainFrame()->GetLastCommittedOrigin()\n"
         "             : url::Origin::Create(url);\n"
         "  int tab_id = extension_misc::kUnknownTabId;\n"
         "  int window_id = extension_misc::kUnknownWindowId;\n"
         "  ExtensionsBrowserClient::Get()->GetTabAndWindowIdForWebContents(\n"
         "      navigation_handle->GetWebContents(), &tab_id, &window_id);\n"
         "  const auto type = parent ? dnr_api::ResourceType::kSubFrame\n"
         "                           : dnr_api::ResourceType::kMainFrame;\n"
         "  const auto method = navigation_handle->IsPost()\n"
         "                          ? dnr_api::RequestMethod::kPost\n"
         "                          : dnr_api::RequestMethod::kGet;\n"
         "\n"
         "  RequestParams params(url, initiator, top_origin, type, method, tab_id,\n"
         "                       /*response_headers=*/nullptr);\n"
         "  std::optional<RequestAction> action =\n"
         "      GetAllowAllRequestsAction(params, RulesetMatchingStage::kOnBeforeRequest);\n"
         "  if (navigation_handle->GetResponseHeaders()) {\n"
         "    RequestParams params_with_headers(\n"
         "        url, initiator, top_origin, type, method, tab_id,\n"
         "        navigation_handle->GetResponseHeaders());\n"
         "    action = GetMaxPriorityAction(\n"
         "        std::move(action),\n"
         "        GetAllowAllRequestsAction(params_with_headers,\n"
         "                                  RulesetMatchingStage::kOnHeadersReceived));\n"
         "  }\n"
         "  if (parent) {\n"
         "    action = GetMaxPriorityAction(\n"
         "        GetAllowlistedFrameAction(parent->GetGlobalId()), std::move(action));\n"
         "  }\n"
         "  if (!action) {\n"
         "    return;\n"
         "  }\n"
         "  allowlisted_frames_.erase(host->GetGlobalId());\n"
         "  allowlisted_frames_.insert(\n"
         "      std::make_pair(host->GetGlobalId(), std::move(*action)));\n"
         "}\n"
         "\n"
         "void RulesetMatcherBase::InheritAllowlistedFrames(\n",
         "DNR: RulesetMatcherBase::OnReadyToCommitNavigation")
    edit(src, d + "ruleset_matcher.h",
         "  void InheritAllowlistedFrames(const RulesetMatcher& other);\n",
         "  void InheritAllowlistedFrames(const RulesetMatcher& other);\n" + nav,
         "DNR: RulesetMatcher::OnReadyToCommitNavigation declaration")
    edit(src, d + "ruleset_matcher.cc",
         "void RulesetMatcher::InheritAllowlistedFrames(const RulesetMatcher& other) {\n",
         "void RulesetMatcher::OnReadyToCommitNavigation(\n"
         "    content::NavigationHandle* navigation_handle) {\n"
         "  url_matcher_.OnReadyToCommitNavigation(navigation_handle);\n"
         "  regex_matcher_.OnReadyToCommitNavigation(navigation_handle);\n"
         "}\n"
         "\n"
         "void RulesetMatcher::InheritAllowlistedFrames(const RulesetMatcher& other) {\n",
         "DNR: RulesetMatcher::OnReadyToCommitNavigation")
    for rel, cls, member, nxt in (
            ("composite_matcher", "CompositeMatcher", "matchers_",
             "  // Returns if this extension has any rulesets"),
            ("ruleset_manager", "RulesetManager", "rulesets_",
             "  // Returns if there are any matchers")):
        edit(src, d + rel + ".h",
             "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
             "\n" + nxt,
             "  void OnDidFinishNavigation(content::NavigationHandle* navigation_handle);\n"
             "  // " + MARK + ".\n" + nav + "\n" + nxt,
             "DNR: %s::OnReadyToCommitNavigation declaration" % cls)
        loop = ("  for (auto& matcher : matchers_) {\n"
                "    matcher->OnReadyToCommitNavigation(navigation_handle);\n"
                "  }\n") if member == "matchers_" else (
                "  for (ExtensionRulesetData& ruleset : rulesets_) {\n"
                "    ruleset.matcher->OnReadyToCommitNavigation(navigation_handle);\n"
                "  }\n")
        edit(src, d + rel + ".cc",
             "void %s::OnDidFinishNavigation(\n" % cls,
             "void %s::OnReadyToCommitNavigation(\n"
             "    content::NavigationHandle* navigation_handle) {\n" % cls + loop +
             "}\n"
             "\n"
             "void %s::OnDidFinishNavigation(\n" % cls,
             "DNR: %s::OnReadyToCommitNavigation" % cls)
    edit(src, d + "web_contents_helper.h",
         "  void DidFinishNavigation(\n"
         "      content::NavigationHandle* navigation_handle) override;\n",
         "  void DidFinishNavigation(\n"
         "      content::NavigationHandle* navigation_handle) override;\n"
         "  // " + MARK + ".\n"
         "  void ReadyToCommitNavigation(\n"
         "      content::NavigationHandle* navigation_handle) override;\n",
         "DNR: WebContentsHelper::ReadyToCommitNavigation declaration")
    edit(src, d + "web_contents_helper.cc",
         "void WebContentsHelper::DidFinishNavigation(\n",
         "void WebContentsHelper::ReadyToCommitNavigation(\n"
         "    content::NavigationHandle* navigation_handle) {\n"
         "  DCHECK(ruleset_manager_);\n"
         "  ruleset_manager_->OnReadyToCommitNavigation(navigation_handle);\n"
         "}\n"
         "\n"
         "void WebContentsHelper::DidFinishNavigation(\n",
         "DNR: WebContentsHelper::ReadyToCommitNavigation")


def main_parts(src):
    rel = "content/shell/browser/shell_browser_main_parts.cc"
    add_include(src, rel,
                "#include \"content/shell/browser/shell_browser_main_parts.h\"\n",
                "main parts include")
    edit(src, rel,
         "  InitializeBrowserContexts();\n"
         "  Shell::Initialize(CreateShellPlatformDelegate());\n",
         "#if defined(SHELL_USE_BEAPI)\n"
         "  // " + MARK + ": extensions need their clients and keyed-service\n"
         "  // factories before the contexts' services are created.\n"
         "  extensions::ShellExtensions::PreBrowserContexts();\n"
         "#endif\n"
         "  InitializeBrowserContexts();\n"
         "#if defined(SHELL_USE_BEAPI)\n"
         "  extensions::ShellExtensions::BrowserContextsCreated(\n"
         "      browser_context_.get());\n"
         "#endif\n"
         "  Shell::Initialize(CreateShellPlatformDelegate());\n",
         "main parts extension start")
    edit(src, rel,
         "  browser_context_.reset();\n"
         "  off_the_record_browser_context_.reset();\n",
         "#if defined(SHELL_USE_BEAPI)\n"
         "  extensions::ShellExtensions::WillDestroyBrowserContexts();\n"
         "#endif\n"
         "  browser_context_.reset();\n"
         "  off_the_record_browser_context_.reset();\n"
         "#if defined(SHELL_USE_BEAPI)\n"
         "  extensions::ShellExtensions::BrowserContextsDestroyed();\n"
         "#endif\n",
         "main parts extension shutdown")


def browser_context(src):
    rel = "content/shell/browser/shell_browser_context.cc"
    add_include(src, rel,
                "#include \"content/shell/browser/shell_browser_context.h\"\n",
                "browser context include")
    edit(src, rel,
         "  if (!delay_services_creation) {\n"
         "    BrowserContextDependencyManager::GetInstance()\n"
         "        ->CreateBrowserContextServices(this);\n",
         "#if defined(SHELL_USE_BEAPI)\n"
         "  // " + MARK + ": a PrefService for the extension services.\n"
         "  extensions::ShellExtensions::OnBrowserContextCreating(this);\n"
         "#endif\n"
         "  if (!delay_services_creation) {\n"
         "    BrowserContextDependencyManager::GetInstance()\n"
         "        ->CreateBrowserContextServices(this);\n",
         "browser context prefs")


BROWSER_CLIENT_DECLS = """\
#if defined(SHELL_USE_BEAPI)
  // %s: extensions. The bodies call extensions::ShellExtensions.
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  CreateNonNetworkNavigationURLLoaderFactory(
      const std::string& scheme,
      FrameTreeNodeId frame_tree_node_id) override;
  void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id,
      int render_frame_id,
      const std::optional<url::Origin>& request_initiator_origin,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
      BrowserContext* browser_context,
      const std::optional<url::Origin>& request_initiator,
      network::mojom::RequestDestination request_destination,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
      BrowserContext* browser_context,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void WillCreateURLLoaderFactory(
      BrowserContext* browser_context,
      RenderFrameHost* frame,
      int render_process_id,
      URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      const net::IsolationInfo& isolation_info,
      std::optional<int64_t> navigation_id,
      ukm::SourceIdObj ukm_source_id,
      network::URLLoaderFactoryBuilder& factory_builder,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client,
      bool* bypass_redirect_checks,
      bool* disable_secure_dns,
      network::mojom::URLLoaderFactoryOverridePtr* factory_override,
      scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner,
      bool is_for_network_service) override;
  void SiteInstanceGotProcessAndSite(SiteInstance* site_instance) override;
  bool ShouldUseProcessPerSite(
      BrowserContext* browser_context,
      const SecurityPrincipal& security_principal) override;
  bool ShouldUseSpareRenderProcessHost(
      BrowserContext* browser_context,
      const GURL& site_url,
      std::optional<SpareProcessRefusedByEmbedderReason>& refused_reason)
      override;
  bool DoesSiteRequireDedicatedProcess(BrowserContext* browser_context,
                                       const GURL& effective_site_url) override;
  bool IsSuitableHost(RenderProcessHost* process_host,
                      const SecurityPrincipal& security_principal) override;
  bool CanCommitURL(RenderProcessHost* process_host, const GURL& url) override;
  void RegisterAssociatedInterfaceBindersForServiceWorker(
      const ServiceWorkerVersionBaseInfo& service_worker_version_info,
      blink::AssociatedInterfaceRegistry& associated_registry) override;
#endif  // defined(SHELL_USE_BEAPI)
""" % MARK

BROWSER_CLIENT_DEFS = """\
#if defined(SHELL_USE_BEAPI)
// %s: extensions; see browser/extensions/shell_extensions.cc.

mojo::PendingRemote<network::mojom::URLLoaderFactory>
ShellContentBrowserClient::CreateNonNetworkNavigationURLLoaderFactory(
    const std::string& scheme,
    FrameTreeNodeId frame_tree_node_id) {
  return extensions::ShellExtensions::CreateNonNetworkNavigationURLLoaderFactory(
      scheme, frame_tree_node_id);
}

void ShellContentBrowserClient::RegisterNonNetworkSubresourceURLLoaderFactories(
    int render_process_id,
    int render_frame_id,
    const std::optional<url::Origin>& request_initiator_origin,
    NonNetworkURLLoaderFactoryMap* factories) {
  extensions::ShellExtensions::RegisterNonNetworkSubresourceURLLoaderFactories(
      render_process_id, render_frame_id, factories);
}

void ShellContentBrowserClient::
    RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
        BrowserContext* browser_context,
        const std::optional<url::Origin>& request_initiator,
        network::mojom::RequestDestination request_destination,
        NonNetworkURLLoaderFactoryMap* factories) {
  extensions::ShellExtensions::
      RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
          browser_context, request_initiator, factories);
}

void ShellContentBrowserClient::
    RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
        BrowserContext* browser_context,
        NonNetworkURLLoaderFactoryMap* factories) {
  extensions::ShellExtensions::
      RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(browser_context,
                                                              factories);
}

void ShellContentBrowserClient::WillCreateURLLoaderFactory(
    BrowserContext* browser_context,
    RenderFrameHost* frame,
    int render_process_id,
    URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    std::optional<int64_t> navigation_id,
    ukm::SourceIdObj ukm_source_id,
    network::URLLoaderFactoryBuilder& factory_builder,
    mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
        header_client,
    bool* bypass_redirect_checks,
    bool* disable_secure_dns,
    network::mojom::URLLoaderFactoryOverridePtr* factory_override,
    scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner,
    bool is_for_network_service) {
  const bool proxied = extensions::ShellExtensions::MaybeProxyURLLoaderFactory(
      browser_context, frame, render_process_id, type, request_initiator,
      std::move(navigation_id), ukm_source_id, factory_builder, header_client,
      std::move(navigation_response_task_runner));
  if (bypass_redirect_checks) {
    *bypass_redirect_checks = proxied;
  }
}

void ShellContentBrowserClient::SiteInstanceGotProcessAndSite(
    SiteInstance* site_instance) {
  extensions::ShellExtensions::SiteInstanceGotProcessAndSite(site_instance);
}

bool ShellContentBrowserClient::ShouldUseProcessPerSite(
    BrowserContext* browser_context,
    const SecurityPrincipal& security_principal) {
  return extensions::ShellExtensions::ShouldUseProcessPerSite(
             browser_context, security_principal) ||
         ContentBrowserClient::ShouldUseProcessPerSite(browser_context,
                                                       security_principal);
}

bool ShellContentBrowserClient::ShouldUseSpareRenderProcessHost(
    BrowserContext* browser_context,
    const GURL& site_url,
    std::optional<SpareProcessRefusedByEmbedderReason>& refused_reason) {
  if (!extensions::ShellExtensions::ShouldUseSpareRenderProcessHost(
          browser_context, site_url)) {
    refused_reason = SpareProcessRefusedByEmbedderReason::ExtensionProcess;
    return false;
  }
  return ContentBrowserClient::ShouldUseSpareRenderProcessHost(
      browser_context, site_url, refused_reason);
}

bool ShellContentBrowserClient::DoesSiteRequireDedicatedProcess(
    BrowserContext* browser_context,
    const GURL& effective_site_url) {
  return extensions::ShellExtensions::DoesSiteRequireDedicatedProcess(
             browser_context, effective_site_url) ||
         ContentBrowserClient::DoesSiteRequireDedicatedProcess(
             browser_context, effective_site_url);
}

bool ShellContentBrowserClient::IsSuitableHost(
    RenderProcessHost* process_host,
    const SecurityPrincipal& security_principal) {
  return extensions::ShellExtensions::IsSuitableHost(process_host,
                                                     security_principal) &&
         ContentBrowserClient::IsSuitableHost(process_host,
                                              security_principal);
}

bool ShellContentBrowserClient::CanCommitURL(RenderProcessHost* process_host,
                                             const GURL& url) {
  return extensions::ShellExtensions::CanCommitURL(process_host, url) &&
         ContentBrowserClient::CanCommitURL(process_host, url);
}

void ShellContentBrowserClient::
    RegisterAssociatedInterfaceBindersForServiceWorker(
        const ServiceWorkerVersionBaseInfo& service_worker_version_info,
        blink::AssociatedInterfaceRegistry& associated_registry) {
  extensions::ShellExtensions::
      RegisterAssociatedInterfaceBindersForServiceWorker(
          service_worker_version_info, associated_registry);
}
#endif  // defined(SHELL_USE_BEAPI)

""" % MARK


def browser_client(src):
    rel = "content/shell/browser/shell_content_browser_client.h"
    anchor = ("  void CreateThrottlesForNavigation(\n"
              "      NavigationThrottleRegistry& registry) override;\n")
    edit(src, rel, anchor, anchor + BROWSER_CLIENT_DECLS,
         "browser client declarations")

    rel = "content/shell/browser/shell_content_browser_client.cc"
    add_include(src, rel,
                "#include \"content/shell/browser/shell_content_browser_client.h\"\n",
                "browser client include")
    edit(src, rel,
         "}  // namespace content\n",
         BROWSER_CLIENT_DEFS + "}  // namespace content\n",
         "browser client definitions")

    def call(body):
        return ("#if defined(SHELL_USE_BEAPI)\n" + body +
                "#endif  // defined(SHELL_USE_BEAPI)\n")

    edit(src, rel,
         "  if (create_throttles_for_navigation_callback_) {\n"
         "    create_throttles_for_navigation_callback_.Run(registry);\n"
         "  }\n"
         "}\n",
         "  if (create_throttles_for_navigation_callback_) {\n"
         "    create_throttles_for_navigation_callback_.Run(registry);\n"
         "  }\n" +
         call("  extensions::ShellExtensions::CreateThrottlesForNavigation("
              "registry);\n") +
         "}\n",
         "browser client navigation throttles")
    edit(src, rel,
         "      .ExposeInterfacesToRendererProcess(registry, render_process_host);\n"
         "}\n",
         "      .ExposeInterfacesToRendererProcess(registry, render_process_host);\n" +
         call("  extensions::ShellExtensions::ExposeInterfacesToRenderer(\n"
              "      registry, associated_registry, render_process_host);\n") +
         "}\n",
         "browser client renderer interfaces")
    edit(src, rel,
         "        RenderFrameHost&,\n"
         "        blink::AssociatedInterfaceRegistry& associated_registry) {\n",
         "        RenderFrameHost& render_frame_host,\n"
         "        blink::AssociatedInterfaceRegistry& associated_registry) {\n" +
         call("  extensions::ShellExtensions::"
              "RegisterAssociatedInterfaceBindersForRenderFrameHost(\n"
              "      render_frame_host, associated_registry);\n"),
         "browser client frame associated interfaces")
    edit(src, rel,
         "  map->Add<network_hints::mojom::NetworkHintsHandler>(&BindNetworkHintsHandler);\n",
         "  map->Add<network_hints::mojom::NetworkHintsHandler>(&BindNetworkHintsHandler);\n" +
         call("  extensions::ShellExtensions::"
              "RegisterBrowserInterfaceBindersForFrame(render_frame_host,\n"
              "                                                       map);\n"),
         "browser client frame binders")
    edit(src, rel,
         "  command_line->CopySwitchesFrom(*base::CommandLine::ForCurrentProcess(),\n"
         "                                 kForwardSwitches);\n",
         "  command_line->CopySwitchesFrom(*base::CommandLine::ForCurrentProcess(),\n"
         "                                 kForwardSwitches);\n" +
         call("  extensions::ShellExtensions::"
              "AppendExtraRendererCommandLineSwitches(command_line,\n"
              "                                                      "
              "child_process_id);\n"),
         "browser client renderer switches")
    edit(src, rel,
         "        factory_params, origin, is_for_isolated_world, is_for_service_worker);\n"
         "  }\n"
         "}\n",
         "        factory_params, origin, is_for_isolated_world, is_for_service_worker);\n"
         "  }\n" +
         call("  extensions::ShellExtensions::OverrideURLLoaderFactoryParams(\n"
              "      browser_context, origin, is_for_isolated_world,\n"
              "      is_for_service_worker, factory_params);\n") +
         "}\n",
         "browser client URL loader factory params")


def content_client(src):
    rel = "content/shell/common/shell_content_client.cc"
    edit(src, rel,
         "#include \"content/shell/common/shell_content_client.h\"\n",
         "#include \"content/shell/common/shell_content_client.h\"\n"
         "\n#if defined(SHELL_USE_BEAPI)\n"
         "#include \"extensions/common/constants.h\"\n"
         "#endif\n",
         "content client include")
    edit(src, rel,
         "void ShellContentClient::AddAdditionalSchemes(Schemes* schemes) {\n",
         "void ShellContentClient::AddAdditionalSchemes(Schemes* schemes) {\n"
         "#if defined(SHELL_USE_BEAPI)\n"
         "  // " + MARK + ": chrome-extension://, registered as\n"
         "  // ChromeContentClient does.\n"
         "  schemes->standard_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->extension_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->savable_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->secure_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->service_worker_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->cors_enabled_schemes.push_back(extensions::kExtensionScheme);\n"
         "  schemes->csp_bypassing_schemes.push_back(extensions::kExtensionScheme);\n"
         "#endif  // defined(SHELL_USE_BEAPI)\n",
         "content client extension scheme")


RENDERER_CLIENT_DECLS = """\
#if defined(SHELL_USE_BEAPI)
  // %s: forwarded to the extensions renderer client.
  void WebViewCreated(blink::WebView* web_view,
                      bool was_created_by_renderer,
                      const url::Origin* outermost_origin) override;
  void RunScriptsAtDocumentStart(RenderFrame* render_frame) override;
  void RunScriptsAtDocumentEnd(RenderFrame* render_frame) override;
  void RunScriptsAtDocumentIdle(RenderFrame* render_frame) override;
  void WillSendRequest(blink::WebLocalFrame* frame,
                       ui::PageTransition transition_type,
                       const blink::WebURL& upstream_url,
                       const blink::WebURL& target_url,
                       const net::SiteForCookies& site_for_cookies,
                       const url::Origin* initiator_origin,
                       GURL* new_url) override;
  bool AllowPopup() override;
  void DidInitializeServiceWorkerContextOnWorkerThread(
      blink::WebServiceWorkerContextProxy* context_proxy,
      const GURL& service_worker_scope,
      const GURL& script_url) override;
  void WillEvaluateServiceWorkerOnWorkerThread(
      blink::WebServiceWorkerContextProxy* context_proxy,
      v8::Local<v8::Context> v8_context,
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url,
      const blink::ServiceWorkerToken& service_worker_token) override;
  void DidStartServiceWorkerContextOnWorkerThread(
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url,
      const blink::ServiceWorkerToken& service_worker_token) override;
  void WillDestroyServiceWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context,
      int64_t service_worker_version_id,
      const GURL& service_worker_scope,
      const GURL& script_url,
      const blink::ServiceWorkerToken& service_worker_token) override;
#endif  // defined(SHELL_USE_BEAPI)
""" % MARK

RENDERER_CLIENT_DEFS = """\
#if defined(SHELL_USE_BEAPI)
// %s: extensions.

void ShellContentRendererClient::WebViewCreated(
    blink::WebView* web_view,
    bool was_created_by_renderer,
    const url::Origin* outermost_origin) {
  extensions::ExtensionsRendererClient::Get()->WebViewCreated(
      web_view, outermost_origin);
}

void ShellContentRendererClient::RunScriptsAtDocumentStart(
    RenderFrame* render_frame) {
  extensions::ExtensionsRendererClient::Get()->RunScriptsAtDocumentStart(
      render_frame);
}

void ShellContentRendererClient::RunScriptsAtDocumentEnd(
    RenderFrame* render_frame) {
  extensions::ExtensionsRendererClient::Get()->RunScriptsAtDocumentEnd(
      render_frame);
}

void ShellContentRendererClient::RunScriptsAtDocumentIdle(
    RenderFrame* render_frame) {
  extensions::ExtensionsRendererClient::Get()->RunScriptsAtDocumentIdle(
      render_frame);
}

void ShellContentRendererClient::WillSendRequest(
    blink::WebLocalFrame* frame,
    ui::PageTransition transition_type,
    const blink::WebURL& upstream_url,
    const blink::WebURL& target_url,
    const net::SiteForCookies& site_for_cookies,
    const url::Origin* initiator_origin,
    GURL* new_url) {
  extensions::ExtensionsRendererClient::Get()->WillSendRequest(
      frame, transition_type, upstream_url, target_url, site_for_cookies,
      initiator_origin, new_url);
}

bool ShellContentRendererClient::AllowPopup() {
  return extensions::ExtensionsRendererClient::Get()->AllowPopup();
}

void ShellContentRendererClient::
    DidInitializeServiceWorkerContextOnWorkerThread(
        blink::WebServiceWorkerContextProxy* context_proxy,
        const GURL& service_worker_scope,
        const GURL& script_url) {
  extensions::ExtensionsRendererClient::Get()
      ->dispatcher()
      ->DidInitializeServiceWorkerContextOnWorkerThread(
          context_proxy, service_worker_scope, script_url);
}

void ShellContentRendererClient::WillEvaluateServiceWorkerOnWorkerThread(
    blink::WebServiceWorkerContextProxy* context_proxy,
    v8::Local<v8::Context> v8_context,
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url,
    const blink::ServiceWorkerToken& service_worker_token) {
  extensions::ExtensionsRendererClient::Get()
      ->dispatcher()
      ->WillEvaluateServiceWorkerOnWorkerThread(
          context_proxy, v8_context, service_worker_version_id,
          service_worker_scope, script_url, service_worker_token);
}

void ShellContentRendererClient::DidStartServiceWorkerContextOnWorkerThread(
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url,
    const blink::ServiceWorkerToken& service_worker_token) {
  extensions::ExtensionsRendererClient::Get()
      ->dispatcher()
      ->DidStartServiceWorkerContextOnWorkerThread(
          service_worker_version_id, service_worker_scope, script_url,
          service_worker_token);
}

void ShellContentRendererClient::WillDestroyServiceWorkerContextOnWorkerThread(
    v8::Local<v8::Context> context,
    int64_t service_worker_version_id,
    const GURL& service_worker_scope,
    const GURL& script_url,
    const blink::ServiceWorkerToken& service_worker_token) {
  extensions::ExtensionsRendererClient::Get()
      ->dispatcher()
      ->WillDestroyServiceWorkerContextOnWorkerThread(
          context, service_worker_version_id, service_worker_scope,
          script_url, service_worker_token);
}
#endif  // defined(SHELL_USE_BEAPI)

""" % MARK


def renderer_client(src):
    rel = "content/shell/renderer/shell_content_renderer_client.h"
    anchor = "  void RenderFrameCreated(RenderFrame* render_frame) override;\n"
    edit(src, rel, anchor, anchor + RENDERER_CLIENT_DECLS,
         "renderer client declarations")

    rel = "content/shell/renderer/shell_content_renderer_client.cc"
    edit(src, rel,
         "#include \"content/shell/renderer/shell_content_renderer_client.h\"\n",
         "#include \"content/shell/renderer/shell_content_renderer_client.h\"\n"
         "\n#if defined(SHELL_USE_BEAPI)\n"
         "#include \"content/shell/renderer/extensions/"
         "shell_extensions_renderer_client.h\"\n"
         "#include \"extensions/common/constants.h\"\n"
         "#include \"extensions/renderer/dispatcher.h\"\n"
         "#include \"third_party/blink/public/platform/web_string.h\"\n"
         "#include \"third_party/blink/public/web/web_security_policy.h\"\n"
         "#endif\n",
         "renderer client includes")
    edit(src, rel,
         "  web_cache_impl_ = std::make_unique<web_cache::WebCacheImpl>();\n"
         "}\n",
         "  web_cache_impl_ = std::make_unique<web_cache::WebCacheImpl>();\n"
         "#if defined(SHELL_USE_BEAPI)\n"
         "  // " + MARK + ": the extension Dispatcher.\n"
         "  extensions::ShellExtensionsRendererClient::Create();\n"
         "  extensions::ExtensionsRendererClient::Get()->RenderThreadStarted();\n"
         "  blink::WebSecurityPolicy::RegisterURLSchemeAsExtension(\n"
         "      blink::WebString::FromAscii(extensions::kExtensionScheme));\n"
         "#endif\n"
         "}\n",
         "renderer client thread start")
    edit(src, rel,
         "  new ShellRenderFrameObserver(render_frame);\n"
         "}\n",
         "  new ShellRenderFrameObserver(render_frame);\n"
         "#if defined(SHELL_USE_BEAPI)\n"
         "  extensions::ShellExtensionsRendererClient::OnRenderFrameCreated(\n"
         "      render_frame);\n"
         "#endif\n"
         "}\n",
         "renderer client frame created")
    edit(src, rel,
         "}  // namespace content\n",
         RENDERER_CLIENT_DEFS + "}  // namespace content\n",
         "renderer client definitions")


def main(src):
    build_gn(src)
    components_build_gn(src)
    extensions_renderer_build_gn(src)
    security_interstitials(src)
    extension_commands(src)
    system_cpu_build_gn(src)
    dnr_keep_allowlisted_frames(src)
    dnr_allowlist_at_ready_to_commit(src)
    main_parts(src)
    browser_context(src)
    browser_client(src)
    content_client(src)
    renderer_client(src)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: %s <chromium/src>" % sys.argv[0])
    main(sys.argv[1])
