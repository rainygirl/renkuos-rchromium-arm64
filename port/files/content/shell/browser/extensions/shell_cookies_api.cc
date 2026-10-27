// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_cookies_api.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/expected.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "base/notreached.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "net/cookies/cookie_util.h"
#include "net/cookies/site_for_cookies.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace extensions {

namespace cookies = api::cookies;

namespace {

constexpr char kStoreId[] = "0";

constexpr char kCookieSetFailedError[] =
    "Failed to parse or set cookie named \"*\".";
constexpr char kInvalidStoreIdError[] = "Invalid cookie store id: \"*\".";
constexpr char kInvalidUrlError[] = "Invalid url: \"*\".";
constexpr char kNoHostPermissionsError[] =
    "No host permissions for cookies at url: \"*\".";

network::mojom::CookieManager* CookieManagerFor(
    content::BrowserContext* context,
    std::optional<std::string>& store_id,
    std::string* error) {
  if (store_id && *store_id != kStoreId) {
    *error = ErrorUtils::FormatErrorMessage(kInvalidStoreIdError, *store_id);
    return nullptr;
  }
  store_id = kStoreId;
  return context->GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess();
}

bool ParseUrl(const Extension* extension,
              const std::string& url_string,
              GURL* url,
              bool check_host_permissions,
              std::string* error) {
  *url = GURL(url_string);
  if (!url->is_valid()) {
    *error = ErrorUtils::FormatErrorMessage(kInvalidUrlError, url_string);
    return false;
  }
  if (check_host_permissions &&
      extension->permissions_data()->GetPageAccess(
          *url, extension_misc::kUnknownTabId, /*error=*/nullptr) !=
          PermissionsData::PageAccess::kAllowed) {
    *error = ErrorUtils::FormatErrorMessage(kNoHostPermissionsError,
                                            url->spec());
    return false;
  }
  return true;
}

cookies::Cookie CreateCookie(const net::CanonicalCookie& canonical_cookie) {
  cookies::Cookie cookie;
  // A cookie is raw bytes; parsing as UTF-8 corrects errors so the string
  // can go to the renderer.
  cookie.name = base::UTF16ToUTF8(base::UTF8ToUTF16(canonical_cookie.Name()));
  cookie.value = base::UTF16ToUTF8(base::UTF8ToUTF16(canonical_cookie.Value()));
  cookie.domain = canonical_cookie.Domain();
  cookie.host_only =
      net::cookie_util::DomainIsHostOnly(canonical_cookie.Domain());
  cookie.path = base::IsStringUTF8(canonical_cookie.Path())
                    ? canonical_cookie.Path()
                    : std::string();
  cookie.secure = canonical_cookie.SecureAttribute();
  cookie.http_only = canonical_cookie.IsHttpOnly();
  switch (canonical_cookie.SameSite()) {
    case net::CookieSameSite::NO_RESTRICTION:
      cookie.same_site = cookies::SameSiteStatus::kNoRestriction;
      break;
    case net::CookieSameSite::LAX_MODE:
      cookie.same_site = cookies::SameSiteStatus::kLax;
      break;
    case net::CookieSameSite::STRICT_MODE:
      cookie.same_site = cookies::SameSiteStatus::kStrict;
      break;
    case net::CookieSameSite::UNSPECIFIED:
      cookie.same_site = cookies::SameSiteStatus::kUnspecified;
      break;
  }
  cookie.session = !canonical_cookie.IsPersistent();
  if (canonical_cookie.IsPersistent()) {
    double expiration_date =
        canonical_cookie.ExpiryDate().InSecondsFSinceUnixEpoch();
    if (canonical_cookie.ExpiryDate().is_max() ||
        !std::isfinite(expiration_date)) {
      expiration_date = std::numeric_limits<double>::max();
    }
    cookie.expiration_date = expiration_date;
  }
  cookie.store_id = kStoreId;
  if (canonical_cookie.PartitionKey()) {
    base::expected<net::CookiePartitionKey::SerializedCookiePartitionKey,
                   std::string>
        serialized =
            net::CookiePartitionKey::Serialize(canonical_cookie.PartitionKey());
    if (serialized.has_value()) {
      cookie.partition_key = cookies::CookiePartitionKey();
      cookie.partition_key->top_level_site = serialized->TopLevelSite();
      cookie.partition_key->has_cross_site_ancestor =
          serialized->has_cross_site_ancestor();
    }
  }
  return cookie;
}

base::expected<bool, std::string> CalculateHasCrossSiteAncestor(
    const std::string& url_string,
    const cookies::CookiePartitionKey& partition_key) {
  if (partition_key.has_cross_site_ancestor) {
    return *partition_key.has_cross_site_ancestor;
  }
  if (partition_key.top_level_site->empty()) {
    return false;
  }
  GURL url(url_string);
  if (!url.is_valid()) {
    return base::unexpected("Invalid url_string.");
  }
  GURL top_level_site(*partition_key.top_level_site);
  if (!top_level_site.is_valid()) {
    return base::unexpected(
        "Invalid value for CookiePartitionKey.topLevelSite.");
  }
  return !net::SiteForCookies::FromUrl(url).IsFirstParty(top_level_site);
}

// Fills in hasCrossSiteAncestor from the URL where it is missing.
std::optional<std::string> CompletePartitionKey(
    const std::string& url,
    std::optional<cookies::CookiePartitionKey>& partition_key) {
  if (!partition_key || partition_key->has_cross_site_ancestor ||
      !partition_key->top_level_site) {
    return std::nullopt;
  }
  base::expected<bool, std::string> ancestor =
      CalculateHasCrossSiteAncestor(url, *partition_key);
  if (!ancestor.has_value()) {
    return ancestor.error();
  }
  partition_key->has_cross_site_ancestor = *ancestor;
  return std::nullopt;
}

base::expected<std::optional<net::CookiePartitionKey>, std::string>
ToNetCookiePartitionKey(
    const std::optional<cookies::CookiePartitionKey>& partition_key) {
  if (!partition_key || !partition_key->top_level_site) {
    if (partition_key && partition_key->has_cross_site_ancestor) {
      return base::unexpected(
          "CookiePartitionKey.topLevelSite unexpectedly not present.");
    }
    return std::nullopt;
  }
  if (partition_key->top_level_site->empty()) {
    if (partition_key->has_cross_site_ancestor.value_or(false)) {
      return base::unexpected(
          "partitionKey with empty topLevelSite unexpectedly has a cross-site "
          "ancestor value of true.");
    }
    return std::nullopt;
  }
  base::expected<net::CookiePartitionKey, std::string> key =
      net::CookiePartitionKey::FromUntrustedInput(
          *partition_key->top_level_site,
          partition_key->has_cross_site_ancestor.value_or(true));
  if (!key.has_value()) {
    return base::unexpected(key.error());
  }
  return std::optional<net::CookiePartitionKey>(std::move(key).value());
}

net::CookiePartitionKeyCollection CollectionFromApiPartitionKey(
    const std::optional<cookies::CookiePartitionKey>& partition_key) {
  if (!partition_key) {
    return net::CookiePartitionKeyCollection();
  }
  if (!partition_key->top_level_site) {
    return net::CookiePartitionKeyCollection::ContainsAll();
  }
  if (partition_key->top_level_site->empty()) {
    return net::CookiePartitionKeyCollection();
  }
  if (!partition_key->has_cross_site_ancestor) {
    return net::CookiePartitionKeyCollection::MatchesSite(
        net::SchemefulSite(GURL(*partition_key->top_level_site)));
  }
  base::expected<net::CookiePartitionKey, std::string> key =
      net::CookiePartitionKey::FromUntrustedInput(
          *partition_key->top_level_site,
          *partition_key->has_cross_site_ancestor);
  if (!key.has_value()) {
    return net::CookiePartitionKeyCollection();
  }
  return net::CookiePartitionKeyCollection(std::move(key).value());
}

bool PartitionKeyMatches(
    const std::optional<cookies::CookiePartitionKey>& api_key,
    const std::optional<net::CookiePartitionKey>& net_key) {
  if (!net_key) {
    return !api_key || !api_key->top_level_site ||
           api_key->top_level_site->empty();
  }
  if (!api_key) {
    return false;
  }
  if (api_key->has_cross_site_ancestor &&
      net_key->IsThirdParty() != *api_key->has_cross_site_ancestor) {
    return false;
  }
  if (!net_key->IsSerializeable() || !api_key->top_level_site) {
    return false;
  }
  base::expected<net::CookiePartitionKey::SerializedCookiePartitionKey,
                 std::string>
      serialized = net::CookiePartitionKey::Serialize(net_key);
  return serialized.has_value() &&
         serialized->TopLevelSite() == *api_key->top_level_site;
}

bool CollectionMatches(const net::CookiePartitionKeyCollection& collection,
                       const net::CanonicalCookie& cookie) {
  if (!cookie.IsPartitioned()) {
    return collection.ContainsAllKeys() || collection.IsEmpty();
  }
  return collection.Contains(*cookie.PartitionKey());
}

// cookies.getAll's filter; `domain` matches subdomains.
bool DomainMatches(const std::optional<std::string>& filter_domain,
                   const std::string& cookie_domain) {
  if (!filter_domain) {
    return true;
  }
  std::string filter = *filter_domain;
  if (net::cookie_util::DomainIsHostOnly(filter)) {
    filter.insert(0, ".");
  }
  std::string sub_domain = cookie_domain;
  if (!net::cookie_util::DomainIsHostOnly(sub_domain)) {
    sub_domain = sub_domain.substr(1);
  }
  for (sub_domain.insert(0, "."); sub_domain.length() >= filter.length();) {
    if (sub_domain == filter) {
      return true;
    }
    sub_domain.erase(0, sub_domain.find('.', 1));
  }
  return false;
}

bool FilterMatches(const cookies::GetAll::Params::Details& details,
                   const net::CookiePartitionKeyCollection& collection,
                   const net::CanonicalCookie& cookie) {
  if (!CollectionMatches(collection, cookie)) {
    return false;
  }
  return (!details.name || *details.name == cookie.Name()) &&
         DomainMatches(details.domain, cookie.Domain()) &&
         (!details.path || *details.path == cookie.Path()) &&
         (!details.secure || *details.secure == cookie.SecureAttribute()) &&
         (!details.session || *details.session == !cookie.IsPersistent());
}

// Cookies the extension has host access to and the filter matches.
std::vector<cookies::Cookie> MatchingCookies(
    const std::vector<const net::CanonicalCookie*>& candidates,
    const cookies::GetAll::Params::Details& details,
    const net::CookiePartitionKeyCollection& collection,
    const Extension* extension) {
  std::vector<cookies::Cookie> result;
  for (const net::CanonicalCookie* cookie : candidates) {
    const GURL domain_url = net::cookie_util::CookieOriginToURL(
        cookie->Domain(), cookie->SecureAttribute());
    if (extension->permissions_data()->GetPageAccess(
            domain_url, extension_misc::kUnknownTabId, nullptr) !=
        PermissionsData::PageAccess::kAllowed) {
      continue;
    }
    if (FilterMatches(details, collection, *cookie)) {
      result.push_back(CreateCookie(*cookie));
    }
  }
  return result;
}

}  // namespace

ShellCookiesEventRouter::ShellCookiesEventRouter(
    content::BrowserContext* context)
    : context_(context) {
  Bind();
}

ShellCookiesEventRouter::~ShellCookiesEventRouter() = default;

void ShellCookiesEventRouter::Bind() {
  network::mojom::CookieManager* manager =
      context_->GetDefaultStoragePartition()
          ->GetCookieManagerForBrowserProcess();
  if (!manager) {
    return;
  }
  manager->AddGlobalChangeListener(receiver_.BindNewPipeAndPassRemote());
  // The network service can restart; listen again when it does.
  receiver_.set_disconnect_handler(base::BindOnce(
      [](ShellCookiesEventRouter* router) {
        router->receiver_.reset();
        router->Bind();
      },
      base::Unretained(this)));
}

void ShellCookiesEventRouter::OnCookieChange(
    const net::CookieChangeInfo& change) {
  // As Chrome: a partitioned cookie the API cannot describe is skipped.
  if (change.cookie.IsPartitioned() &&
      !change.cookie.PartitionKey()->IsSerializeable()) {
    return;
  }
  cookies::OnChanged::ChangeInfo info;
  info.removed =
      change.cause != net::CookieChangeCause::INSERTED &&
      change.cause != net::CookieChangeCause::INSERTED_NO_CHANGE_OVERWRITE &&
      change.cause !=
          net::CookieChangeCause::INSERTED_NO_VALUE_CHANGE_OVERWRITE;
  info.cookie = CreateCookie(change.cookie);
  switch (change.cause) {
    case net::CookieChangeCause::INSERTED:
    case net::CookieChangeCause::EXPLICIT:
    case net::CookieChangeCause::INSERTED_NO_CHANGE_OVERWRITE:
    case net::CookieChangeCause::INSERTED_NO_VALUE_CHANGE_OVERWRITE:
      info.cause = cookies::OnChangedCause::kExplicit;
      break;
    case net::CookieChangeCause::OVERWRITE:
      info.cause = cookies::OnChangedCause::kOverwrite;
      break;
    case net::CookieChangeCause::EXPIRED:
      info.cause = cookies::OnChangedCause::kExpired;
      break;
    case net::CookieChangeCause::EVICTED:
      info.cause = cookies::OnChangedCause::kEvicted;
      break;
    case net::CookieChangeCause::EXPIRED_OVERWRITE:
      info.cause = cookies::OnChangedCause::kExpiredOverwrite;
      break;
    case net::CookieChangeCause::UNKNOWN_DELETION:
      NOTREACHED();
  }
  EventRouter* router = EventRouter::Get(context_);
  if (!router) {
    return;
  }
  auto event = std::make_unique<Event>(
      events::COOKIES_ON_CHANGED, cookies::OnChanged::kEventName,
      cookies::OnChanged::Create(info), context_.get());
  // Delivered only where the extension has access to this URL.
  event->event_url = net::cookie_util::CookieOriginToURL(
      change.cookie.Domain(), change.cookie.SecureAttribute());
  router->BroadcastEvent(std::move(event));
}

CookiesGetFunction::CookiesGetFunction() = default;
CookiesGetFunction::~CookiesGetFunction() = default;

ExtensionFunction::ResponseAction CookiesGetFunction::Run() {
  params_ = cookies::Get::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params_);
  auto& details = params_->details;

  std::string error;
  GURL url;
  if (!ParseUrl(extension(), details.url, &url, true, &error)) {
    return RespondNow(Error(std::move(error)));
  }
  network::mojom::CookieManager* manager =
      CookieManagerFor(browser_context(), details.store_id, &error);
  if (!manager) {
    return RespondNow(Error(std::move(error)));
  }
  if (std::optional<std::string> key_error =
          CompletePartitionKey(details.url, details.partition_key)) {
    return RespondNow(Error(std::move(*key_error)));
  }
  auto partition_key = ToNetCookiePartitionKey(details.partition_key);
  if (!partition_key.has_value()) {
    return RespondNow(Error(std::move(partition_key.error())));
  }
  manager->GetCookieList(
      url, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(std::move(partition_key).value()),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&CookiesGetFunction::GotCookies, this),
          net::CookieAccessResultList(), net::CookieAccessResultList()));
  return RespondLater();
}

void CookiesGetFunction::GotCookies(
    const net::CookieAccessResultList& cookie_list,
    const net::CookieAccessResultList& excluded_cookies) {
  for (const net::CookieWithAccessResult& entry : cookie_list) {
    if (PartitionKeyMatches(params_->details.partition_key,
                            entry.cookie.PartitionKey()) &&
        entry.cookie.Name() == params_->details.name) {
      Respond(ArgumentList(
          cookies::Get::Results::Create(CreateCookie(entry.cookie))));
      return;
    }
  }
  Respond(WithArguments(base::Value()));
}

CookiesGetAllFunction::CookiesGetAllFunction() = default;
CookiesGetAllFunction::~CookiesGetAllFunction() = default;

ExtensionFunction::ResponseAction CookiesGetAllFunction::Run() {
  params_ = cookies::GetAll::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params_);
  auto& details = params_->details;

  std::string error;
  GURL url;
  if (details.url && !ParseUrl(extension(), *details.url, &url, false, &error)) {
    return RespondNow(Error(std::move(error)));
  }
  network::mojom::CookieManager* manager =
      CookieManagerFor(browser_context(), details.store_id, &error);
  if (!manager) {
    return RespondNow(Error(std::move(error)));
  }
  auto partition_key = ToNetCookiePartitionKey(details.partition_key);
  if (!partition_key.has_value()) {
    return RespondNow(Error(std::move(partition_key.error())));
  }
  if (url.is_empty()) {
    manager->GetAllCookies(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
        base::BindOnce(&CookiesGetAllFunction::GotAllCookies, this),
        net::CookieList()));
  } else {
    manager->GetCookieList(
        url, net::CookieOptions::MakeAllInclusive(),
        CollectionFromApiPartitionKey(details.partition_key),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&CookiesGetAllFunction::GotURLCookies, this),
            net::CookieAccessResultList(), net::CookieAccessResultList()));
  }
  return RespondLater();
}

void CookiesGetAllFunction::GotAllCookies(const net::CookieList& cookie_list) {
  std::vector<const net::CanonicalCookie*> candidates;
  for (const net::CanonicalCookie& cookie : cookie_list) {
    candidates.push_back(&cookie);
  }
  Respond(ArgumentList(cookies::GetAll::Results::Create(MatchingCookies(
      candidates, params_->details,
      CollectionFromApiPartitionKey(params_->details.partition_key),
      extension()))));
}

void CookiesGetAllFunction::GotURLCookies(
    const net::CookieAccessResultList& cookie_list,
    const net::CookieAccessResultList& excluded_cookies) {
  std::vector<const net::CanonicalCookie*> candidates;
  for (const net::CookieWithAccessResult& entry : cookie_list) {
    candidates.push_back(&entry.cookie);
  }
  Respond(ArgumentList(cookies::GetAll::Results::Create(MatchingCookies(
      candidates, params_->details,
      CollectionFromApiPartitionKey(params_->details.partition_key),
      extension()))));
}

CookiesSetFunction::CookiesSetFunction() = default;
CookiesSetFunction::~CookiesSetFunction() = default;

ExtensionFunction::ResponseAction CookiesSetFunction::Run() {
  params_ = cookies::Set::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params_);
  auto& details = params_->details;

  std::string error;
  if (!ParseUrl(extension(), details.url, &url_, true, &error)) {
    return RespondNow(Error(std::move(error)));
  }
  network::mojom::CookieManager* manager =
      CookieManagerFor(browser_context(), details.store_id, &error);
  if (!manager) {
    return RespondNow(Error(std::move(error)));
  }
  if (std::optional<std::string> key_error =
          CompletePartitionKey(details.url, details.partition_key)) {
    return RespondNow(Error(std::move(*key_error)));
  }
  auto partition_key = ToNetCookiePartitionKey(details.partition_key);
  if (!partition_key.has_value()) {
    return RespondNow(Error(std::move(partition_key.error())));
  }

  base::Time expiration_time;
  if (details.expiration_date) {
    expiration_time =
        *details.expiration_date == 0
            ? base::Time::UnixEpoch()
            : base::Time::FromSecondsSinceUnixEpoch(*details.expiration_date);
  }
  net::CookieSameSite same_site = net::CookieSameSite::UNSPECIFIED;
  switch (details.same_site) {
    case cookies::SameSiteStatus::kNoRestriction:
      same_site = net::CookieSameSite::NO_RESTRICTION;
      break;
    case cookies::SameSiteStatus::kLax:
      same_site = net::CookieSameSite::LAX_MODE;
      break;
    case cookies::SameSiteStatus::kStrict:
      same_site = net::CookieSameSite::STRICT_MODE;
      break;
    case cookies::SameSiteStatus::kUnspecified:
    case cookies::SameSiteStatus::kNone:
      break;
  }

  std::unique_ptr<net::CanonicalCookie> cookie =
      net::CanonicalCookie::CreateSanitizedCookie(
          url_, details.name.value_or(std::string()),
          details.value.value_or(std::string()),
          details.domain.value_or(std::string()),
          details.path.value_or(std::string()), /*creation_time=*/base::Time(),
          expiration_time, /*last_access_time=*/base::Time(),
          details.secure.value_or(false), details.http_only.value_or(false),
          same_site, net::COOKIE_PRIORITY_DEFAULT, partition_key.value(),
          /*status=*/nullptr);
  if (!cookie) {
    return RespondNow(Error(ErrorUtils::FormatErrorMessage(
        kCookieSetFailedError, details.name.value_or(std::string()))));
  }

  net::CookieOptions options;
  options.set_include_httponly();
  options.set_same_site_cookie_context(
      net::CookieOptions::SameSiteCookieContext::MakeInclusive());
  manager->SetCanonicalCookie(
      *cookie, url_, options,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&CookiesSetFunction::CookieSet, this),
          net::CookieAccessResult()));
  // Read it back, after the set (the manager answers in order).
  manager->GetCookieList(
      url_, net::CookieOptions::MakeAllInclusive(),
      net::CookiePartitionKeyCollection(std::move(partition_key).value()),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&CookiesSetFunction::GotCookies, this),
          net::CookieAccessResultList(), net::CookieAccessResultList()));
  return RespondLater();
}

void CookiesSetFunction::CookieSet(net::CookieAccessResult result) {
  success_ = result.status.IsInclude();
}

void CookiesSetFunction::GotCookies(
    const net::CookieAccessResultList& cookie_list,
    const net::CookieAccessResultList& excluded_cookies) {
  const std::string name = params_->details.name.value_or(std::string());
  if (!success_) {
    Respond(Error(ErrorUtils::FormatErrorMessage(kCookieSetFailedError, name)));
    return;
  }
  for (const net::CookieWithAccessResult& entry : cookie_list) {
    if (PartitionKeyMatches(params_->details.partition_key,
                            entry.cookie.PartitionKey()) &&
        entry.cookie.Name() == name) {
      Respond(ArgumentList(
          cookies::Set::Results::Create(CreateCookie(entry.cookie))));
      return;
    }
  }
  Respond(NoArguments());
}

CookiesRemoveFunction::CookiesRemoveFunction() = default;
CookiesRemoveFunction::~CookiesRemoveFunction() = default;

ExtensionFunction::ResponseAction CookiesRemoveFunction::Run() {
  params_ = cookies::Remove::Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params_);
  auto& details = params_->details;

  std::string error;
  if (!ParseUrl(extension(), details.url, &url_, true, &error)) {
    return RespondNow(Error(std::move(error)));
  }
  network::mojom::CookieManager* manager =
      CookieManagerFor(browser_context(), details.store_id, &error);
  if (!manager) {
    return RespondNow(Error(std::move(error)));
  }
  auto partition_key = ToNetCookiePartitionKey(details.partition_key);
  if (!partition_key.has_value()) {
    return RespondNow(Error(std::move(partition_key.error())));
  }

  auto filter = network::mojom::CookieDeletionFilter::New();
  filter->cookie_partition_key_collection =
      net::CookiePartitionKeyCollection(std::move(partition_key).value());
  filter->url = url_;
  filter->cookie_name = details.name;
  manager->DeleteCookies(
      std::move(filter),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&CookiesRemoveFunction::CookiesRemoved, this), 0u));
  return RespondLater();
}

void CookiesRemoveFunction::CookiesRemoved(uint32_t num_deleted) {
  cookies::Remove::Results::Details details;
  details.name = params_->details.name;
  details.url = url_.spec();
  details.store_id = *params_->details.store_id;
  if (params_->details.partition_key) {
    details.partition_key = params_->details.partition_key->Clone();
  }
  Respond(ArgumentList(cookies::Remove::Results::Create(details)));
}

ExtensionFunction::ResponseAction CookiesGetAllCookieStoresFunction::Run() {
  cookies::CookieStore store;
  store.id = kStoreId;
  for (content::WebContents* tab : ShellTabs::GetTabs(browser_context())) {
    store.tab_ids.push_back(ShellTabs::GetTabId(tab));
  }
  std::vector<cookies::CookieStore> stores;
  stores.push_back(std::move(store));
  return RespondNow(
      ArgumentList(cookies::GetAllCookieStores::Results::Create(stores)));
}

}  // namespace extensions
