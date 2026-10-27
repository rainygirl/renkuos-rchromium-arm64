// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_API_PROVIDER_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_API_PROVIDER_H_

#include "extensions/browser/extensions_browser_api_provider.h"

namespace extensions {

// R Chromium: registers the functions of the //chrome APIs content_shell
// implements (tabs, action, webNavigation, cookies, extension).
class ShellExtensionsBrowserAPIProvider : public ExtensionsBrowserAPIProvider {
 public:
  ShellExtensionsBrowserAPIProvider() = default;
  ShellExtensionsBrowserAPIProvider(const ShellExtensionsBrowserAPIProvider&) =
      delete;
  ShellExtensionsBrowserAPIProvider& operator=(
      const ShellExtensionsBrowserAPIProvider&) = delete;
  ~ShellExtensionsBrowserAPIProvider() override = default;

  // ExtensionsBrowserAPIProvider:
  void RegisterExtensionFunctions(ExtensionFunctionRegistry* registry) override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_BROWSER_API_PROVIDER_H_
