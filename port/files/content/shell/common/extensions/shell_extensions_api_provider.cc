// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/common/extensions/shell_extensions_api_provider.h"

#include "content/shell/common/extensions/api/api_features.h"
#include "content/shell/common/extensions/api/generated_schemas.h"
#include "content/shell/common/extensions/api/permission_features.h"
#include "extensions/common/alias.h"
#include "extensions/common/mojom/api_permission_id.mojom-shared.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_info.h"

namespace extensions {

namespace {

using mojom::APIPermissionID;

// As chrome/common/extensions/permissions/chrome_api_permissions.cc
// registers them.
constexpr APIPermissionInfo::InitInfo kPermissionsToRegister[] = {
    {APIPermissionID::kCookie, "cookies"},
    {APIPermissionID::kScripting, "scripting",
     APIPermissionInfo::kFlagRequiresManagementUIWarning},
    {APIPermissionID::kTab, "tabs",
     APIPermissionInfo::kFlagRequiresManagementUIWarning},
    {APIPermissionID::kWebNavigation, "webNavigation",
     APIPermissionInfo::kFlagRequiresManagementUIWarning},
};

constexpr Alias kPermissionAliases[] = {Alias("windows", "tabs")};

}  // namespace

ShellExtensionsAPIProvider::ShellExtensionsAPIProvider() = default;
ShellExtensionsAPIProvider::~ShellExtensionsAPIProvider() = default;

void ShellExtensionsAPIProvider::AddAPIFeatures(FeatureProvider* provider) {
  AddShellAPIFeatures(provider);
}

void ShellExtensionsAPIProvider::AddManifestFeatures(
    FeatureProvider* provider) {
  // "action" and the other manifest keys the target extensions use are core.
}

void ShellExtensionsAPIProvider::AddPermissionFeatures(
    FeatureProvider* provider) {
  AddShellPermissionFeatures(provider);
}

void ShellExtensionsAPIProvider::AddBehaviorFeatures(
    FeatureProvider* provider) {}

void ShellExtensionsAPIProvider::AddAPIJSONSources(
    JSONFeatureProviderSource* json_source) {
  // Only ExtensionsClient::CreateAPIFeatureSource reads these, and nothing
  // calls it; the compiled features above are what is used.
}

bool ShellExtensionsAPIProvider::IsAPISchemaGenerated(
    const std::string& name) {
  return api::ShellGeneratedSchemas::IsGenerated(name);
}

std::string_view ShellExtensionsAPIProvider::GetAPISchema(
    const std::string& name) {
  return api::ShellGeneratedSchemas::Get(name);
}

void ShellExtensionsAPIProvider::RegisterPermissions(
    PermissionsInfo* permissions_info) {
  permissions_info->RegisterPermissions(kPermissionsToRegister,
                                        kPermissionAliases);
}

void ShellExtensionsAPIProvider::RegisterManifestHandlers(
    ManifestHandlerRegistry* registry) {
  // The "action" handler (ExtensionActionHandler) is registered by core.
}

}  // namespace extensions
