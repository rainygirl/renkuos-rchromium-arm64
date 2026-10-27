// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_WEB_CONTENTS_OBSERVER_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_WEB_CONTENTS_OBSERVER_H_

#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/api/declarative_net_request/web_contents_helper.h"
#include "extensions/browser/extension_web_contents_observer.h"

namespace extensions {

// R Chromium: attached to every WebContents (tabs and extension pages), so
// extension frames get their mojo hosts and function dispatcher. The base
// class's defaults are enough. It also does the one job of Chrome's
// TabHelper that the extensions we run need: telling declarativeNetRequest
// which frames committed where, without which allowAllRequests rules (a site
// exempted from blocking) never take effect.
class ShellExtensionWebContentsObserver
    : public ExtensionWebContentsObserver,
      public content::WebContentsUserData<ShellExtensionWebContentsObserver> {
 public:
  ShellExtensionWebContentsObserver(const ShellExtensionWebContentsObserver&) =
      delete;
  ShellExtensionWebContentsObserver& operator=(
      const ShellExtensionWebContentsObserver&) = delete;
  ~ShellExtensionWebContentsObserver() override;

  // Creates (once) and initializes the observer for `web_contents`.
  static void CreateForWebContents(content::WebContents* web_contents);

 private:
  friend class content::WebContentsUserData<ShellExtensionWebContentsObserver>;

  explicit ShellExtensionWebContentsObserver(
      content::WebContents* web_contents);

  declarative_net_request::WebContentsHelper declarative_net_request_helper_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_EXTENSION_WEB_CONTENTS_OBSERVER_H_
