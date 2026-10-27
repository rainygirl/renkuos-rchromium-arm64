// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_CLIENT_H_
#define CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_CLIENT_H_

#include <set>
#include <string>

#include "extensions/common/extensions_client.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "url/gurl.h"

namespace extensions {

// R Chromium: content_shell's ExtensionsClient, shared by the browser and
// renderer processes. Core APIs plus ShellExtensionsAPIProvider; no Web Store
// and no permission warnings (nothing shows an install prompt).
class ShellExtensionsClient : public ExtensionsClient {
 public:
  ShellExtensionsClient();
  ShellExtensionsClient(const ShellExtensionsClient&) = delete;
  ShellExtensionsClient& operator=(const ShellExtensionsClient&) = delete;
  ~ShellExtensionsClient() override;

  // Creates the process-wide instance and installs it with
  // ExtensionsClient::Set(), once.
  static void EnsureInitialized();

  // ExtensionsClient:
  void Initialize() override;
  void InitializeWebStoreUrls(base::CommandLine* command_line) override;
  const PermissionMessageProvider& GetPermissionMessageProvider()
      const override;
  const std::string GetProductName() override;
  void FilterHostPermissions(const URLPatternSet& hosts,
                             URLPatternSet* new_hosts,
                             PermissionIDSet* permissions) const override;
  void SetScriptingAllowlist(const ScriptingAllowlist& allowlist) override;
  const ScriptingAllowlist& GetScriptingAllowlist() const override;
  URLPatternSet GetPermittedChromeSchemeHosts(
      const Extension* extension,
      const APIPermissionSet& api_permissions) const override;
  bool IsScriptableURL(const GURL& url, std::string* error) const override;
  const GURL& GetWebstoreBaseURL() const override;
  const GURL& GetNewWebstoreBaseURL() const override;
  const GURL& GetWebstoreUpdateURL() const override;
  bool IsBlocklistUpdateURL(const GURL& url) const override;
  std::set<base::FilePath> GetBrowserImagePaths(
      const Extension* extension) override;

 private:
  class NoWarningsPermissionMessageProvider : public PermissionMessageProvider {
   public:
    PermissionMessages GetPermissionMessages(
        const PermissionIDSet& permissions) const override;
    bool IsPrivilegeIncrease(const PermissionSet& granted_permissions,
                             const PermissionSet& requested_permissions,
                             Manifest::Type extension_type) const override;
    PermissionIDSet GetAllPermissionIDs(
        const PermissionSet& permissions,
        Manifest::Type extension_type) const override;
  };

  const NoWarningsPermissionMessageProvider permission_message_provider_;
  ScriptingAllowlist scripting_allowlist_;
  // Empty: there is no Web Store integration.
  const GURL webstore_base_url_;
  const GURL webstore_update_url_;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_CLIENT_H_
