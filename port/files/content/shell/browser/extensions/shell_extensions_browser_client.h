// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_CLIENT_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "extensions/browser/extensions_browser_client.h"

namespace custom_handlers {
class ProtocolHandlerRegistry;
}

namespace extensions {

class ExtensionCache;
class ExtensionManagementClient;
class KioskDelegate;
class SafeBrowsingDelegate;

// R Chromium: content_shell's ExtensionsBrowserClient. One regular browser
// context, where extensions run, and content_shell's off-the-record context,
// where they do not (no incognito support). No kiosk, demo mode, Web Store
// or updater.
class ShellExtensionsBrowserClient : public ExtensionsBrowserClient {
 public:
  ShellExtensionsBrowserClient();
  ShellExtensionsBrowserClient(const ShellExtensionsBrowserClient&) = delete;
  ShellExtensionsBrowserClient& operator=(const ShellExtensionsBrowserClient&) =
      delete;
  ~ShellExtensionsBrowserClient() override;

  void SetContexts(content::BrowserContext* main_context,
                   content::BrowserContext* off_the_record_context);
  void set_shutting_down() { shutting_down_ = true; }
  content::BrowserContext* main_context() const { return main_context_; }
  // After the browser contexts are gone, before their prefs.
  void DestroyProtocolHandlerRegistry();

  // ExtensionsBrowserClient:
  void Init() override;
  bool IsShuttingDown() override;
  bool AreExtensionsDisabled(const base::CommandLine& command_line,
                             content::BrowserContext* context) override;
  bool IsValidContext(void* context) override;
  bool IsSameContext(content::BrowserContext* first,
                     content::BrowserContext* second) override;
  bool HasOffTheRecordContext(content::BrowserContext* context) override;
  content::BrowserContext* GetOffTheRecordContext(
      content::BrowserContext* context) override;
  content::BrowserContext* GetOriginalContext(
      content::BrowserContext* context) override;
  content::BrowserContext* GetContextRedirectedToOriginal(
      content::BrowserContext* context) override;
  content::BrowserContext* GetContextRedirectedToOriginalWithoutAshInternals(
      content::BrowserContext* context) override;
  content::BrowserContext* GetContextOwnInstance(
      content::BrowserContext* context) override;
  content::BrowserContext* GetContextForOriginalOnly(
      content::BrowserContext* context) override;
  bool AreExtensionsDisabledForContext(
      content::BrowserContext* context) override;
#if BUILDFLAG(IS_CHROMEOS)
  bool IsActiveContext(content::BrowserContext* browser_context) const override;
#endif
  bool IsGuestSession(content::BrowserContext* context) const override;
  bool IsExtensionIncognitoEnabled(
      const ExtensionId& extension_id,
      content::BrowserContext* context) const override;
  bool IsExtensionIncognitoEnabled(
      const Extension* extension,
      content::BrowserContext* context) const override;
  bool CanExtensionCrossIncognito(
      const Extension* extension,
      content::BrowserContext* context) const override;
  base::FilePath GetBundleResourcePath(
      const network::ResourceRequest& request,
      const base::FilePath& extension_resources_path,
      int* resource_id) const override;
  void LoadResourceFromResourceBundle(
      const network::ResourceRequest& request,
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      const base::FilePath& resource_relative_path,
      int resource_id,
      scoped_refptr<net::HttpResponseHeaders> headers,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      content::BrowserContext* browser_context) override;
  bool AllowCrossRendererResourceLoad(
      const network::ResourceRequest& request,
      network::mojom::RequestDestination destination,
      ui::PageTransition page_transition,
      content::ChildProcessId child_id,
      bool is_incognito,
      const Extension* extension,
      const ExtensionSet& extensions,
      const ProcessMap& process_map,
      const GURL& upstream_url) override;
  void GetEarlyExtensionPrefsObservers(
      content::BrowserContext* context,
      std::vector<EarlyExtensionPrefsObserver*>* observers) const override;
  ProcessManagerDelegate* GetProcessManagerDelegate() const override;
  mojo::PendingRemote<network::mojom::URLLoaderFactory>
  GetControlledFrameEmbedderURLLoader(
      const url::Origin& app_origin,
      content::FrameTreeNodeId frame_tree_node_id,
      content::BrowserContext* browser_context) override;
  std::unique_ptr<ExtensionHostDelegate> CreateExtensionHostDelegate() override;
  bool DidVersionUpdate(content::BrowserContext* context) override;
  void PermitExternalProtocolHandler() override;
  bool IsInDemoMode() override;
  bool IsScreensaverInDemoMode(const std::string& app_id) override;
  bool IsRunningInForcedAppMode() override;
  bool IsAppModeForcedForApp(const ExtensionId& extension_id) override;
  bool IsLoggedInAsPublicAccount() override;
  ExtensionSystemProvider* GetExtensionSystemFactory() override;
  void RegisterBrowserInterfaceBindersForFrame(
      mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map,
      content::RenderFrameHost* render_frame_host,
      const Extension* extension) const override;
  std::unique_ptr<RuntimeAPIDelegate> CreateRuntimeAPIDelegate(
      content::BrowserContext* context) const override;
  const ComponentExtensionResourceManager*
  GetComponentExtensionResourceManager() override;
  void BroadcastEventToRenderers(
      events::HistogramValue histogram_value,
      const std::string& event_name,
      base::ListValue args,
      bool dispatch_to_off_the_record_profiles) override;
  ExtensionCache* GetExtensionCache() override;
  bool IsBackgroundUpdateAllowed() override;
  bool IsMinBrowserVersionSupported(const std::string& min_version) override;
  void CreateExtensionWebContentsObserver(
      content::WebContents* web_contents) override;
  ExtensionWebContentsObserver* GetExtensionWebContentsObserver(
      content::WebContents* web_contents) override;
  KioskDelegate* GetKioskDelegate() override;
  SafeBrowsingDelegate* GetSafeBrowsingDelegate() override;
  std::string GetApplicationLocale() override;
  ExtensionManagementClient* GetExtensionManagementClient(
      content::BrowserContext* context) override;
  custom_handlers::ProtocolHandlerRegistry* GetProtocolHandlerRegistry(
      content::BrowserContext* context) override;
  void GetTabAndWindowIdForWebContents(content::WebContents* web_contents,
                                       int* tab_id,
                                       int* window_id) override;
  bool IsValidTabId(content::BrowserContext* browser_context,
                    int tab_id,
                    bool include_incognito,
                    content::WebContents** web_contents) const override;
  ScriptExecutor* GetScriptExecutorForTab(
      content::WebContents& web_contents) override;

 private:
  raw_ptr<content::BrowserContext> main_context_ = nullptr;
  raw_ptr<content::BrowserContext> off_the_record_context_ = nullptr;
  bool shutting_down_ = false;

  std::unique_ptr<ExtensionCache> extension_cache_;
  std::unique_ptr<KioskDelegate> kiosk_delegate_;
  std::unique_ptr<SafeBrowsingDelegate> safe_browsing_delegate_;
  std::unique_ptr<ExtensionManagementClient> extension_management_client_;
  // navigator.registerProtocolHandler and extension protocol handlers,
  // for the main context; created on first use.
  std::unique_ptr<custom_handlers::ProtocolHandlerRegistry>
      protocol_handler_registry_;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_CLIENT_H_
