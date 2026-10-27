// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_COOKIES_API_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_COOKIES_API_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "content/shell/common/extensions/api/cookies.h"
#include "extensions/browser/extension_function.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_change_dispatcher.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "url/gurl.h"

// R Chromium: chrome.cookies over the main context's cookie manager,
// adapted from chrome/browser/extensions/api/cookies (same validation,
// partition-key and matching rules). There is one cookie store, "0":
// extensions do not run off the record.

namespace content {
class BrowserContext;
}

namespace extensions {

// Turns the main context's cookie changes into cookies.onChanged, for the
// extensions with host access to the cookie's domain.
class ShellCookiesEventRouter : public network::mojom::CookieChangeListener {
 public:
  explicit ShellCookiesEventRouter(content::BrowserContext* context);
  ShellCookiesEventRouter(const ShellCookiesEventRouter&) = delete;
  ShellCookiesEventRouter& operator=(const ShellCookiesEventRouter&) = delete;
  ~ShellCookiesEventRouter() override;

  // network::mojom::CookieChangeListener:
  void OnCookieChange(const net::CookieChangeInfo& change) override;

 private:
  void Bind();

  raw_ptr<content::BrowserContext> context_;
  mojo::Receiver<network::mojom::CookieChangeListener> receiver_{this};
};

class CookiesGetFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.get", COOKIES_GET)

  CookiesGetFunction();

 protected:
  ~CookiesGetFunction() override;
  ResponseAction Run() override;

 private:
  void GotCookies(const net::CookieAccessResultList& cookie_list,
                  const net::CookieAccessResultList& excluded_cookies);

  std::optional<api::cookies::Get::Params> params_;
};

class CookiesGetAllFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.getAll", COOKIES_GETALL)

  CookiesGetAllFunction();

 protected:
  ~CookiesGetAllFunction() override;
  ResponseAction Run() override;

 private:
  void GotAllCookies(const net::CookieList& cookie_list);
  void GotURLCookies(const net::CookieAccessResultList& cookie_list,
                     const net::CookieAccessResultList& excluded_cookies);

  std::optional<api::cookies::GetAll::Params> params_;
};

class CookiesSetFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.set", COOKIES_SET)

  CookiesSetFunction();

 protected:
  ~CookiesSetFunction() override;
  ResponseAction Run() override;

 private:
  void CookieSet(net::CookieAccessResult result);
  void GotCookies(const net::CookieAccessResultList& cookie_list,
                  const net::CookieAccessResultList& excluded_cookies);

  std::optional<api::cookies::Set::Params> params_;
  GURL url_;
  bool success_ = false;
};

class CookiesRemoveFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.remove", COOKIES_REMOVE)

  CookiesRemoveFunction();

 protected:
  ~CookiesRemoveFunction() override;
  ResponseAction Run() override;

 private:
  void CookiesRemoved(uint32_t num_deleted);

  std::optional<api::cookies::Remove::Params> params_;
  GURL url_;
};

class CookiesGetAllCookieStoresFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("cookies.getAllCookieStores",
                             COOKIES_GETALLCOOKIESTORES)

 protected:
  ~CookiesGetAllCookieStoresFunction() override = default;
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_COOKIES_API_H_
