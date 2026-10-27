// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_SYSTEM_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_SYSTEM_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/one_shot_event.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_system_provider.h"
#include "extensions/common/extension_id.h"

namespace base {
class FilePath;
}

namespace content {
class BrowserContext;
}

namespace value_store {
class ValueStoreFactory;
}

namespace extensions {

class AppSorting;
class ManagementPolicy;
class QuotaService;
class ServiceWorkerManager;
class StateStore;
class UserScriptManager;

// R Chromium: content_shell's ExtensionSystem, modelled on
// ChromeExtensionSystem without ExtensionService. Extensions are added
// through ExtensionRegistrar, from UnpackedInstaller (--load-extension).
class ShellExtensionSystem : public ExtensionSystem,
                             public ExtensionRegistrar::Delegate {
 public:
  explicit ShellExtensionSystem(content::BrowserContext* browser_context);
  ShellExtensionSystem(const ShellExtensionSystem&) = delete;
  ShellExtensionSystem& operator=(const ShellExtensionSystem&) = delete;
  ~ShellExtensionSystem() override;

  // Loads an unpacked extension from `dir`, asynchronously. Errors are
  // logged. `done` runs once loading has finished, with the extension's ID,
  // or an empty one if it failed.
  void LoadUnpacked(const base::FilePath& dir,
                    base::OnceCallback<void(const ExtensionId&)> done);

  // KeyedService:
  void Shutdown() override;

  // ExtensionSystem:
  void InitForRegularProfile(bool extensions_enabled) override;
  ExtensionService* extension_service() override;
  ManagementPolicy* management_policy() override;
  ServiceWorkerManager* service_worker_manager() override;
  UserScriptManager* user_script_manager() override;
  StateStore* state_store() override;
  StateStore* rules_store() override;
  StateStore* dynamic_user_scripts_store() override;
  scoped_refptr<value_store::ValueStoreFactory> store_factory() override;
  QuotaService* quota_service() override;
  AppSorting* app_sorting() override;
  const base::OneShotEvent& ready() const override;
  bool is_ready() const override;
  ContentVerifier* content_verifier() override;
  void InstallUpdate(const ExtensionId& extension_id,
                     const std::string& public_key,
                     const base::FilePath& unpacked_dir,
                     bool install_immediately,
                     InstallUpdateCallback install_update_callback) override;
  void PerformActionBasedOnOmahaAttributes(
      const ExtensionId& extension_id,
      const base::DictValue& attributes) override;

  // ExtensionRegistrar::Delegate:
  void PreAddExtension(const Extension* extension,
                       const Extension* old_extension) override;
  void OnAddNewOrUpdatedExtension(const Extension* extension) override;
  void PostActivateExtension(scoped_refptr<const Extension> extension) override;
  void PostDeactivateExtension(
      scoped_refptr<const Extension> extension) override;
  void PreUninstallExtension(scoped_refptr<const Extension> extension) override;
  void PostUninstallExtension(scoped_refptr<const Extension> extension,
                              base::OnceClosure done_callback) override;
  void LoadExtensionForReload(const ExtensionId& extension_id,
                              const base::FilePath& path) override;
  void LoadExtensionForReloadWithQuietFailure(
      const ExtensionId& extension_id,
      const base::FilePath& path) override;
  void ShowExtensionDisabledError(const Extension* extension,
                                  bool is_remote_install) override;
  bool CanEnableExtension(const Extension* extension) override;
  bool CanDisableExtension(const Extension* extension) override;
  void GrantActivePermissions(const Extension* extension) override;
  void UpdateExternalExtensionAlert() override;
  void OnExtensionInstalled(const Extension* extension,
                            const syncer::StringOrdinal& page_ordinal,
                            int install_flags,
                            base::DictValue ruleset_install_prefs) override;

 private:
  raw_ptr<content::BrowserContext> browser_context_;

  scoped_refptr<value_store::ValueStoreFactory> store_factory_;
  std::unique_ptr<StateStore> state_store_;
  std::unique_ptr<StateStore> rules_store_;
  std::unique_ptr<StateStore> dynamic_user_scripts_store_;
  std::unique_ptr<ServiceWorkerManager> service_worker_manager_;
  std::unique_ptr<UserScriptManager> user_script_manager_;
  std::unique_ptr<ManagementPolicy> management_policy_;
  std::unique_ptr<QuotaService> quota_service_;
  std::unique_ptr<AppSorting> app_sorting_;

  base::OneShotEvent ready_;
};

// The ExtensionSystemProvider ExtensionsBrowserClient hands out.
class ShellExtensionSystemFactory : public ExtensionSystemProvider {
 public:
  static ShellExtensionSystemFactory* GetInstance();

  ShellExtensionSystemFactory(const ShellExtensionSystemFactory&) = delete;
  ShellExtensionSystemFactory& operator=(const ShellExtensionSystemFactory&) =
      delete;

  // ExtensionSystemProvider:
  ExtensionSystem* GetForBrowserContext(
      content::BrowserContext* context) override;

 private:
  friend class base::NoDestructor<ShellExtensionSystemFactory>;

  ShellExtensionSystemFactory();
  ~ShellExtensionSystemFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_SYSTEM_H_
