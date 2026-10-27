// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extension_system.h"

#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/value_store/value_store_factory_impl.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry_factory.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/null_app_sorting.h"
#include "extensions/browser/permissions/permissions_updater.h"
#include "extensions/browser/quota_service.h"
#include "extensions/browser/service_worker_manager.h"
#include "extensions/browser/state_store.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/browser/user_script_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"

namespace extensions {

namespace {

void OnUnpackedLoaded(base::OnceCallback<void(const ExtensionId&)> done,
                      const Extension* extension,
                      const base::FilePath& path,
                      const std::u16string& error) {
  if (extension) {
    LOG(WARNING) << "[RCH] extension loaded: " << extension->name() << " "
                 << extension->VersionString() << " (" << extension->id()
                 << ") from " << path.value();
  } else {
    LOG(ERROR) << "[RCH] extension failed to load from " << path.value()
               << ": " << error;
  }
  std::move(done).Run(extension ? extension->id() : ExtensionId());
}

}  // namespace

ShellExtensionSystem::ShellExtensionSystem(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  // The stores exist from the start, as ChromeExtensionSystem::Shared's
  // InitPrefs makes them: services such as chrome.storage's StorageFrontend
  // take store_factory() when they are created, before
  // InitForRegularProfile(). The rules and dynamic-script stores load
  // immediately so they are ready before the first network request.
  const base::FilePath& path = browser_context_->GetPath();
  store_factory_ =
      base::MakeRefCounted<value_store::ValueStoreFactoryImpl>(path);
  state_store_ = std::make_unique<StateStore>(
      browser_context_, store_factory_, StateStore::BackendType::STATE, true);
  rules_store_ = std::make_unique<StateStore>(
      browser_context_, store_factory_, StateStore::BackendType::RULES, false);
  dynamic_user_scripts_store_ = std::make_unique<StateStore>(
      browser_context_, store_factory_, StateStore::BackendType::SCRIPTS,
      false);
}

ShellExtensionSystem::~ShellExtensionSystem() = default;

void ShellExtensionSystem::LoadUnpacked(
    const base::FilePath& dir,
    base::OnceCallback<void(const ExtensionId&)> done) {
  scoped_refptr<UnpackedInstaller> installer =
      UnpackedInstaller::Create(browser_context_);
  installer->set_be_noisy_on_failure(false);
  // Runs on failure too, including LoadFromCommandLine()'s own (except
  // without a browser context, which cannot happen here).
  installer->set_completion_callback(
      base::BindOnce(&OnUnpackedLoaded, std::move(done)));
  std::string id;
  if (!installer->LoadFromCommandLine(dir, &id, /*only_allow_apps=*/false)) {
    LOG(ERROR) << "[RCH] cannot load extension from " << dir.value();
  }
}

void ShellExtensionSystem::Shutdown() {
  // Members are torn down with the system, after every KeyedService that
  // depends on it has shut down.
}

void ShellExtensionSystem::InitForRegularProfile(bool extensions_enabled) {
  const base::FilePath& path = browser_context_->GetPath();

  service_worker_manager_ =
      std::make_unique<ServiceWorkerManager>(browser_context_);
  user_script_manager_ = std::make_unique<UserScriptManager>(browser_context_);
  management_policy_ = std::make_unique<ManagementPolicy>();
  quota_service_ = std::make_unique<QuotaService>();
  app_sorting_ = std::make_unique<NullAppSorting>();

  // Where packed (.crx) installs would go. Not Chrome's "Extensions": in
  // R Chromium that name is the folder people drop extensions into (see
  // ShellExtensions::BrowserContextsCreated).
  ExtensionRegistrar::Get(browser_context_)
      ->Init(this, extensions_enabled, base::CommandLine::ForCurrentProcess(),
             path.AppendASCII("Installed Extensions"),
             path.AppendASCII(kUnpackedInstallDirectoryName));

  ready_.Signal();
}

ExtensionService* ShellExtensionSystem::extension_service() {
  return nullptr;
}

ManagementPolicy* ShellExtensionSystem::management_policy() {
  return management_policy_.get();
}

ServiceWorkerManager* ShellExtensionSystem::service_worker_manager() {
  return service_worker_manager_.get();
}

UserScriptManager* ShellExtensionSystem::user_script_manager() {
  return user_script_manager_.get();
}

StateStore* ShellExtensionSystem::state_store() {
  return state_store_.get();
}

StateStore* ShellExtensionSystem::rules_store() {
  return rules_store_.get();
}

StateStore* ShellExtensionSystem::dynamic_user_scripts_store() {
  return dynamic_user_scripts_store_.get();
}

scoped_refptr<value_store::ValueStoreFactory>
ShellExtensionSystem::store_factory() {
  return store_factory_;
}

QuotaService* ShellExtensionSystem::quota_service() {
  return quota_service_.get();
}

AppSorting* ShellExtensionSystem::app_sorting() {
  return app_sorting_.get();
}

const base::OneShotEvent& ShellExtensionSystem::ready() const {
  return ready_;
}

bool ShellExtensionSystem::is_ready() const {
  return ready_.is_signaled();
}

ContentVerifier* ShellExtensionSystem::content_verifier() {
  return nullptr;
}

void ShellExtensionSystem::InstallUpdate(
    const ExtensionId& extension_id,
    const std::string& public_key,
    const base::FilePath& unpacked_dir,
    bool install_immediately,
    InstallUpdateCallback install_update_callback) {
  // No updater: nothing installs from the Web Store.
  NOTREACHED();
}

void ShellExtensionSystem::PerformActionBasedOnOmahaAttributes(
    const ExtensionId& extension_id,
    const base::DictValue& attributes) {}

void ShellExtensionSystem::PreAddExtension(const Extension* extension,
                                           const Extension* old_extension) {
  // Sets up the extension's active permissions from its manifest and what
  // has been granted, as ChromeExtensionRegistrarDelegate does.
  PermissionsUpdater(browser_context_).InitializePermissions(extension);
}

void ShellExtensionSystem::OnAddNewOrUpdatedExtension(
    const Extension* extension) {}

void ShellExtensionSystem::PostActivateExtension(
    scoped_refptr<const Extension> extension) {}

void ShellExtensionSystem::PostDeactivateExtension(
    scoped_refptr<const Extension> extension) {}

void ShellExtensionSystem::PreUninstallExtension(
    scoped_refptr<const Extension> extension) {}

void ShellExtensionSystem::PostUninstallExtension(
    scoped_refptr<const Extension> extension,
    base::OnceClosure done_callback) {
  std::move(done_callback).Run();
}

void ShellExtensionSystem::LoadExtensionForReload(
    const ExtensionId& extension_id,
    const base::FilePath& path) {
  LoadUnpacked(path, base::DoNothing());
}

void ShellExtensionSystem::LoadExtensionForReloadWithQuietFailure(
    const ExtensionId& extension_id,
    const base::FilePath& path) {
  LoadUnpacked(path, base::DoNothing());
}

void ShellExtensionSystem::ShowExtensionDisabledError(
    const Extension* extension,
    bool is_remote_install) {}

bool ShellExtensionSystem::CanEnableExtension(const Extension* extension) {
  return true;
}

bool ShellExtensionSystem::CanDisableExtension(const Extension* extension) {
  return true;
}

void ShellExtensionSystem::GrantActivePermissions(const Extension* extension) {
  // No install prompt: what the manifest asks for is granted.
  PermissionsUpdater(browser_context_).GrantActivePermissions(extension);
}

void ShellExtensionSystem::UpdateExternalExtensionAlert() {}

void ShellExtensionSystem::OnExtensionInstalled(
    const Extension* extension,
    const syncer::StringOrdinal& page_ordinal,
    int install_flags,
    base::DictValue ruleset_install_prefs) {
  // Nothing delays an install here (no updater, no install gates), so this
  // is ChromeExtensionRegistrarDelegate's InstallGate::INSTALL case.
  ExtensionRegistrar::Get(browser_context_)
      ->AddNewOrUpdatedExtension(extension, install_flags, page_ordinal,
                                 /*install_parameter=*/std::string(),
                                 std::move(ruleset_install_prefs));
}

// static
ShellExtensionSystemFactory* ShellExtensionSystemFactory::GetInstance() {
  static base::NoDestructor<ShellExtensionSystemFactory> instance;
  return instance.get();
}

ShellExtensionSystemFactory::ShellExtensionSystemFactory()
    : ExtensionSystemProvider("ExtensionSystem",
                              BrowserContextDependencyManager::GetInstance()) {
  DCHECK(ExtensionsBrowserClient::Get());
  DependsOn(ExtensionPrefsFactory::GetInstance());
  DependsOn(ExtensionRegistryFactory::GetInstance());
}

ShellExtensionSystemFactory::~ShellExtensionSystemFactory() = default;

ExtensionSystem* ShellExtensionSystemFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<ShellExtensionSystem*>(
      GetServiceForBrowserContext(context, true));
}

std::unique_ptr<KeyedService>
ShellExtensionSystemFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<ShellExtensionSystem>(context);
}

content::BrowserContext* ShellExtensionSystemFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // One system, shared with the off-the-record context.
  return ExtensionsBrowserClient::Get()->GetContextRedirectedToOriginal(
      context);
}

bool ShellExtensionSystemFactory::ServiceIsCreatedWithBrowserContext() const {
  return false;
}

}  // namespace extensions
