// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_API_CLIENT_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_API_CLIENT_H_

#include <memory>

#include "extensions/browser/api/extensions_api_client.h"

namespace extensions {

// R Chromium: the API embedder hooks. ExtensionsAPIClient's defaults do,
// except that the webstorePrivate factory and runtime messaging insist on
// delegates.
class ShellExtensionsAPIClient : public ExtensionsAPIClient {
 public:
  ShellExtensionsAPIClient();
  ShellExtensionsAPIClient(const ShellExtensionsAPIClient&) = delete;
  ShellExtensionsAPIClient& operator=(const ShellExtensionsAPIClient&) = delete;
  ~ShellExtensionsAPIClient() override;

  // ExtensionsAPIClient:
  WebstorePrivateAPIDelegate* GetWebstorePrivateAPIDelegate() override;
  MessagingDelegate* GetMessagingDelegate() override;

 private:
  std::unique_ptr<WebstorePrivateAPIDelegate> webstore_private_api_delegate_;
  std::unique_ptr<MessagingDelegate> messaging_delegate_;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSIONS_API_CLIENT_H_
