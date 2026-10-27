// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/extensions/shell_action_toolbar.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/shell/browser/extensions/shell_extension_web_contents_observer.h"
#include "content/shell/browser/extensions/shell_tabs.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/test/test_focus_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host_platform.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/wm/core/default_activation_client.h"

namespace extensions {

namespace {

// Chrome's popup limits (ExtensionPopup in //chrome/browser/ui/views).
constexpr gfx::Size kPopupMinSize(25, 25);
constexpr gfx::Size kPopupMaxSize(800, 600);

// Chrome's badge colours when the extension sets none.
constexpr SkColor kDefaultBadgeBackground = SkColorSetRGB(0xDA, 0x00, 0x18);
constexpr SkColor kDefaultBadgeText = SK_ColorWHITE;

// The button for `action` as it should look on tab `tab_id`.
ui::HaikuToolbarAction ToolbarState(const Extension& extension,
                                    ExtensionAction& action,
                                    ExtensionActionIconFactory& icon_factory,
                                    int tab_id) {
  ui::HaikuToolbarAction state;
  state.id = extension.id();
  gfx::Image icon = icon_factory.GetIcon(tab_id);
  if (!icon.IsEmpty()) {
    const SkBitmap& bitmap =
        icon.AsImageSkia().GetRepresentation(1.0f).GetBitmap();
    if (!bitmap.drawsNothing()) {
      state.width = bitmap.width();
      state.height = bitmap.height();
      state.argb.reserve(state.width * state.height);
      for (int y = 0; y < state.height; ++y) {
        for (int x = 0; x < state.width; ++x) {
          // getColor() unpremultiplies: SkColor is what the shim takes.
          state.argb.push_back(bitmap.getColor(x, y));
        }
      }
    }
  }
  state.badge_text = action.GetDisplayBadgeText(tab_id);
  const SkColor background = action.GetBadgeBackgroundColor(tab_id);
  state.badge_background = SkColorGetA(background) == SK_AlphaTRANSPARENT
                               ? kDefaultBadgeBackground
                               : background;
  const SkColor text = action.GetBadgeTextColor(tab_id);
  state.badge_text_color =
      SkColorGetA(text) == SK_AlphaTRANSPARENT ? kDefaultBadgeText : text;
  const std::string title = action.GetTitle(tab_id);
  state.tooltip = title.empty() ? extension.name() : title;
  state.enabled = action.GetIsVisible(tab_id);
  return state;
}

}  // namespace

// A borderless top-level window for an action's popup page, the size the
// page asks for (auto-resize), right-aligned under its button. It closes on
// window.close(), when it loses activation, or when the button is clicked
// again.
class ExtensionPopup : public content::WebContentsDelegate,
                       public content::WebContentsObserver {
 public:
  ExtensionPopup(content::BrowserContext* context,
                 const GURL& url,
                 const gfx::Rect& anchor,
                 base::OnceClosure on_close)
      : anchor_(anchor), on_close_(std::move(on_close)) {
    ui::PlatformWindowInitProperties properties;
    properties.type = ui::PlatformWindowType::kPopup;
    // No frame: the BeAPI window gets no tab and no toolbar.
    properties.remove_standard_frame = true;
    properties.bounds = BoundsFor(kPopupMinSize);
    host_ = std::make_unique<Host>(std::move(properties), this);
    host_->InitHost();
    aura::Window* root = host_->window();
    focus_client_ = std::make_unique<aura::test::TestFocusClient>(root);
    // Deletes itself with the root window, as in ShellPlatformDataAura.
    new wm::DefaultActivationClient(root);
    capture_client_ =
        std::make_unique<aura::client::DefaultCaptureClient>(root);

    content::WebContents::CreateParams params(
        context, content::SiteInstance::CreateForURL(context, url));
    web_contents_ = content::WebContents::Create(params);
    web_contents_->SetDelegate(this);
    Observe(web_contents_.get());
    SetViewType(web_contents_.get(), mojom::ViewType::kExtensionPopup);
    ShellExtensionWebContentsObserver::CreateForWebContents(
        web_contents_.get());

    aura::Window* content = web_contents_->GetNativeView();
    root->AddChild(content);
    content->SetBounds(gfx::Rect(root->bounds().size()));
    content->Show();
    root->Show();
    host_->Show();

    web_contents_->GetController().LoadURL(
        url, content::Referrer(), ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
        std::string());
    web_contents_->Focus();
  }

  ~ExtensionPopup() override {
    Observe(nullptr);
    web_contents_.reset();
    capture_client_.reset();
    focus_client_.reset();
    host_.reset();
  }

  // content::WebContentsDelegate:
  void CloseContents(content::WebContents* source) override { Close(); }

  void ResizeDueToAutoResize(content::WebContents* source,
                             const gfx::Size& new_size) override {
    host_->SetBoundsInPixels(BoundsFor(new_size));
    web_contents_->GetNativeView()->SetBounds(gfx::Rect(new_size));
  }

  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override {
    return false;
  }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted()) {
      return;
    }
    if (content::RenderWidgetHostView* view =
            web_contents_->GetRenderWidgetHostView()) {
      view->EnableAutoResize(kPopupMinSize, kPopupMaxSize);
    }
  }

 private:
  // Closes the popup when it stops being the active window.
  class Host : public aura::WindowTreeHostPlatform {
   public:
    Host(ui::PlatformWindowInitProperties properties, ExtensionPopup* popup)
        : aura::WindowTreeHostPlatform(std::move(properties)), popup_(popup) {}

   protected:
    void OnActivationChanged(bool active) override {
      aura::WindowTreeHostPlatform::OnActivationChanged(active);
      if (active) {
        was_active_ = true;
      } else if (was_active_) {
        popup_->Close();
      }
    }

   private:
    raw_ptr<ExtensionPopup> popup_;
    bool was_active_ = false;
  };

  // Right edges aligned, just below the button, as Chrome places it.
  gfx::Rect BoundsFor(const gfx::Size& size) const {
    return gfx::Rect(anchor_.right() - size.width(), anchor_.bottom() + 1,
                     size.width(), size.height());
  }

  // The owner deletes this, so leave the current call stack first.
  void Close() {
    if (on_close_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(on_close_));
    }
  }

  gfx::Rect anchor_;
  base::OnceClosure on_close_;
  std::unique_ptr<Host> host_;
  std::unique_ptr<aura::test::TestFocusClient> focus_client_;
  std::unique_ptr<aura::client::DefaultCaptureClient> capture_client_;
  std::unique_ptr<content::WebContents> web_contents_;
};

void ShellActionToolbar::IconObserver::OnIconUpdated() {
  toolbar_->Push(id_);
}

ShellActionToolbar::Entry::Entry() = default;
ShellActionToolbar::Entry::~Entry() = default;

ShellActionToolbar::ShellActionToolbar(content::BrowserContext* context)
    : context_(context) {
  ShellActions::AddObserver(this);
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  registry_observation_.Observe(registry);
  for (const auto& extension : registry->enabled_extensions()) {
    OnExtensionLoaded(context, extension.get());
  }
}

ShellActionToolbar::~ShellActionToolbar() {
  ShellActions::RemoveObserver(this);
  popup_.reset();
}

void ShellActionToolbar::AddWindow(gfx::AcceleratedWidget widget,
                                   content::WebContents* tab) {
  windows_[widget] = tab;
  PushAll();
}

void ShellActionToolbar::RemoveWindow(gfx::AcceleratedWidget widget) {
  windows_.erase(widget);
}

void ShellActionToolbar::OnTabChanged(content::WebContents* tab,
                                      bool navigated) {
  if (navigated && tab) {
    const int tab_id = ShellTabs::GetTabId(tab);
    for (auto& [id, entry] : entries_) {
      const Extension* extension =
          ExtensionRegistry::Get(context_)->enabled_extensions().GetByID(id);
      if (!extension) {
        continue;
      }
      if (ExtensionAction* action =
              ExtensionActionManager::Get(context_)->GetExtensionAction(
                  *extension)) {
        action->ClearAllValuesForTab(tab_id);
      }
    }
  }
  PushAll();
}

void ShellActionToolbar::OnActionClicked(content::WebContents* tab,
                                         const std::string& id,
                                         const gfx::Rect& anchor) {
  anchors_[id] = anchor;
  // A second click on the same button closes its popup.
  if (popup_ && popup_extension_id_ == id) {
    ClosePopup();
    return;
  }
  const Extension* extension =
      ExtensionRegistry::Get(context_)->enabled_extensions().GetByID(id);
  if (!extension) {
    return;
  }
  ExtensionAction* action =
      ExtensionActionManager::Get(context_)->GetExtensionAction(*extension);
  if (!action) {
    return;
  }
  const int tab_id = ShellTabs::GetTabId(tab);
  if (!action->GetIsVisible(tab_id)) {
    return;
  }
  if (action->HasPopup(tab_id)) {
    OpenPopup(*extension, action->GetPopupUrl(tab_id), anchor);
    return;
  }

  // No popup: action.onClicked with the tab, as a user gesture.
  EventRouter* router = EventRouter::Get(context_);
  if (!tab || !router) {
    return;
  }
  base::ListValue args;
  args.Append(ShellTabs::CreateTabObject(tab, extension).ToValue());
  router->DispatchEventToExtension(
      id, std::make_unique<Event>(events::ACTION_ON_CLICKED,
                                  "action.onClicked", std::move(args), context_,
                                  std::nullopt, GURL(),
                                  EventRouter::UserGestureState::kEnabled,
                                  /*info=*/nullptr));
}

void ShellActionToolbar::OnActionUpdated(const ExtensionId& extension_id,
                                         int tab_id) {
  Push(extension_id,
       tab_id == ExtensionAction::kDefaultTabId ? -1 : tab_id);
}

bool ShellActionToolbar::OnOpenPopupRequested(const ExtensionId& extension_id) {
  const Extension* extension =
      ExtensionRegistry::Get(context_)->enabled_extensions().GetByID(
          extension_id);
  ExtensionAction* action =
      extension ? ExtensionActionManager::Get(context_)->GetExtensionAction(
                      *extension)
                : nullptr;
  content::WebContents* active = ShellTabs::GetActiveTab(context_);
  const int tab_id =
      active ? ShellTabs::GetTabId(active) : ExtensionAction::kDefaultTabId;
  if (!action || !action->HasPopup(tab_id) || windows_.empty()) {
    return false;
  }
  // Under the button if it has been clicked before; otherwise near the top
  // right of the screen area the toolbar is in.
  auto it = anchors_.find(extension_id);
  const gfx::Rect anchor =
      it != anchors_.end() ? it->second : gfx::Rect(600, 0, 24, 24);
  OpenPopup(*extension, action->GetPopupUrl(tab_id), anchor);
  return true;
}

void ShellActionToolbar::OnExtensionLoaded(
    content::BrowserContext* browser_context,
    const Extension* extension) {
  ExtensionAction* action =
      ExtensionActionManager::Get(context_)->GetExtensionAction(*extension);
  if (!action) {
    return;
  }
  Entry& entry = entries_[extension->id()];
  entry.icon_observer = std::make_unique<IconObserver>(this, extension->id());
  entry.icon_factory = std::make_unique<ExtensionActionIconFactory>(
      extension, action, entry.icon_observer.get());
  Push(extension->id());
}

void ShellActionToolbar::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const Extension* extension,
    UnloadedExtensionReason reason) {
  if (popup_extension_id_ == extension->id()) {
    ClosePopup();
  }
  if (entries_.erase(extension->id())) {
    for (const auto& [widget, tab] : windows_) {
      ui::HaikuToolbarRemoveAction(widget, extension->id());
    }
  }
}

void ShellActionToolbar::Push(const ExtensionId& id, int only_tab_id) {
  auto entry = entries_.find(id);
  const Extension* extension =
      ExtensionRegistry::Get(context_)->enabled_extensions().GetByID(id);
  if (entry == entries_.end() || !extension) {
    return;
  }
  ExtensionAction* action =
      ExtensionActionManager::Get(context_)->GetExtensionAction(*extension);
  if (!action) {
    return;
  }
  for (const auto& [widget, tab] : windows_) {
    const int tab_id = ShellTabs::GetTabId(tab);
    if (only_tab_id >= 0 && tab_id != only_tab_id) {
      continue;
    }
    ui::HaikuToolbarSetAction(
        widget, ToolbarState(*extension, *action,
                             *entry->second.icon_factory, tab_id));
  }
}

void ShellActionToolbar::PushAll() {
  for (const auto& [id, entry] : entries_) {
    Push(id);
  }
}

void ShellActionToolbar::OpenPopup(const Extension& extension,
                                   const GURL& url,
                                   const gfx::Rect& anchor) {
  ClosePopup();
  popup_extension_id_ = extension.id();
  popup_ = std::make_unique<ExtensionPopup>(
      context_, url, anchor,
      base::BindOnce(&ShellActionToolbar::ClosePopup,
                     weak_factory_.GetWeakPtr()));
}

void ShellActionToolbar::ClosePopup() {
  popup_.reset();
  popup_extension_id_.clear();
}

}  // namespace extensions
