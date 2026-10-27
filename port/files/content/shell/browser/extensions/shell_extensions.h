// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_H_

#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "content/public/browser/content_browser_client.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/url_loader_factory.mojom-forward.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

class GURL;

namespace base {
class CommandLine;
class SequencedTaskRunner;
}  // namespace base

namespace blink {
class AssociatedInterfaceRegistry;
}

namespace content {
class BrowserContext;
class NavigationThrottleRegistry;
class RenderFrameHost;
class RenderProcessHost;
class SecurityPrincipal;
class SiteInstance;
class WebContents;
struct ServiceWorkerVersionBaseInfo;
}  // namespace content

namespace network {
class URLLoaderFactoryBuilder;
namespace mojom {
class TrustedURLLoaderHeaderClient;
class URLLoaderFactoryParams;
}  // namespace mojom
}  // namespace network

namespace url {
class Origin;
}

namespace extensions {

// R Chromium: the one place content_shell calls into for extensions. The
// lifecycle calls come from ShellBrowserMainParts and ShellBrowserContext;
// the rest are the bodies of ShellContentBrowserClient overrides, after what
// ChromeContentBrowserClient(ExtensionsPart) does, minus hosted apps,
// <webview> and Chrome's own services. See docs/EXTENSIONS.md.
class ShellExtensions {
 public:
  ShellExtensions() = delete;

  // --- Lifecycle, in call order ---

  // Before any browser context exists: installs the ExtensionsClient and
  // ExtensionsBrowserClient and registers the keyed-service factories.
  static void PreBrowserContexts();
  // From ShellBrowserContext's constructor, before its keyed services are
  // created: gives the context a PrefService and tells the browser client
  // about it.
  static void OnBrowserContextCreating(content::BrowserContext* context);
  // Both contexts exist: starts the extension system and loads the
  // extensions named by --load-extension.
  static void BrowserContextsCreated(content::BrowserContext* main_context);
  // Before the contexts are destroyed, and after.
  static void WillDestroyBrowserContexts();
  static void BrowserContextsDestroyed();

  // A Shell window's WebContents was created.
  static void WebContentsCreated(content::WebContents* web_contents);

  // The native toolbar: a window that has one appeared (showing `tab`) or
  // went away, and a click on an extension's button in the window showing
  // `tab` (`anchor` in screen coordinates).
  static void ToolbarWindowCreated(gfx::AcceleratedWidget widget,
                                   content::WebContents* tab);
  static void ToolbarWindowDestroyed(gfx::AcceleratedWidget widget);
  static void ToolbarActionClicked(content::WebContents* tab,
                                   const std::string& extension_id,
                                   const gfx::Rect& anchor);

  // --- ShellContentBrowserClient hooks ---

  static mojo::PendingRemote<network::mojom::URLLoaderFactory>
  CreateNonNetworkNavigationURLLoaderFactory(
      const std::string& scheme,
      content::FrameTreeNodeId frame_tree_node_id);
  static void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id,
      int render_frame_id,
      content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories);
  static void RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
      content::BrowserContext* browser_context,
      const std::optional<url::Origin>& request_initiator,
      content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories);
  static void RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
      content::BrowserContext* browser_context,
      content::ContentBrowserClient::NonNetworkURLLoaderFactoryMap* factories);
  // The WebRequest proxy, which is also where declarativeNetRequest rules
  // are applied. Returns whether the request was proxied.
  static bool MaybeProxyURLLoaderFactory(
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
      scoped_refptr<base::SequencedTaskRunner>
          navigation_response_task_runner);
  static void OverrideURLLoaderFactoryParams(
      content::BrowserContext* browser_context,
      const url::Origin& origin,
      bool is_for_isolated_world,
      bool is_for_service_worker,
      network::mojom::URLLoaderFactoryParams* factory_params);

  static void SiteInstanceGotProcessAndSite(content::SiteInstance* site_instance);
  static bool ShouldUseProcessPerSite(
      content::BrowserContext* browser_context,
      const content::SecurityPrincipal& security_principal);
  static bool ShouldUseSpareRenderProcessHost(
      content::BrowserContext* browser_context,
      const GURL& site_url);
  static bool DoesSiteRequireDedicatedProcess(
      content::BrowserContext* browser_context,
      const GURL& effective_site_url);
  static bool IsSuitableHost(
      content::RenderProcessHost* process_host,
      const content::SecurityPrincipal& security_principal);
  static bool CanCommitURL(content::RenderProcessHost* process_host,
                           const GURL& url);
  static void AppendExtraRendererCommandLineSwitches(
      base::CommandLine* command_line,
      int child_process_id);

  static void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* render_process_host);
  static void RegisterAssociatedInterfaceBindersForRenderFrameHost(
      content::RenderFrameHost& render_frame_host,
      blink::AssociatedInterfaceRegistry& associated_registry);
  static void RegisterAssociatedInterfaceBindersForServiceWorker(
      const content::ServiceWorkerVersionBaseInfo& service_worker_version_info,
      blink::AssociatedInterfaceRegistry& associated_registry);
  static void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map);

  static void CreateThrottlesForNavigation(
      content::NavigationThrottleRegistry& registry);
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_H_
