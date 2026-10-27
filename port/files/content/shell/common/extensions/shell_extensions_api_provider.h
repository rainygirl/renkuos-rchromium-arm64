// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_API_PROVIDER_H_
#define CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_API_PROVIDER_H_

#include <string>
#include <string_view>

#include "extensions/common/extensions_api_provider.h"

namespace extensions {

// R Chromium: features, schemas and permissions of the //chrome extension
// APIs content_shell implements itself (tabs, action, webNavigation,
// cookies, extension). Everything else comes from CoreExtensionsAPIProvider.
class ShellExtensionsAPIProvider : public ExtensionsAPIProvider {
 public:
  ShellExtensionsAPIProvider();
  ShellExtensionsAPIProvider(const ShellExtensionsAPIProvider&) = delete;
  ShellExtensionsAPIProvider& operator=(const ShellExtensionsAPIProvider&) =
      delete;
  ~ShellExtensionsAPIProvider() override;

  // ExtensionsAPIProvider:
  void AddAPIFeatures(FeatureProvider* provider) override;
  void AddManifestFeatures(FeatureProvider* provider) override;
  void AddPermissionFeatures(FeatureProvider* provider) override;
  void AddBehaviorFeatures(FeatureProvider* provider) override;
  void AddAPIJSONSources(JSONFeatureProviderSource* json_source) override;
  bool IsAPISchemaGenerated(const std::string& name) override;
  std::string_view GetAPISchema(const std::string& name) override;
  void RegisterPermissions(PermissionsInfo* permissions_info) override;
  void RegisterManifestHandlers(ManifestHandlerRegistry* registry) override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_COMMON_EXTENSIONS_SHELL_EXTENSIONS_API_PROVIDER_H_
