// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/common/extensions/shell_extensions_client.h"

#include <memory>

#include "base/no_destructor.h"
#include "content/shell/common/extensions/shell_extensions_api_provider.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/core_extensions_api_provider.h"
#include "extensions/common/permissions/permission_message.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/url_pattern_set.h"

namespace extensions {

ShellExtensionsClient::ShellExtensionsClient() {
  AddAPIProvider(std::make_unique<CoreExtensionsAPIProvider>());
  AddAPIProvider(std::make_unique<ShellExtensionsAPIProvider>());
}

ShellExtensionsClient::~ShellExtensionsClient() = default;

// static
void ShellExtensionsClient::EnsureInitialized() {
  // ExtensionsClient::Get() DCHECKs that a client is set, so it cannot be
  // asked whether one is.
  static base::NoDestructor<ShellExtensionsClient> client;
  static bool installed = false;
  if (!installed) {
    ExtensionsClient::Set(client.get());
    installed = true;
  }
}

void ShellExtensionsClient::Initialize() {}

void ShellExtensionsClient::InitializeWebStoreUrls(
    base::CommandLine* command_line) {}

const PermissionMessageProvider&
ShellExtensionsClient::GetPermissionMessageProvider() const {
  return permission_message_provider_;
}

const std::string ShellExtensionsClient::GetProductName() {
  return "R Chromium";
}

void ShellExtensionsClient::FilterHostPermissions(
    const URLPatternSet& hosts,
    URLPatternSet* new_hosts,
    PermissionIDSet* permissions) const {
  // No chrome:// hosts are special here; keep every pattern.
  for (const URLPattern& pattern : hosts) {
    new_hosts->AddPattern(pattern);
  }
}

void ShellExtensionsClient::SetScriptingAllowlist(
    const ScriptingAllowlist& allowlist) {
  scripting_allowlist_ = allowlist;
}

const ExtensionsClient::ScriptingAllowlist&
ShellExtensionsClient::GetScriptingAllowlist() const {
  return scripting_allowlist_;
}

URLPatternSet ShellExtensionsClient::GetPermittedChromeSchemeHosts(
    const Extension* extension,
    const APIPermissionSet& api_permissions) const {
  return URLPatternSet();
}

bool ShellExtensionsClient::IsScriptableURL(const GURL& url,
                                            std::string* error) const {
  return true;
}

const GURL& ShellExtensionsClient::GetWebstoreBaseURL() const {
  return webstore_base_url_;
}

const GURL& ShellExtensionsClient::GetNewWebstoreBaseURL() const {
  return webstore_base_url_;
}

const GURL& ShellExtensionsClient::GetWebstoreUpdateURL() const {
  return webstore_update_url_;
}

bool ShellExtensionsClient::IsBlocklistUpdateURL(const GURL& url) const {
  return false;
}

std::set<base::FilePath> ShellExtensionsClient::GetBrowserImagePaths(
    const Extension* extension) {
  std::set<base::FilePath> image_paths =
      ExtensionsClient::GetBrowserImagePaths(extension);
  // The toolbar draws the action's icon, so it is decoded at load time like
  // the extension's own icons.
  const ActionInfo* action = ActionInfo::GetExtensionActionInfo(extension);
  if (action && !action->default_icon.empty()) {
    action->default_icon.GetPaths(&image_paths);
  }
  return image_paths;
}

PermissionMessages
ShellExtensionsClient::NoWarningsPermissionMessageProvider::
    GetPermissionMessages(const PermissionIDSet& permissions) const {
  return PermissionMessages();
}

bool ShellExtensionsClient::NoWarningsPermissionMessageProvider::
    IsPrivilegeIncrease(const PermissionSet& granted_permissions,
                        const PermissionSet& requested_permissions,
                        Manifest::Type extension_type) const {
  return false;
}

PermissionIDSet
ShellExtensionsClient::NoWarningsPermissionMessageProvider::
    GetAllPermissionIDs(const PermissionSet& permissions,
                        Manifest::Type extension_type) const {
  PermissionIDSet ids;
  for (const APIPermission* api : permissions.apis()) {
    ids.InsertAll(api->GetPermissions());
  }
  return ids;
}

}  // namespace extensions
