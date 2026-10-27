// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extensions_browser_client.h"

#include <utility>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "build/build_config.h"
#include "components/custom_handlers/protocol_handler_registry.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents.h"
#include "content/shell/browser/extensions/shell_extension_system.h"
#include "content/shell/browser/extensions/shell_extension_web_contents_observer.h"
#include "content/shell/browser/extensions/shell_extensions_browser_api_provider.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "extensions/browser/api/core_extensions_browser_api_provider.h"
#include "extensions/browser/api/runtime/runtime_api_delegate.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_host_delegate.h"
#include "extensions/browser/extension_management_client.h"
#include "extensions/browser/extensions_browser_interface_binders.h"
#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "extensions/browser/managed_installation_mode.h"
#include "extensions/browser/safe_browsing_delegate.h"
#include "extensions/browser/updater/null_extension_cache.h"
#include "extensions/browser/url_request_util.h"
#include "extensions/common/api/runtime.h"
#include "extensions/common/switches.h"
#include "extensions/common/url_pattern_set.h"
#include "net/http/http_response_headers.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

namespace {

// No enterprise policy: everything is allowed, nothing is forced.
class ShellExtensionManagementClient : public ExtensionManagementClient {
 public:
  bool UpdatesFromWebstore(const Extension& extension) override {
    return false;
  }
  bool IsInstallationExplicitlyAllowed(const ExtensionId& id) override {
    return false;
  }
  bool IsForceInstalledInLowTrustEnvironment(
      const Extension& extension) override {
    return false;
  }
  const URLPatternSet& GetPolicyBlockedHosts(
      const Extension* extension) override {
    return URLPatternSet::Empty();
  }
  const URLPatternSet& GetPolicyAllowedHosts(
      const Extension* extension) override {
    return URLPatternSet::Empty();
  }
  bool UsesDefaultPolicyHostRestrictions(const Extension* extension) override {
    return true;
  }
  bool BlocklistedByDefault() const override { return false; }
  GURL GetEffectiveUpdateURL(const Extension& extension) override {
    return GURL();
  }
  bool IsAllowedManifestType(Manifest::Type manifest_type,
                             const std::string& extension_id) const override {
    return true;
  }
  ManagedInstallationMode GetInstallationMode(
      const Extension* extension) override {
    return ManagedInstallationMode::kAllowed;
  }
  ManagedInstallationMode GetInstallationMode(
      const ExtensionId& extension_id,
      const std::string& update_url) override {
    return ManagedInstallationMode::kAllowed;
  }
  const std::string BlockedInstallMessage(const ExtensionId& id) override {
    return std::string();
  }
  bool IsPermissionSetAllowed(const Extension* extension,
                              const PermissionSet& perms) override {
    return true;
  }
  bool IsPermissionSetAllowed(const ExtensionId& extension_id,
                              const std::string& update_url,
                              const PermissionSet& perms) override {
    return true;
  }
  bool IsInstallationExplicitlyBlocked(const ExtensionId& id) override {
    return false;
  }
};

class ShellKioskDelegate : public KioskDelegate {
 public:
  bool IsAutoLaunchedKioskApp(const ExtensionId& id) const override {
    return false;
  }
};

// chrome.runtime's embedder hooks: no updater, no device restart.
class ShellRuntimeAPIDelegate : public RuntimeAPIDelegate {
 public:
  void AddUpdateObserver(UpdateObserver* observer) override {}
  void RemoveUpdateObserver(UpdateObserver* observer) override {}
  void ReloadExtension(const ExtensionId& extension_id) override {
    NOTIMPLEMENTED_LOG_ONCE();
  }
  bool CheckForUpdates(const ExtensionId& extension_id,
                       UpdateCheckCallback callback) override {
    return false;
  }
  void OpenURL(const GURL& uninstall_url) override {}
  bool GetPlatformInfo(api::runtime::PlatformInfo* info) override {
    info->os = api::runtime::PlatformOs::kLinux;
#if defined(ARCH_CPU_ARM64)
    info->arch = api::runtime::PlatformArch::kArm64;
    info->nacl_arch = api::runtime::PlatformNaclArch::kArm;
#elif defined(ARCH_CPU_X86_64)
    info->arch = api::runtime::PlatformArch::kX86_64;
    info->nacl_arch = api::runtime::PlatformNaclArch::kX86_64;
#endif
    return true;
  }
  bool RestartDevice(std::string* error_message) override {
    *error_message = "Restart is not supported.";
    return false;
  }
};

// No OS integration: handlers apply inside the browser only.
class ShellProtocolHandlerRegistryDelegate
    : public custom_handlers::ProtocolHandlerRegistry::Delegate {
 public:
  void RegisterExternalHandler(const std::string& protocol) override {}
  bool IsExternalHandlerRegistered(const std::string& protocol) override {
    return false;
  }
  void RegisterWithOSAsDefaultClient(
      const std::string& protocol,
      DefaultClientCallback callback) override {
    std::move(callback).Run(false);
  }
  void CheckDefaultClientWithOS(
      const std::string& protocol,
      DefaultClientCallback callback) override {
    std::move(callback).Run(false);
  }
  bool ShouldRemoveHandlersNotInOS() override { return false; }
};

// For ExtensionHosts (background pages, offscreen documents). MV3
// extensions run a service worker instead, so this rarely does anything.
class ShellExtensionHostDelegate : public ExtensionHostDelegate {
 public:
  void OnExtensionHostCreated(content::WebContents* web_contents) override {
    ShellExtensionWebContentsObserver::CreateForWebContents(web_contents);
  }
  void CreateTab(std::unique_ptr<content::WebContents> web_contents,
                 const GURL& target_url,
                 const ExtensionId& extension_id,
                 WindowOpenDisposition disposition,
                 const blink::mojom::WindowFeatures& window_features,
                 bool user_gesture) override {
    LOG(WARNING) << "[RCH] extension " << extension_id
                 << " tried to open a tab; not supported yet";
  }
  void ProcessMediaAccessRequest(content::WebContents* web_contents,
                                 const content::MediaStreamRequest& request,
                                 content::MediaResponseCallback callback,
                                 const Extension* extension) override {
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED,
        /*ui=*/nullptr);
  }
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const url::Origin& security_origin,
                                  blink::mojom::MediaStreamType type,
                                  const Extension* extension) override {
    return false;
  }
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override {
    return content::PictureInPictureResult::kNotSupported;
  }
  void ExitPictureInPicture() override {}
};

}  // namespace

ShellExtensionsBrowserClient::ShellExtensionsBrowserClient()
    : extension_cache_(std::make_unique<NullExtensionCache>()),
      kiosk_delegate_(std::make_unique<ShellKioskDelegate>()),
      safe_browsing_delegate_(std::make_unique<SafeBrowsingDelegate>()),
      extension_management_client_(
          std::make_unique<ShellExtensionManagementClient>()) {
  AddAPIProvider(std::make_unique<CoreExtensionsBrowserAPIProvider>());
  AddAPIProvider(std::make_unique<ShellExtensionsBrowserAPIProvider>());
}

ShellExtensionsBrowserClient::~ShellExtensionsBrowserClient() = default;

void ShellExtensionsBrowserClient::SetContexts(
    content::BrowserContext* main_context,
    content::BrowserContext* off_the_record_context) {
  DCHECK(!main_context->IsOffTheRecord());
  DCHECK(!off_the_record_context || off_the_record_context->IsOffTheRecord());
  main_context_ = main_context;
  off_the_record_context_ = off_the_record_context;
}

void ShellExtensionsBrowserClient::Init() {}

bool ShellExtensionsBrowserClient::IsShuttingDown() {
  return shutting_down_;
}

bool ShellExtensionsBrowserClient::AreExtensionsDisabled(
    const base::CommandLine& command_line,
    content::BrowserContext* context) {
  return command_line.HasSwitch(switches::kDisableExtensions);
}

bool ShellExtensionsBrowserClient::IsValidContext(void* context) {
  return context && (context == main_context_.get() ||
                     context == off_the_record_context_.get());
}

bool ShellExtensionsBrowserClient::IsSameContext(
    content::BrowserContext* first,
    content::BrowserContext* second) {
  return first == second ||
         (IsValidContext(first) && IsValidContext(second));
}

bool ShellExtensionsBrowserClient::HasOffTheRecordContext(
    content::BrowserContext* context) {
  return context == main_context_ && off_the_record_context_;
}

content::BrowserContext* ShellExtensionsBrowserClient::GetOffTheRecordContext(
    content::BrowserContext* context) {
  return context == main_context_ ? off_the_record_context_.get() : nullptr;
}

content::BrowserContext* ShellExtensionsBrowserClient::GetOriginalContext(
    content::BrowserContext* context) {
  return context && context->IsOffTheRecord() ? main_context_.get() : context;
}

content::BrowserContext*
ShellExtensionsBrowserClient::GetContextRedirectedToOriginal(
    content::BrowserContext* context) {
  return GetOriginalContext(context);
}

content::BrowserContext*
ShellExtensionsBrowserClient::GetContextRedirectedToOriginalWithoutAshInternals(
    content::BrowserContext* context) {
  return GetOriginalContext(context);
}

content::BrowserContext* ShellExtensionsBrowserClient::GetContextOwnInstance(
    content::BrowserContext* context) {
  return context;
}

content::BrowserContext*
ShellExtensionsBrowserClient::GetContextForOriginalOnly(
    content::BrowserContext* context) {
  return context && context->IsOffTheRecord() ? nullptr : context;
}

bool ShellExtensionsBrowserClient::AreExtensionsDisabledForContext(
    content::BrowserContext* context) {
  return false;
}

#if BUILDFLAG(IS_CHROMEOS)
bool ShellExtensionsBrowserClient::IsActiveContext(
    content::BrowserContext* browser_context) const {
  return true;
}
#endif

bool ShellExtensionsBrowserClient::IsGuestSession(
    content::BrowserContext* context) const {
  return false;
}

bool ShellExtensionsBrowserClient::IsExtensionIncognitoEnabled(
    const ExtensionId& extension_id,
    content::BrowserContext* context) const {
  return false;
}

bool ShellExtensionsBrowserClient::IsExtensionIncognitoEnabled(
    const Extension* extension,
    content::BrowserContext* context) const {
  return false;
}

bool ShellExtensionsBrowserClient::CanExtensionCrossIncognito(
    const Extension* extension,
    content::BrowserContext* context) const {
  return false;
}

base::FilePath ShellExtensionsBrowserClient::GetBundleResourcePath(
    const network::ResourceRequest& request,
    const base::FilePath& extension_resources_path,
    int* resource_id) const {
  // No component extensions ship resources in the .pak.
  *resource_id = 0;
  return base::FilePath();
}

void ShellExtensionsBrowserClient::LoadResourceFromResourceBundle(
    const network::ResourceRequest& request,
    mojo::PendingReceiver<network::mojom::URLLoader> loader,
    const base::FilePath& resource_relative_path,
    int resource_id,
    scoped_refptr<net::HttpResponseHeaders> headers,
    mojo::PendingRemote<network::mojom::URLLoaderClient> client,
    content::BrowserContext* browser_context) {
  NOTREACHED() << "No resources come from the bundle";
}

bool ShellExtensionsBrowserClient::AllowCrossRendererResourceLoad(
    const network::ResourceRequest& request,
    network::mojom::RequestDestination destination,
    ui::PageTransition page_transition,
    content::ChildProcessId child_id,
    bool is_incognito,
    const Extension* extension,
    const ExtensionSet& extensions,
    const ProcessMap& process_map,
    const GURL& upstream_url) {
  bool allowed = false;
  if (url_request_util::AllowCrossRendererResourceLoad(
          request, destination, page_transition, child_id, is_incognito,
          extension, extensions, process_map, upstream_url, &allowed)) {
    return allowed;
  }
  // Undecided: block, as Chrome does.
  return false;
}

void ShellExtensionsBrowserClient::GetEarlyExtensionPrefsObservers(
    content::BrowserContext* context,
    std::vector<EarlyExtensionPrefsObserver*>* observers) const {}

ProcessManagerDelegate* ShellExtensionsBrowserClient::GetProcessManagerDelegate()
    const {
  return nullptr;
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
ShellExtensionsBrowserClient::GetControlledFrameEmbedderURLLoader(
    const url::Origin& app_origin,
    content::FrameTreeNodeId frame_tree_node_id,
    content::BrowserContext* browser_context) {
  return mojo::PendingRemote<network::mojom::URLLoaderFactory>();
}

std::unique_ptr<ExtensionHostDelegate>
ShellExtensionsBrowserClient::CreateExtensionHostDelegate() {
  return std::make_unique<ShellExtensionHostDelegate>();
}

bool ShellExtensionsBrowserClient::DidVersionUpdate(
    content::BrowserContext* context) {
  return false;
}

void ShellExtensionsBrowserClient::PermitExternalProtocolHandler() {}

bool ShellExtensionsBrowserClient::IsInDemoMode() {
  return false;
}

bool ShellExtensionsBrowserClient::IsScreensaverInDemoMode(
    const std::string& app_id) {
  return false;
}

bool ShellExtensionsBrowserClient::IsRunningInForcedAppMode() {
  return false;
}

bool ShellExtensionsBrowserClient::IsAppModeForcedForApp(
    const ExtensionId& extension_id) {
  return false;
}

bool ShellExtensionsBrowserClient::IsLoggedInAsPublicAccount() {
  return false;
}

ExtensionSystemProvider*
ShellExtensionsBrowserClient::GetExtensionSystemFactory() {
  return ShellExtensionSystemFactory::GetInstance();
}

void ShellExtensionsBrowserClient::RegisterBrowserInterfaceBindersForFrame(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map,
    content::RenderFrameHost* render_frame_host,
    const Extension* extension) const {
  PopulateExtensionFrameBinders(binder_map, render_frame_host, extension);
}

std::unique_ptr<RuntimeAPIDelegate>
ShellExtensionsBrowserClient::CreateRuntimeAPIDelegate(
    content::BrowserContext* context) const {
  return std::make_unique<ShellRuntimeAPIDelegate>();
}

const ComponentExtensionResourceManager*
ShellExtensionsBrowserClient::GetComponentExtensionResourceManager() {
  return nullptr;
}

void ShellExtensionsBrowserClient::BroadcastEventToRenderers(
    events::HistogramValue histogram_value,
    const std::string& event_name,
    base::ListValue args,
    bool dispatch_to_off_the_record_profiles) {
  if (!main_context_) {
    return;
  }
  EventRouter::Get(main_context_)
      ->BroadcastEvent(std::make_unique<Event>(histogram_value, event_name,
                                               std::move(args)));
}

ExtensionCache* ShellExtensionsBrowserClient::GetExtensionCache() {
  return extension_cache_.get();
}

bool ShellExtensionsBrowserClient::IsBackgroundUpdateAllowed() {
  return false;
}

bool ShellExtensionsBrowserClient::IsMinBrowserVersionSupported(
    const std::string& min_version) {
  return true;
}

void ShellExtensionsBrowserClient::CreateExtensionWebContentsObserver(
    content::WebContents* web_contents) {
  ShellExtensionWebContentsObserver::CreateForWebContents(web_contents);
}

ExtensionWebContentsObserver*
ShellExtensionsBrowserClient::GetExtensionWebContentsObserver(
    content::WebContents* web_contents) {
  return ShellExtensionWebContentsObserver::FromWebContents(web_contents);
}

KioskDelegate* ShellExtensionsBrowserClient::GetKioskDelegate() {
  return kiosk_delegate_.get();
}

SafeBrowsingDelegate* ShellExtensionsBrowserClient::GetSafeBrowsingDelegate() {
  return safe_browsing_delegate_.get();
}

std::string ShellExtensionsBrowserClient::GetApplicationLocale() {
  return l10n_util::GetApplicationLocale(std::string());
}

ExtensionManagementClient*
ShellExtensionsBrowserClient::GetExtensionManagementClient(
    content::BrowserContext* context) {
  return extension_management_client_.get();
}

custom_handlers::ProtocolHandlerRegistry*
ShellExtensionsBrowserClient::GetProtocolHandlerRegistry(
    content::BrowserContext* context) {
  if (!main_context_ || shutting_down_ ||
      GetOriginalContext(context) != main_context_) {
    return nullptr;
  }
  if (!protocol_handler_registry_) {
    protocol_handler_registry_ = custom_handlers::ProtocolHandlerRegistry::Create(
        user_prefs::UserPrefs::Get(main_context_),
        std::make_unique<ShellProtocolHandlerRegistryDelegate>());
  }
  return protocol_handler_registry_.get();
}

void ShellExtensionsBrowserClient::GetTabAndWindowIdForWebContents(
    content::WebContents* web_contents,
    int* tab_id,
    int* window_id) {
  *tab_id = ShellTabs::GetTabId(web_contents);
  *window_id = ShellTabs::GetWindowId(web_contents);
}

bool ShellExtensionsBrowserClient::IsValidTabId(
    content::BrowserContext* browser_context,
    int tab_id,
    bool include_incognito,
    content::WebContents** web_contents) const {
  content::WebContents* tab = ShellTabs::GetTabById(browser_context, tab_id);
  if (web_contents) {
    *web_contents = tab;
  }
  return tab;
}

ScriptExecutor* ShellExtensionsBrowserClient::GetScriptExecutorForTab(
    content::WebContents& web_contents) {
  return ShellTabs::GetScriptExecutor(&web_contents);
}

void ShellExtensionsBrowserClient::DestroyProtocolHandlerRegistry() {
  if (protocol_handler_registry_) {
    protocol_handler_registry_->Shutdown();
    protocol_handler_registry_.reset();
  }
}

}  // namespace extensions
