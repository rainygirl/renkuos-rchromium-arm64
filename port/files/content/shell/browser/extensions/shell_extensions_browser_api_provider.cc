// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extensions_browser_api_provider.h"

#include "content/shell/browser/extensions/shell_action_api.h"
#include "content/shell/browser/extensions/shell_cookies_api.h"
#include "content/shell/browser/extensions/shell_tabs_api.h"
#include "extensions/browser/extension_function_registry.h"

namespace extensions {

void ShellExtensionsBrowserAPIProvider::RegisterExtensionFunctions(
    ExtensionFunctionRegistry* registry) {
  registry->RegisterFunction<TabsQueryFunction>();
  registry->RegisterFunction<TabsGetFunction>();
  registry->RegisterFunction<TabsGetCurrentFunction>();
  registry->RegisterFunction<TabsCreateFunction>();
  registry->RegisterFunction<TabsUpdateFunction>();
  registry->RegisterFunction<TabsReloadFunction>();
  registry->RegisterFunction<TabsRemoveFunction>();

  registry->RegisterFunction<WebNavigationGetFrameFunction>();
  registry->RegisterFunction<WebNavigationGetAllFramesFunction>();

  registry->RegisterFunction<ExtensionIsAllowedIncognitoAccessFunction>();
  registry->RegisterFunction<ExtensionIsAllowedFileSchemeAccessFunction>();

  registry->RegisterFunction<CookiesGetFunction>();
  registry->RegisterFunction<CookiesGetAllFunction>();
  registry->RegisterFunction<CookiesSetFunction>();
  registry->RegisterFunction<CookiesRemoveFunction>();
  registry->RegisterFunction<CookiesGetAllCookieStoresFunction>();

  registry->RegisterFunction<ActionSetIconFunction>();
  registry->RegisterFunction<ActionSetTitleFunction>();
  registry->RegisterFunction<ActionGetTitleFunction>();
  registry->RegisterFunction<ActionSetPopupFunction>();
  registry->RegisterFunction<ActionGetPopupFunction>();
  registry->RegisterFunction<ActionSetBadgeTextFunction>();
  registry->RegisterFunction<ActionGetBadgeTextFunction>();
  registry->RegisterFunction<ActionSetBadgeBackgroundColorFunction>();
  registry->RegisterFunction<ActionGetBadgeBackgroundColorFunction>();
  registry->RegisterFunction<ActionSetBadgeTextColorFunction>();
  registry->RegisterFunction<ActionGetBadgeTextColorFunction>();
  registry->RegisterFunction<ActionEnableFunction>();
  registry->RegisterFunction<ActionDisableFunction>();
  registry->RegisterFunction<ActionIsEnabledFunction>();
  registry->RegisterFunction<ActionGetUserSettingsFunction>();
  registry->RegisterFunction<ActionOpenPopupFunction>();
}

}  // namespace extensions
