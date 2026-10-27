// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extensions_api_client.h"

#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "components/enterprise/browser/promotion/promotion_eligibility_checker.h"
#include "components/safe_browsing/buildflags.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "extensions/browser/api/messaging/messaging_delegate.h"
#include "extensions/browser/api/webstore_private/webstore_private_api_delegate.h"

namespace extensions {

namespace {

// There is no Web Store integration: nothing installs through
// webstorePrivate, so every hook declines.
class ShellWebstorePrivateAPIDelegate : public WebstorePrivateAPIDelegate {
 public:
  std::vector<KeyedServiceBaseFactory*> GetWebStoreAPIFactoryDependencies()
      override {
    return {};
  }
  ExtensionAllowlist* GetExtensionAllowlist(
      content::BrowserContext* context) override {
    return nullptr;
  }
  signin::IdentityManager* GetIdentityManager(
      content::BrowserContext* context) override {
    return nullptr;
  }
  void ShowExtensionInstallBlockedDialog(
      content::WebContents* web_contents,
      const Extension* extension,
      const std::u16string& custom_error_message,
      const gfx::ImageSkia& icon,
      base::OnceClosure done_callback) override {
    std::move(done_callback).Run();
  }
  void ShowExtensionInstallFrictionDialog(
      content::WebContents* web_contents,
      base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(false);
  }
#if BUILDFLAG(IS_ANDROID)
  void ShowExtensionInstallAskParentDialog(
      content::WebContents* web_contents,
      base::OnceClosure cancel_callback,
      base::OnceClosure approve_callback) override {
    std::move(cancel_callback).Run();
  }
#endif
  void ReportFrictionAcceptedEvent(content::BrowserContext* context) override {
  }
#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  bool IsSafeBrowsingEnabledAndReady(
      content::BrowserContext* context) override {
    return false;
  }
  safe_browsing::SafeBrowsingNavigationObserverManager*
  GetSafeBrowsingNavigationObserverManager(
      content::BrowserContext* context) override {
    return nullptr;
  }
#endif
  std::unique_ptr<enterprise_promotion::PromotionEligibilityChecker>
  CreatePromotionEligibilityChecker(content::BrowserContext* context,
                                    bool dismissed_banner_pref,
                                    bool feature_enabled) override {
    return nullptr;
  }
};

// Message senders and tabs.sendMessage targets are ShellTabs' tabs.
class ShellMessagingDelegate : public MessagingDelegate {
 public:
  std::optional<base::DictValue> MaybeGetTabInfo(
      content::WebContents* web_contents) override {
    if (!web_contents || ShellTabs::GetTabId(web_contents) < 0) {
      return std::nullopt;
    }
    return ShellTabs::CreateTabObject(web_contents, nullptr).ToValue();
  }
  content::WebContents* GetWebContentsByTabId(
      content::BrowserContext* browser_context,
      int tab_id) override {
    return ShellTabs::GetTabById(browser_context, tab_id);
  }
};

}  // namespace

ShellExtensionsAPIClient::ShellExtensionsAPIClient()
    : webstore_private_api_delegate_(
          std::make_unique<ShellWebstorePrivateAPIDelegate>()),
      messaging_delegate_(std::make_unique<ShellMessagingDelegate>()) {}

ShellExtensionsAPIClient::~ShellExtensionsAPIClient() = default;

WebstorePrivateAPIDelegate*
ShellExtensionsAPIClient::GetWebstorePrivateAPIDelegate() {
  return webstore_private_api_delegate_.get();
}

MessagingDelegate* ShellExtensionsAPIClient::GetMessagingDelegate() {
  return messaging_delegate_.get();
}

}  // namespace extensions
