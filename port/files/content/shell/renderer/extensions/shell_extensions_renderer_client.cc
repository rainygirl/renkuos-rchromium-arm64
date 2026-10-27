// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/renderer/extensions/shell_extensions_renderer_client.h"

#include <memory>
#include <string>

#include "base/no_destructor.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/shell/common/extensions/shell_extensions_client.h"
#include "extensions/renderer/api/core_extensions_renderer_api_provider.h"
#include "services/service_manager/public/cpp/binder_registry.h"

namespace extensions {

namespace {

// Holds the frame's BinderRegistry for the extension frame observers and
// answers the browser's interface requests from it. Deletes itself with the
// frame.
class FrameInterfaces : public content::RenderFrameObserver {
 public:
  explicit FrameInterfaces(content::RenderFrame* render_frame)
      : content::RenderFrameObserver(render_frame) {}

  service_manager::BinderRegistry* registry() { return &registry_; }

  // content::RenderFrameObserver:
  void OnInterfaceRequestForFrame(
      const std::string& interface_name,
      mojo::ScopedMessagePipeHandle* interface_pipe) override {
    registry_.TryBindInterface(interface_name, interface_pipe);
  }
  void OnDestruct() override { delete this; }

 private:
  service_manager::BinderRegistry registry_;
};

}  // namespace

ShellExtensionsRendererClient::ShellExtensionsRendererClient() {
  ExtensionsRendererClient::Set(this);
  AddAPIProvider(std::make_unique<CoreExtensionsRendererAPIProvider>());
}

ShellExtensionsRendererClient::~ShellExtensionsRendererClient() {
  ExtensionsRendererClient::Set(nullptr);
}

// static
void ShellExtensionsRendererClient::Create() {
  ShellExtensionsClient::EnsureInitialized();
  static base::NoDestructor<ShellExtensionsRendererClient> client;
}

// static
void ShellExtensionsRendererClient::OnRenderFrameCreated(
    content::RenderFrame* render_frame) {
  auto* interfaces = new FrameInterfaces(render_frame);
  Get()->RenderFrameCreated(render_frame, interfaces->registry());
}

bool ShellExtensionsRendererClient::IsIncognitoProcess() const {
  // Extensions only run in the regular profile.
  return false;
}

int ShellExtensionsRendererClient::GetLowestIsolatedWorldId() const {
  // content_shell uses no isolated worlds of its own past content's.
  return content::ISOLATED_WORLD_ID_CONTENT_END + 1;
}

}  // namespace extensions
