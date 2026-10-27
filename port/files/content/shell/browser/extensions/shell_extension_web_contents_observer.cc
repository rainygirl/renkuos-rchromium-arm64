// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_extension_web_contents_observer.h"

namespace extensions {

ShellExtensionWebContentsObserver::ShellExtensionWebContentsObserver(
    content::WebContents* web_contents)
    : ExtensionWebContentsObserver(web_contents),
      content::WebContentsUserData<ShellExtensionWebContentsObserver>(
          *web_contents),
      declarative_net_request_helper_(web_contents) {}

ShellExtensionWebContentsObserver::~ShellExtensionWebContentsObserver() =
    default;

// static
void ShellExtensionWebContentsObserver::CreateForWebContents(
    content::WebContents* web_contents) {
  content::WebContentsUserData<
      ShellExtensionWebContentsObserver>::CreateForWebContents(web_contents);
  FromWebContents(web_contents)->Initialize();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ShellExtensionWebContentsObserver);

}  // namespace extensions
