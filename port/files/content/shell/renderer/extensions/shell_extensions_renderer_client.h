// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_RENDERER_EXTENSIONS_SHELL_EXTENSIONS_RENDERER_CLIENT_H_
#define CONTENT_SHELL_RENDERER_EXTENSIONS_SHELL_EXTENSIONS_RENDERER_CLIENT_H_

#include "extensions/renderer/extensions_renderer_client.h"

namespace content {
class RenderFrame;
}

namespace extensions {

// R Chromium: content_shell's renderer side of extensions. The base class
// owns the Dispatcher and does the work; ShellContentRendererClient forwards
// its frame, script-injection and service-worker hooks to it.
class ShellExtensionsRendererClient : public ExtensionsRendererClient {
 public:
  ShellExtensionsRendererClient();
  ShellExtensionsRendererClient(const ShellExtensionsRendererClient&) = delete;
  ShellExtensionsRendererClient& operator=(
      const ShellExtensionsRendererClient&) = delete;
  ~ShellExtensionsRendererClient() override;

  // Creates the process-wide instance (it installs itself with
  // ExtensionsRendererClient::Set()) together with the ExtensionsClient.
  static void Create();

  // ExtensionsRendererClient::RenderFrameCreated() with a per-frame
  // BinderRegistry, which content_shell does not otherwise keep.
  static void OnRenderFrameCreated(content::RenderFrame* render_frame);

  // ExtensionsRendererClient:
  bool IsIncognitoProcess() const override;
  int GetLowestIsolatedWorldId() const override;
};

}  // namespace extensions

#endif  // CONTENT_SHELL_RENDERER_EXTENSIONS_SHELL_EXTENSIONS_RENDERER_CLIENT_H_
