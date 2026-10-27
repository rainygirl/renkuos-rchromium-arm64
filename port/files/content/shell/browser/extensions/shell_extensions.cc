// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extensions.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <utility>

#include "base/barrier_closure.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_split.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/custom_handlers/protocol_handler_registry.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/in_memory_pref_store.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/pref_service_factory.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/shell/browser/extensions/shell_action_toolbar.h"
#include "content/shell/browser/extensions/shell_cookies_api.h"
#include "content/shell/browser/extensions/shell_extension_system.h"
#include "content/shell/browser/extensions/shell_extension_web_contents_observer.h"
#include "content/shell/browser/extensions/shell_extensions_api_client.h"
#include "content/shell/browser/extensions/shell_extensions_browser_client.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "content/shell/common/extensions/shell_extensions_client.h"
#include "extensions/browser/api/audio/audio_api.h"
#include "extensions/browser/api/declarative_net_request/rules_monitor_service.h"
#include "extensions/browser/api/declarative_net_request/ruleset_manager.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "extensions/browser/api/runtime/runtime_api.h"
#include "extensions/browser/api/web_request/extension_web_request_event_router.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/browser_context_keyed_service_factories.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_navigation_throttle.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_protocols.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_web_contents_observer.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/browser/process_map.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/browser/service_worker/service_worker_host.h"
#include "extensions/browser/url_loader_factory_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/sandboxed_page_info.h"
#include "extensions/common/mojom/event_router.mojom.h"
#include "extensions/common/mojom/frame.mojom.h"
#include "extensions/common/mojom/renderer_host.mojom.h"
#include "extensions/common/mojom/service_worker_host.mojom.h"
#include "extensions/common/mojom/web_request_host.mojom.h"
#include "extensions/common/switches.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace extensions {

namespace {

struct State {
  std::unique_ptr<ShellExtensionsBrowserClient> browser_client;
  std::unique_ptr<ShellExtensionsAPIClient> api_client;
  std::unique_ptr<ShellActionToolbar> toolbar;
  std::unique_ptr<ShellCookiesEventRouter> cookies_event_router;
  std::map<content::BrowserContext*, std::unique_ptr<PrefService>> prefs;

  // Startup: the extensions found at startup load asynchronously, and their
  // declarativeNetRequest rules after them. Navigations wait for both (see
  // StartupNavigationThrottle).
  bool startup_loaded = false;
  std::vector<ExtensionId> startup_extensions;
  std::vector<base::OnceClosure> startup_waiters;
  base::TimeTicks startup_deadline;
  base::RepeatingTimer startup_rules_timer;
};

State* g_state = nullptr;

// How long navigations wait at most for the startup extensions, so a broken
// extension cannot keep every page from loading.
constexpr base::TimeDelta kStartupWait = base::Seconds(10);

void StartupLoaded() {
  if (!g_state || g_state->startup_loaded) {
    return;
  }
  g_state->startup_loaded = true;
  g_state->startup_rules_timer.Stop();
  for (base::OnceClosure& waiter : g_state->startup_waiters) {
    std::move(waiter).Run();
  }
  g_state->startup_waiters.clear();
}

// Whether every startup extension that can have declarativeNetRequest rules
// has them in the RulesetManager. Rules load after the extension, on another
// sequence; an extension without enabled rulesets never gets a matcher, so
// the deadline ends the wait for those.
bool StartupRulesLoaded(content::BrowserContext* context) {
  auto* rules_monitor =
      declarative_net_request::RulesMonitorService::Get(context);
  if (!rules_monitor) {
    return true;
  }
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  for (const ExtensionId& id : g_state->startup_extensions) {
    const Extension* extension = registry->enabled_extensions().GetByID(id);
    if (extension && declarative_net_request::HasAnyDNRPermission(*extension) &&
        !rules_monitor->ruleset_manager()->GetMatcherForExtension(id)) {
      return false;
    }
  }
  return true;
}

void StartupExtensionsLoaded(content::BrowserContext* context) {
  if (!g_state || g_state->startup_loaded) {
    return;
  }
  if (StartupRulesLoaded(context)) {
    StartupLoaded();
    return;
  }
  g_state->startup_rules_timer.Start(
      FROM_HERE, base::Milliseconds(50),
      base::BindRepeating(
          [](content::BrowserContext* context) {
            if (StartupRulesLoaded(context) ||
                base::TimeTicks::Now() >= g_state->startup_deadline) {
              StartupLoaded();
            }
          },
          context));
}

// Defers a navigation's network request until the extensions present at
// startup are loaded, with their declarativeNetRequest rules. Chrome loads
// installed extensions before its first window; R Chromium loads them
// asynchronously, so without this the start page loads unfiltered, without
// content scripts -- and when an extension that can modify requests finishes
// loading, Chromium resets every URLLoaderFactory, which leaves a navigation
// already under way hanging for good if a rule modifies its request headers.
class StartupNavigationThrottle : public content::NavigationThrottle {
 public:
  explicit StartupNavigationThrottle(
      content::NavigationThrottleRegistry& registry)
      : content::NavigationThrottle(registry) {}
  StartupNavigationThrottle(const StartupNavigationThrottle&) = delete;
  StartupNavigationThrottle& operator=(const StartupNavigationThrottle&) =
      delete;
  ~StartupNavigationThrottle() override = default;

  ThrottleCheckResult WillStartRequest() override {
    if (!g_state || g_state->startup_loaded) {
      return PROCEED;
    }
    g_state->startup_waiters.push_back(base::BindOnce(
        &StartupNavigationThrottle::Resume, weak_factory_.GetWeakPtr()));
    return DEFER;
  }

  const char* GetNameForLogging() override {
    return "StartupNavigationThrottle";
  }

 private:
  base::WeakPtrFactory<StartupNavigationThrottle> weak_factory_{this};
};

bool Enabled() {
  return g_state && g_state->browser_client->main_context() &&
         !g_state->browser_client->IsShuttingDown();
}

bool EnabledFor(content::BrowserContext* context) {
  return Enabled() && !context->IsOffTheRecord();
}

const Extension* GetEnabledExtensionFromSecurityPrincipal(
    content::BrowserContext* context,
    const content::SecurityPrincipal& principal) {
  if (!principal.SchemeIs(kExtensionScheme)) {
    return nullptr;
  }
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  return registry ? registry->enabled_extensions().GetByID(
                        ExtensionId(principal.GetHost()))
                  : nullptr;
}

std::unique_ptr<PrefService> CreatePrefService(
    content::BrowserContext* context) {
  auto registry = base::MakeRefCounted<user_prefs::PrefRegistrySyncable>();
  BrowserContextDependencyManager::GetInstance()
      ->RegisterProfilePrefsForServices(registry.get());
  // Registered centrally rather than by their factories; these are the
  // //extensions ones chrome/browser/prefs/browser_prefs.cc registers.
  ExtensionPrefs::RegisterProfilePrefs(registry.get());
  PermissionsManager::RegisterProfilePrefs(registry.get());
  RuntimeAPI::RegisterPrefs(registry.get());
  AudioAPI::RegisterUserPrefs(registry.get());
  custom_handlers::ProtocolHandlerRegistry::RegisterProfilePrefs(registry.get());

  PrefServiceFactory factory;
  if (context->IsOffTheRecord()) {
    factory.set_user_prefs(base::MakeRefCounted<InMemoryPrefStore>());
  } else {
    factory.set_user_prefs(base::MakeRefCounted<JsonPrefStore>(
        context->GetPath().AppendASCII("Preferences")));
  }
  // The profile's preferences are read synchronously, during startup, before
  // the services that read them exist. content_shell is a testonly target.
  base::ScopedAllowBlockingForTesting allow_blocking;
  return factory.Create(registry);
}

// Every subfolder of `parent` with a manifest.json, sorted.
std::vector<base::FilePath> ExtensionsIn(const base::FilePath& parent) {
  std::vector<base::FilePath> result;
  base::FileEnumerator folders(parent, /*recursive=*/false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = folders.Next(); !dir.empty();
       dir = folders.Next()) {
    if (base::PathExists(dir.Append(kManifestFilename))) {
      result.push_back(dir);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// The manifest's "version", or empty.
std::string ManifestVersion(const base::FilePath& dir) {
  std::string json;
  if (!base::ReadFileToString(dir.Append(kManifestFilename), &json)) {
    return std::string();
  }
  std::optional<base::Value> manifest = base::JSONReader::Read(
      json, base::JSON_PARSE_CHROMIUM_EXTENSIONS | base::JSON_ALLOW_TRAILING_COMMAS);
  const std::string* version =
      manifest && manifest->is_dict() ? manifest->GetDict().FindString("version")
                                      : nullptr;
  return version ? *version : std::string();
}

// A bundled extension's copy in the profile, made or refreshed when the
// package ships a different version. Extensions are loaded from a writable
// copy because Chromium writes into an unpacked extension's folder (its
// indexed declarativeNetRequest rulesets), and the app folder is not.
base::FilePath CopyOfBundledExtension(const base::FilePath& bundled,
                                      const base::FilePath& copies) {
  const base::FilePath copy = copies.Append(bundled.BaseName());
  const std::string version = ManifestVersion(bundled);
  if (base::PathExists(copy) && ManifestVersion(copy) == version) {
    return copy;
  }
  base::DeletePathRecursively(copy);
  if (!base::CreateDirectory(copies) ||
      !base::CopyDirectory(bundled, copies, /*recursive=*/true)) {
    LOG(ERROR) << "[RCH] cannot copy bundled extension " << bundled.value()
               << " to " << copies.value();
    return base::FilePath();
  }
  return copy;
}

// The extensions to load, in order:
//  1. bundled with the browser: the "extensions" folder next to it
//     (/boot/system/apps/RChromium/extensions), each through its copy in
//     "Bundled Extensions" in the profile;
//  2. the user's: "Extensions" in the profile (~/config/settings/content_shell),
//     made on first run -- drop an unpacked extension's folder in;
//  3. --load-extension=<dir>[,<dir>...], as in Chrome.
std::vector<base::FilePath> ExtensionDirectories(
    content::BrowserContext* context) {
  // Startup, before any page loads; content_shell is a testonly target.
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::vector<base::FilePath> dirs;
  base::FilePath exe_dir;
  if (base::PathService::Get(base::DIR_EXE, &exe_dir)) {
    const base::FilePath copies =
        context->GetPath().AppendASCII("Bundled Extensions");
    for (const base::FilePath& bundled :
         ExtensionsIn(exe_dir.AppendASCII("extensions"))) {
      if (base::FilePath copy = CopyOfBundledExtension(bundled, copies);
          !copy.empty()) {
        dirs.push_back(copy);
      }
    }
  }
  const base::FilePath user_dir = context->GetPath().AppendASCII("Extensions");
  base::CreateDirectory(user_dir);
  std::ranges::move(ExtensionsIn(user_dir), std::back_inserter(dirs));
  for (const auto& dir : base::SplitString(
           base::CommandLine::ForCurrentProcess()->GetSwitchValueNative(
               switches::kLoadExtension),
           FILE_PATH_LITERAL(","), base::TRIM_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    dirs.emplace_back(dir);
  }
  return dirs;
}

}  // namespace

// static
void ShellExtensions::PreBrowserContexts() {
  DCHECK(!g_state);
  g_state = new State;
  ShellExtensionsClient::EnsureInitialized();
  g_state->browser_client = std::make_unique<ShellExtensionsBrowserClient>();
  ExtensionsBrowserClient::Set(g_state->browser_client.get());
  g_state->browser_client->Init();
  g_state->api_client = std::make_unique<ShellExtensionsAPIClient>();

  EnsureBrowserContextKeyedServiceFactoriesBuilt();
  EnsureExtensionURLLoaderFactoryShutdownNotifierFactoryBuilt();
  ShellExtensionSystemFactory::GetInstance();
}

// static
void ShellExtensions::OnBrowserContextCreating(
    content::BrowserContext* context) {
  if (!g_state) {
    return;
  }
  // Tell the browser client first: factories ask it which context to use
  // while the services are created.
  ShellExtensionsBrowserClient* client = g_state->browser_client.get();
  if (context->IsOffTheRecord()) {
    CHECK(client->main_context());
    client->SetContexts(client->main_context(), context);
  } else {
    client->SetContexts(context, nullptr);
  }

  std::unique_ptr<PrefService> prefs = CreatePrefService(context);
  user_prefs::UserPrefs::Set(context, prefs.get());
  g_state->prefs[context] = std::move(prefs);
}

// static
void ShellExtensions::BrowserContextsCreated(
    content::BrowserContext* main_context) {
  if (!g_state) {
    return;
  }
  auto* system = static_cast<ShellExtensionSystem*>(
      ExtensionSystem::Get(main_context));
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  const bool enabled = !g_state->browser_client->AreExtensionsDisabled(
      *command_line, main_context);
  system->InitForRegularProfile(enabled);
  if (!enabled) {
    StartupLoaded();
    return;
  }

  g_state->toolbar = std::make_unique<ShellActionToolbar>(main_context);
  g_state->cookies_event_router =
      std::make_unique<ShellCookiesEventRouter>(main_context);
  ShellTabs::SetTabChangedCallback(base::BindRepeating(
      [](content::WebContents* tab, bool navigated) {
        if (g_state && g_state->toolbar) {
          g_state->toolbar->OnTabChanged(tab, navigated);
        }
      }));

  const std::vector<base::FilePath> dirs = ExtensionDirectories(main_context);
  g_state->startup_deadline = base::TimeTicks::Now() + kStartupWait;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&StartupLoaded), kStartupWait);
  base::RepeatingClosure loaded = base::BarrierClosure(
      dirs.size(), base::BindOnce(&StartupExtensionsLoaded, main_context));
  for (const base::FilePath& dir : dirs) {
    system->LoadUnpacked(
        dir, base::BindOnce(
                 [](base::RepeatingClosure loaded, const ExtensionId& id) {
                   if (g_state && !id.empty()) {
                     g_state->startup_extensions.push_back(id);
                   }
                   loaded.Run();
                 },
                 loaded));
  }
}

// static
void ShellExtensions::WillDestroyBrowserContexts() {
  if (g_state) {
    ShellTabs::SetTabChangedCallback(ShellTabs::TabChangedCallback());
    g_state->toolbar.reset();
    g_state->cookies_event_router.reset();
    g_state->browser_client->set_shutting_down();
  }
}

// static
void ShellExtensions::BrowserContextsDestroyed() {
  if (!g_state) {
    return;
  }
  g_state->browser_client->DestroyProtocolHandlerRegistry();
  for (auto& [context, prefs] : g_state->prefs) {
    prefs->CommitPendingWrite();
  }
  g_state->prefs.clear();
  ExtensionsBrowserClient::Set(nullptr);
  delete g_state;
  g_state = nullptr;
}

// static
void ShellExtensions::WebContentsCreated(content::WebContents* web_contents) {
  if (EnabledFor(web_contents->GetBrowserContext())) {
    ShellExtensionWebContentsObserver::CreateForWebContents(web_contents);
    ShellTabs::AttachTab(web_contents);
  }
}

// static
void ShellExtensions::ToolbarWindowCreated(gfx::AcceleratedWidget widget,
                                           content::WebContents* tab) {
  if (g_state && g_state->toolbar && EnabledFor(tab->GetBrowserContext())) {
    g_state->toolbar->AddWindow(widget, tab);
  }
}

// static
void ShellExtensions::ToolbarWindowDestroyed(gfx::AcceleratedWidget widget) {
  if (g_state && g_state->toolbar) {
    g_state->toolbar->RemoveWindow(widget);
  }
}

// static
void ShellExtensions::ToolbarActionClicked(content::WebContents* tab,
                                           const std::string& extension_id,
                                           const gfx::Rect& anchor) {
  if (g_state && g_state->toolbar) {
    g_state->toolbar->OnActionClicked(tab, extension_id, anchor);
  }
}

// static
mojo::PendingRemote<network::mojom::URLLoaderFactory>
ShellExtensions::CreateNonNetworkNavigationURLLoaderFactory(
    const std::string& scheme,
    content::FrameTreeNodeId frame_tree_node_id) {
  if (scheme != kExtensionScheme || !Enabled()) {
    return {};
  }
  content::WebContents* web_contents =
      content::WebContents::FromFrameTreeNodeId(frame_tree_node_id);
  if (!web_contents) {
    return {};
  }
  return CreateExtensionNavigationURLLoaderFactory(
      web_contents->GetBrowserContext(), /*is_web_view_request=*/false);
}

// static
void ShellExtensions::RegisterNonNetworkSubresourceURLLoaderFactories(
    int render_process_id,
    int render_frame_id,
    content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories) {
  if (!Enabled()) {
    return;
  }
  factories->emplace(
      kExtensionScheme,
      CreateExtensionURLLoaderFactory(
          content::ChildProcessId::FromUnsafeValue(render_process_id),
          render_frame_id));
}

// static
void ShellExtensions::RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
    content::BrowserContext* browser_context,
    const std::optional<url::Origin>& request_initiator,
    content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories) {
  if (!EnabledFor(browser_context)) {
    return;
  }
  factories->emplace(kExtensionScheme,
                     CreateExtensionWorkerMainResourceURLLoaderFactory(
                         browser_context, request_initiator));
}

// static
void ShellExtensions::RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
    content::BrowserContext* browser_context,
    content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories) {
  if (!EnabledFor(browser_context)) {
    return;
  }
  factories->emplace(
      kExtensionScheme,
      CreateExtensionServiceWorkerScriptURLLoaderFactory(browser_context));
}

// static
bool ShellExtensions::MaybeProxyURLLoaderFactory(
    content::BrowserContext* browser_context,
    content::RenderFrameHost* frame,
    int render_process_id,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    std::optional<int64_t> navigation_id,
    ukm::SourceIdObj ukm_source_id,
    network::URLLoaderFactoryBuilder& factory_builder,
    mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
        header_client,
    scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner) {
  if (!Enabled()) {
    return false;
  }
  auto* web_request_api =
      BrowserContextKeyedAPIFactory<WebRequestAPI>::Get(browser_context);
  if (!web_request_api) {
    return false;
  }
  return web_request_api->MaybeProxyURLLoaderFactory(
      browser_context, frame, render_process_id, type, std::move(navigation_id),
      ukm_source_id, factory_builder, header_client,
      std::move(navigation_response_task_runner), request_initiator);
}

// static
void ShellExtensions::OverrideURLLoaderFactoryParams(
    content::BrowserContext* browser_context,
    const url::Origin& origin,
    bool is_for_isolated_world,
    bool is_for_service_worker,
    network::mojom::URLLoaderFactoryParams* factory_params) {
  if (!EnabledFor(browser_context)) {
    return;
  }
  URLLoaderFactoryManager::OverrideURLLoaderFactoryParams(
      browser_context, origin, is_for_isolated_world, is_for_service_worker,
      factory_params);
}

// static
void ShellExtensions::SiteInstanceGotProcessAndSite(
    content::SiteInstance* site_instance) {
  content::BrowserContext* context =
      site_instance->GetProcess()->GetBrowserContext();
  if (!EnabledFor(context)) {
    return;
  }
  const Extension* extension = GetEnabledExtensionFromSecurityPrincipal(
      context, site_instance->GetSecurityPrincipal());
  // Manifest-sandboxed pages get no extension APIs, so no privileged
  // process either.
  if (!extension || site_instance->GetSecurityPrincipal().IsSandboxed()) {
    return;
  }
  ProcessMap::Get(context)->Insert(extension->id(),
                                   site_instance->GetProcess()->GetID());
}

// static
bool ShellExtensions::ShouldUseProcessPerSite(
    content::BrowserContext* browser_context,
    const content::SecurityPrincipal& security_principal) {
  // All of an extension's pages share one process.
  return EnabledFor(browser_context) &&
         GetEnabledExtensionFromSecurityPrincipal(browser_context,
                                                  security_principal);
}

// static
bool ShellExtensions::ShouldUseSpareRenderProcessHost(
    content::BrowserContext* browser_context,
    const GURL& site_url) {
  // Extension processes need --extension-process at launch, which a spare
  // process started ahead of time lacks.
  return !site_url.SchemeIs(kExtensionScheme);
}

// static
bool ShellExtensions::DoesSiteRequireDedicatedProcess(
    content::BrowserContext* browser_context,
    const GURL& effective_site_url) {
  return EnabledFor(browser_context) &&
         ExtensionRegistry::Get(browser_context)
             ->enabled_extensions()
             .GetExtensionOrAppByURL(effective_site_url);
}

// static
bool ShellExtensions::IsSuitableHost(
    content::RenderProcessHost* process_host,
    const content::SecurityPrincipal& security_principal) {
  content::BrowserContext* context = process_host->GetBrowserContext();
  if (!EnabledFor(context)) {
    return true;
  }
  // Never mix an extension with web content, or with another extension.
  ProcessMap* process_map = ProcessMap::Get(context);
  const Extension* extension =
      GetEnabledExtensionFromSecurityPrincipal(context, security_principal);
  if (extension) {
    return process_map->Contains(extension->id(), process_host->GetID());
  }
  return !process_map->Contains(process_host->GetID());
}

// static
bool ShellExtensions::CanCommitURL(content::RenderProcessHost* process_host,
                                   const GURL& url) {
  content::BrowserContext* context = process_host->GetBrowserContext();
  if (!EnabledFor(context)) {
    return true;
  }
  const Extension* extension = ExtensionRegistry::Get(context)
                                   ->enabled_extensions()
                                   .GetExtensionOrAppByURL(url);
  if (!extension) {
    return true;
  }
  return ProcessMap::Get(context)->Contains(extension->id(),
                                            process_host->GetID()) ||
         SandboxedPageInfo::IsSandboxedPage(extension, url.GetPath());
}

// static
void ShellExtensions::AppendExtraRendererCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  if (command_line->GetSwitchValueASCII(::switches::kProcessType) !=
      ::switches::kRendererProcess) {
    return;
  }
  content::RenderProcessHost* process =
      content::RenderProcessHost::FromID(child_process_id);
  if (!process || !EnabledFor(process->GetBrowserContext())) {
    return;
  }
  if (ProcessMap::Get(process->GetBrowserContext())
          ->GetEnabledExtensionByProcessID(process->GetID())) {
    command_line->AppendSwitch(switches::kExtensionProcess);
  }
}

// static
void ShellExtensions::ExposeInterfacesToRenderer(
    service_manager::BinderRegistry* registry,
    blink::AssociatedInterfaceRegistry* associated_registry,
    content::RenderProcessHost* host) {
  if (!EnabledFor(host->GetBrowserContext())) {
    return;
  }
  associated_registry->AddInterface<mojom::RendererHost>(base::BindRepeating(
      &RendererStartupHelper::BindForRenderer, host->GetID()));
  associated_registry->AddInterface<mojom::WebRequestHost>(base::BindRepeating(
      &WebRequestEventRouter::BindForRenderer, host->GetID()));
}

// static
void ShellExtensions::RegisterAssociatedInterfaceBindersForRenderFrameHost(
    content::RenderFrameHost& render_frame_host,
    blink::AssociatedInterfaceRegistry& associated_registry) {
  if (!EnabledFor(render_frame_host.GetBrowserContext())) {
    return;
  }
  content::ChildProcessId render_process_id =
      render_frame_host.GetProcess()->GetID();
  associated_registry.AddInterface<mojom::RendererHost>(base::BindRepeating(
      &RendererStartupHelper::BindForRenderer, render_process_id));
  associated_registry.AddInterface<mojom::EventRouter>(
      base::BindRepeating(&EventRouter::BindForRenderer, render_process_id));
  associated_registry.AddInterface<mojom::LocalFrameHost>(base::BindRepeating(
      [](content::RenderFrameHost* render_frame_host,
         mojo::PendingAssociatedReceiver<mojom::LocalFrameHost> receiver) {
        ExtensionWebContentsObserver::BindLocalFrameHost(std::move(receiver),
                                                         render_frame_host);
      },
      &render_frame_host));
}

// static
void ShellExtensions::RegisterAssociatedInterfaceBindersForServiceWorker(
    const content::ServiceWorkerVersionBaseInfo& service_worker_version_info,
    blink::AssociatedInterfaceRegistry& associated_registry) {
  if (!Enabled()) {
    return;
  }
  CHECK(service_worker_version_info.process_id);
  const content::ChildProcessId process_id =
      service_worker_version_info.process_id;
  associated_registry.AddInterface<mojom::RendererHost>(base::BindRepeating(
      &RendererStartupHelper::BindForRenderer, process_id));
  associated_registry.AddInterface<mojom::ServiceWorkerHost>(
      base::BindRepeating(&ServiceWorkerHost::BindReceiver, process_id));
  associated_registry.AddInterface<mojom::WebRequestHost>(base::BindRepeating(
      &WebRequestEventRouter::BindForRenderer, process_id));
  associated_registry.AddInterface<mojom::EventRouter>(
      base::BindRepeating(&EventRouter::BindForRenderer, process_id));
}

// static
void ShellExtensions::RegisterBrowserInterfaceBindersForFrame(
    content::RenderFrameHost* render_frame_host,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
  content::BrowserContext* context = render_frame_host->GetBrowserContext();
  if (!EnabledFor(context)) {
    return;
  }
  const Extension* extension = GetEnabledExtensionFromSecurityPrincipal(
      context, render_frame_host->GetSiteInstance()->GetSecurityPrincipal());
  if (extension) {
    ExtensionsBrowserClient::Get()->RegisterBrowserInterfaceBindersForFrame(
        map, render_frame_host, extension);
  }
}

// static
void ShellExtensions::CreateThrottlesForNavigation(
    content::NavigationThrottleRegistry& registry) {
  if (!EnabledFor(
          registry.GetNavigationHandle().GetWebContents()->GetBrowserContext())) {
    return;
  }
  registry.AddThrottle(std::make_unique<ExtensionNavigationThrottle>(registry));
  registry.AddThrottle(std::make_unique<StartupNavigationThrottle>(registry));
}

}  // namespace extensions
