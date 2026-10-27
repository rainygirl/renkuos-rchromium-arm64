// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_API_H_
#define CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_API_H_

#include "extensions/browser/extension_function.h"

// R Chromium: chrome.tabs, chrome.webNavigation (the functions; the events
// come from ShellTabs) and chrome.extension, over ShellTabs' one-tab-per-
// window model.

namespace extensions {

class TabsQueryFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.query", TABS_QUERY)

 protected:
  ~TabsQueryFunction() override = default;
  ResponseAction Run() override;
};

class TabsGetFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.get", TABS_GET)

 protected:
  ~TabsGetFunction() override = default;
  ResponseAction Run() override;
};

class TabsGetCurrentFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.getCurrent", TABS_GETCURRENT)

 protected:
  ~TabsGetCurrentFunction() override = default;
  ResponseAction Run() override;
};

class TabsCreateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.create", TABS_CREATE)

 protected:
  ~TabsCreateFunction() override = default;
  ResponseAction Run() override;
};

class TabsUpdateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.update", TABS_UPDATE)

 protected:
  ~TabsUpdateFunction() override = default;
  ResponseAction Run() override;
};

class TabsReloadFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.reload", TABS_RELOAD)

 protected:
  ~TabsReloadFunction() override = default;
  ResponseAction Run() override;
};

class TabsRemoveFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabs.remove", TABS_REMOVE)

 protected:
  ~TabsRemoveFunction() override = default;
  ResponseAction Run() override;
};

class WebNavigationGetFrameFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webNavigation.getFrame", WEBNAVIGATION_GETFRAME)

 protected:
  ~WebNavigationGetFrameFunction() override = default;
  ResponseAction Run() override;
};

class WebNavigationGetAllFramesFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webNavigation.getAllFrames",
                             WEBNAVIGATION_GETALLFRAMES)

 protected:
  ~WebNavigationGetAllFramesFunction() override = default;
  ResponseAction Run() override;
};

class ExtensionIsAllowedIncognitoAccessFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extension.isAllowedIncognitoAccess",
                             EXTENSION_ISALLOWEDINCOGNITOACCESS)

 protected:
  ~ExtensionIsAllowedIncognitoAccessFunction() override = default;
  ResponseAction Run() override;
};

class ExtensionIsAllowedFileSchemeAccessFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("extension.isAllowedFileSchemeAccess",
                             EXTENSION_ISALLOWEDFILESCHEMEACCESS)

 protected:
  ~ExtensionIsAllowedFileSchemeAccessFunction() override = default;
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_BROWSER_EXTENSIONS_SHELL_TABS_API_H_
