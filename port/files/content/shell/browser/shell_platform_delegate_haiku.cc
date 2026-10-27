// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// R Chromium's Haiku ShellPlatformDelegate. It keeps content_shell's aura
// content hosting -- the ozone Haiku backend renders the page into the BWindow
// through the aura WindowTreeHost -- and adds the native BeAPI toolbar that the
// ozone layer draws, wired to Shell navigation through the toolbar bridge.
//
// This replaces shell_platform_delegate_aura.cc when is_haiku; it therefore
// provides the same out-of-line ShellPlatformDelegate definitions the aura
// file does, plus the toolbar wiring.

#include "content/shell/browser/shell_platform_delegate.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "content/public/browser/render_widget_host_view.h"
#include <sys/stat.h>
#include <unistd.h>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "content/browser/manifest/manifest_manager_host.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/manifest/display_mode.mojom.h"
#include "third_party/blink/public/mojom/manifest/manifest.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/extensions/shell_extensions.h"
#include "content/shell/browser/shell_platform_data_aura.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_observer.h"
#include "ui/ozone/platform/haiku/haiku_toolbar_bridge.h"
#include "url/gurl.h"

namespace content {

namespace {

// ----------------------------------------------------------------- install
//
// Chrome's install button lives in chrome/browser/web_applications, which
// content_shell does not have. It does not need it: ManifestManagerHost hands
// over the parsed manifest and WebContents::DownloadImage() fetches its
// icons, so the whole feature fits here.
//
// Installing writes ~/config/non-packaged/apps/<Name>/<Name>, a shell script
// that runs this same content_shell at the manifest's start_url with the
// toolbar off, plus a symlink from ~/config/settings/deskbar/menu/Applications
// so Deskbar lists it. A script and not a copied binary: content_shell is
// 300 MB and an installed app is the same browser pointed at one URL.

// Chrome's installability test minus the service worker. Chrome requires a
// fetch handler because an installed app there is expected to work offline;
// here it is a Deskbar entry that opens a URL, which is useful either way,
// and requiring one would rule out most of what anyone would install here.
bool IsInstallable(const blink::mojom::Manifest& manifest,
                   std::string* why_not) {
  if (!manifest.start_url.is_valid()) {
    *why_not = "no valid start_url";
    return false;
  }
  if (!manifest.name.has_value() && !manifest.short_name.has_value()) {
    *why_not = "no name or short_name";
    return false;
  }
  switch (manifest.display) {
    case blink::mojom::DisplayMode::kStandalone:
    case blink::mojom::DisplayMode::kFullscreen:
    case blink::mojom::DisplayMode::kMinimalUi:
      break;
    default:
      *why_not = "display is not standalone/fullscreen/minimal-ui";
      return false;
  }
  for (const auto& icon : manifest.icons) {
    if (!icon.src.is_valid()) {
      continue;
    }
    for (const auto& purpose : icon.purpose) {
      if (purpose == blink::mojom::ManifestImageResource_Purpose::ANY ||
          purpose == blink::mojom::ManifestImageResource_Purpose::MASKABLE) {
        return true;
      }
    }
  }
  *why_not = "no usable icon";
  return false;
}

std::string AppNameOf(const blink::mojom::Manifest& manifest) {
  if (manifest.name.has_value()) {
    return base::UTF16ToUTF8(manifest.name.value());
  }
  if (manifest.short_name.has_value()) {
    return base::UTF16ToUTF8(manifest.short_name.value());
  }
  return std::string();
}

// A name that can be a directory and a Deskbar entry.
std::string SanitizeAppName(const std::string& name, const GURL& start_url) {
  std::string out;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '\n' || c == '\r' ||
        c == '\t') {
      out += ' ';
    } else if (static_cast<unsigned char>(c) >= 0x20) {
      out += c;
    }
  }
  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);
  while (!out.empty() && out[0] == '.') {
    out.erase(0, 1);
  }
  if (out.size() > 48) {
    out.resize(48);
  }
  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);
  if (out.empty()) {
    out = start_url.host();
  }
  if (out.empty()) {
    out = "Web App";
  }
  return out;
}

base::FilePath AppDirFor(const std::string& app_name) {
  const char* home = getenv("HOME");
  if (home == nullptr) {
    home = "/boot/home";
  }
  return base::FilePath(home)
      .Append("config")
      .Append("non-packaged")
      .Append("apps")
      .Append(app_name);
}

// A value as one single-quoted shell word. The app name comes from the
// site's web app manifest and the start URL is the site's own, and inside
// double quotes "$(...)" or a stray '"' in either would run as a command
// whenever the installed app is opened.
std::string ShellQuote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  return out + "'";
}

bool WriteLauncher(const base::FilePath& dir,
                   const std::string& app_name,
                   const GURL& start_url,
                   std::string* error) {
  base::FilePath shell_path;
  if (!base::PathService::Get(base::FILE_EXE, &shell_path)) {
    *error = "cannot find my own path";
    return false;
  }
  std::string script;
  script += "#!/bin/sh\n";
  script += "# " + app_name + "\n";
  script += "#\n";
  script += "# Installed by R Chromium from " + start_url.spec() + "\n";
  script += "# Delete this directory to uninstall.\n\n";
  script += "SHELL_BIN=" + ShellQuote(shell_path.value()) + "\n";
  script += "APPDIR=$(dirname \"$SHELL_BIN\")\n\n";
  script += "LIBRARY_PATH=\"$APPDIR/lib:/boot/system/lib\"\n";
  script += "export LIBRARY_PATH\n\n";
  script += "# No toolbar: an installed app is a window onto one site.\n";
  script += "RCH_NO_TOOLBAR=1\n";
  script += "export RCH_NO_TOOLBAR\n\n";
  script += "# The window keeps the app's name rather than following the page.\n";
  script += "RCH_APP_NAME=" + ShellQuote(app_name) + "\n";
  script += "export RCH_APP_NAME\n\n";
  script += "# Haiku's default of 256 descriptors is not enough once\n";
  script += "# renderers run in their own processes.\n";
  script += "ulimit -n 8192 2>/dev/null\n\n";
  script += "exec \"$SHELL_BIN\" \\\n";
  script += "\t--ozone-platform=haiku \\\n";
  script += "\t--disable-gpu \\\n";
  script += "\t--in-process-gpu \\\n";
  script += "\t--disable-gpu-compositing \\\n";
  script += "\t--user-data-dir=" + ShellQuote(dir.value() + "/profile") + " \\\n";
  script += "\t" + ShellQuote(start_url.spec()) + " \"$@\"\n";

  const base::FilePath launcher = dir.Append(app_name);
  if (!base::WriteFile(launcher, script)) {
    *error = "cannot write " + launcher.value();
    return false;
  }
  if (chmod(launcher.value().c_str(), 0755) != 0) {
    *error = "cannot chmod " + launcher.value();
    return false;
  }
  return true;
}

// Deskbar does not scan ~/config/non-packaged/apps. It lists the symlinks in
// ~/config/settings/deskbar/menu/Applications, which is how every
// non-packaged application on a Haiku install is registered. Without this the
// app exists and cannot be found.
bool LinkIntoDeskbar(const base::FilePath& launcher,
                     const std::string& app_name,
                     std::string* error) {
  const char* home = getenv("HOME");
  if (home == nullptr) {
    home = "/boot/home";
  }
  const base::FilePath menu = base::FilePath(home)
                                  .Append("config")
                                  .Append("settings")
                                  .Append("deskbar")
                                  .Append("menu")
                                  .Append("Applications");
  base::File::Error mkdir_error = base::File::FILE_OK;
  if (!base::CreateDirectoryAndGetError(menu, &mkdir_error)) {
    *error = "cannot create " + menu.value();
    return false;
  }
  const base::FilePath link = menu.Append(app_name);
  unlink(link.value().c_str());
  if (symlink(launcher.value().c_str(), link.value().c_str()) != 0) {
    *error = "cannot link " + link.value();
    return false;
  }
  return true;
}

// The largest square icon the manifest declares; a vector ("any") beats any
// raster, since it rasterises to whatever is asked for.
GURL BestIconUrl(const blink::mojom::Manifest& manifest) {
  GURL best;
  int best_area = -1;
  for (const auto& icon : manifest.icons) {
    if (!icon.src.is_valid()) {
      continue;
    }
    bool usable = false;
    for (const auto& purpose : icon.purpose) {
      if (purpose == blink::mojom::ManifestImageResource_Purpose::ANY ||
          purpose == blink::mojom::ManifestImageResource_Purpose::MASKABLE) {
        usable = true;
        break;
      }
    }
    if (!usable) {
      continue;
    }
    if (icon.sizes.empty()) {
      return icon.src;
    }
    for (const auto& size : icon.sizes) {
      if (size.width() != size.height()) {
        continue;
      }
      const int area = size.width() * size.height();
      if (area > best_area) {
        best_area = area;
        best = icon.src;
      }
    }
  }
  if (!best.is_valid()) {
    for (const auto& icon : manifest.icons) {
      if (icon.src.is_valid()) {
        return icon.src;
      }
    }
  }
  return best;
}

void OnIconDownloaded(const base::FilePath& launcher,
                      int /*id*/,
                      int http_status_code,
                      const GURL& image_url,
                      const std::vector<SkBitmap>& bitmaps,
                      const std::vector<gfx::Size>& /*sizes*/) {
  if (bitmaps.empty()) {
    LOG(ERROR) << "[RCH] icon download failed (" << http_status_code
               << ") for " << image_url.spec();
    return;
  }
  const SkBitmap* best = &bitmaps[0];
  for (const SkBitmap& bitmap : bitmaps) {
    if (bitmap.width() * bitmap.height() > best->width() * best->height()) {
      best = &bitmap;
    }
  }
  const int w = best->width();
  const int h = best->height();
  if (w <= 0 || h <= 0) {
    return;
  }
  std::vector<uint32_t> argb(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      argb[static_cast<size_t>(y) * w + x] = best->getColor(x, y);
    }
  }
  const bool ok = ui::HaikuSetFileIcon(launcher.value(), argb.data(), w, h);
  LOG(ERROR) << "[RCH] icon " << w << "x" << h << " -> " << launcher.value()
             << ": " << (ok ? "set" : "FAILED");
}

void DoInstall(WebContents* web_contents,
               const blink::mojom::Manifest& manifest) {
  std::string why_not;
  if (!IsInstallable(manifest, &why_not)) {
    LOG(ERROR) << "[RCH] install refused: " << why_not;
    return;
  }
  const std::string app_name =
      SanitizeAppName(AppNameOf(manifest), manifest.start_url);
  const base::FilePath dir = AppDirFor(app_name);

  base::File::Error mkdir_error = base::File::FILE_OK;
  if (!base::CreateDirectoryAndGetError(dir, &mkdir_error)) {
    LOG(ERROR) << "[RCH] install failed: cannot create " << dir.value();
    return;
  }
  std::string error;
  if (!WriteLauncher(dir, app_name, manifest.start_url, &error)) {
    LOG(ERROR) << "[RCH] install failed: " << error;
    return;
  }
  const base::FilePath launcher = dir.Append(app_name);
  std::string link_error;
  const bool linked = LinkIntoDeskbar(launcher, app_name, &link_error);
  LOG(ERROR) << "[RCH] installed \"" << app_name << "\" -> "
             << launcher.value()
             << " (deskbar: " << (linked ? "listed" : link_error) << ")";

  // After the launcher, so a failed download leaves a working app.
  const GURL icon_url = BestIconUrl(manifest);
  if (icon_url.is_valid() && web_contents != nullptr) {
    web_contents->DownloadImage(
        icon_url, /*is_favicon=*/false, /*preferred_size=*/gfx::Size(128, 128),
        /*max_bitmap_size=*/512, /*bypass_cache=*/false,
        base::BindOnce(&OnIconDownloaded, launcher));
  }
}

// One per Shell. Receives toolbar events (on the UI thread, posted by the
// ozone bridge) and turns them into Shell navigation.
class ShellToolbarObserver final : public ui::HaikuToolbarObserver {
 public:
  explicit ShellToolbarObserver(Shell* shell) : shell_(shell) {}

  void OnNavigateBack() override { shell_->GoBackOrForward(-1); }
  void OnNavigateForward() override { shell_->GoBackOrForward(1); }

  void OnReloadOrStop() override {
    if (shell_->web_contents() && shell_->web_contents()->IsLoading()) {
      shell_->Stop();
    } else {
      shell_->Reload();
    }
  }

  void OnNavigateToURL(const std::string& text) override {
    if (text.empty()) {
      return;
    }
    GURL url(text);
    // A bare "example.com" has no scheme and parses as invalid; retry as https.
    if (!url.is_valid() || !url.has_scheme()) {
      url = GURL("https://" + text);
    }
    if (url.is_valid()) {
      shell_->LoadURL(url);
    }
  }

  void OnExtensionActionClicked(const std::string& id,
                                const gfx::Rect& anchor) override {
    extensions::ShellExtensions::ToolbarActionClicked(shell_->web_contents(),
                                                      id, anchor);
  }

  void OnInstall() override {
    if (shell_ == nullptr || shell_->web_contents() == nullptr) {
      return;
    }
    // Ask for the manifest again rather than caching it from the last load:
    // the page may have navigated since the button appeared, and installing
    // the previous site's app would be a surprise.
    WebContents* contents = shell_->web_contents();
    ManifestManagerHost* host =
        ManifestManagerHost::GetOrCreateForPage(contents->GetPrimaryPage());
    if (host == nullptr) {
      return;
    }
    host->GetManifest(base::BindOnce(
        [](WebContents* contents, blink::mojom::ManifestRequestResult,
           const GURL&, blink::mojom::ManifestPtr manifest) {
          if (manifest) {
            DoInstall(contents, *manifest);
          }
        },
        contents));
  }

  // Add/show bookmarks live entirely in the shim's toolbar now (BookmarkStore
  // + BookmarksWindow), so the delegate only needs to navigate.

 private:
  raw_ptr<Shell> shell_;
};

// content_shell's own FillLayout sizes the WebContents windows to the host
// only once, at creation (the upstream shell is never resized by a user), so
// a BWindow resize would leave the page at its original size. Follow every
// host resize by refitting each child window to the host.
class HostResizeObserver : public aura::WindowTreeHostObserver {
 public:
  void OnHostResized(aura::WindowTreeHost* host) override {
    aura::Window* root = host->window();
    const gfx::Rect fill(root->bounds().size());
    for (aura::Window* child : root->children()) {
      child->SetBounds(fill);
    }
  }
};

// Closes the window's Shell when the BWindow's close box is pressed.
//
// ShimWindow::QuitRequested() -> HaikuEventBridge::OnQuitRequested() ->
// HaikuWindow::OnCloseRequestedFromWindowThread() ->
// PlatformWindowDelegate::OnCloseRequest() ->
// WindowTreeHost::OnHostCloseRequested() -> here. Each Shell has its own
// window; Shell quits the message loop when the last one goes.
class HostCloseObserver : public aura::WindowTreeHostObserver {
 public:
  explicit HostCloseObserver(Shell* shell) : shell_(shell) {}

  void OnHostCloseRequested(aura::WindowTreeHost* host) override {
    // Not synchronously: Shell::Close() tears down what is iterating this
    // observer list right now.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&HostCloseObserver::CloseShell, shell_));
  }

 private:
  static void CloseShell(Shell* shell) {
    // A second press before this ran may already have closed it.
    if (std::ranges::find(Shell::windows(), shell) != Shell::windows().end()) {
      shell->Close();
    }
  }

  raw_ptr<Shell> shell_;
};

void FocusContentsSoon(base::WeakPtr<WebContents> contents, int attempts_left);

// Focus the page, retrying on later turns of the loop while there is still
// nothing focusable. RenderWidgetHostViewAura::Focus() is a no-op until the
// view has a focus client and its window can take focus, and the view itself
// is created and shown asynchronously, so the first attempt -- made from
// SetContents -- routinely comes too early.
void FocusContentsNow(base::WeakPtr<WebContents> contents, int attempts_left) {
  WebContents* web_contents = contents.get();
  if (!web_contents) {
    return;
  }
  RenderWidgetHostView* view = web_contents->GetRenderWidgetHostView();
  if (view && view->HasFocus()) {
    return;
  }
  web_contents->Focus();
  view = web_contents->GetRenderWidgetHostView();
  if (view && view->HasFocus()) {
    return;
  }
  if (attempts_left <= 0) {
    LOG(ERROR) << "haiku: web contents never took focus; view=" << view;
    return;
  }
  FocusContentsSoon(std::move(contents), attempts_left - 1);
}

void FocusContentsSoon(base::WeakPtr<WebContents> contents, int attempts_left) {
  // Spread the retries over real time: what is being waited for is the view's
  // creation and the host window's Show(), and posting without a delay would
  // burn every attempt in the same millisecond.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&FocusContentsNow, std::move(contents), attempts_left),
      base::Milliseconds(50));
}

}  // namespace

struct ShellPlatformDelegate::ShellData {
  gfx::NativeWindow window = gfx::NativeWindow();
  gfx::AcceleratedWidget widget = gfx::kNullAcceleratedWidget;
  // The Shell's native window: its own, or the startup one (see
  // PlatformData), which `own_aura` then leaves null. `own_aura` comes first
  // so that `aura` is cleared before the window it may point to is freed.
  std::unique_ptr<ShellPlatformDataAura> own_aura;
  raw_ptr<ShellPlatformDataAura> aura = nullptr;
  std::unique_ptr<HostResizeObserver> resize_observer;
  std::unique_ptr<HostCloseObserver> close_observer;
  std::unique_ptr<ShellToolbarObserver> toolbar_observer;
  // Enabled state Shell reports one button at a time; the shim wants both.
  bool back_enabled = false;
  bool forward_enabled = false;
};

// The window made at startup. It also owns the process's display::Screen,
// so it lives as long as the delegate: it serves one Shell at a time, is
// hidden when that Shell closes, and goes to the next new one. Every other
// Shell gets a window of its own.
struct ShellPlatformDelegate::PlatformData {
  std::unique_ptr<ShellPlatformDataAura> aura;
  raw_ptr<Shell> aura_shell = nullptr;
};

ShellPlatformDelegate::ShellPlatformDelegate() = default;
ShellPlatformDelegate::~ShellPlatformDelegate() = default;

void ShellPlatformDelegate::Initialize(const gfx::Size& default_window_size) {
  platform_ = std::make_unique<PlatformData>();
  platform_->aura =
      std::make_unique<ShellPlatformDataAura>(default_window_size);
}

void ShellPlatformDelegate::CreatePlatformWindow(
    Shell* shell,
    const gfx::Size& initial_size) {
  DCHECK(!shell_data_map_.contains(shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (!platform_->aura_shell) {
    // The startup window is free. Upstream resizes it with ResizeWindow(),
    // i.e. gfx::Rect(size) at (0,0), which would yank the BWindow back to
    // the top-left corner; keep the origin where the user left it.
    platform_->aura_shell = shell;
    shell_data.aura = platform_->aura.get();
    aura::WindowTreeHost* host = shell_data.aura->host();
    gfx::Rect bounds = host->GetBoundsInPixels();
    bounds.set_size(initial_size);
    host->SetBoundsInPixels(bounds);
  } else {
    shell_data.own_aura = std::make_unique<ShellPlatformDataAura>(initial_size);
    shell_data.aura = shell_data.own_aura.get();
    // Cascade new windows from the top-left, as Tracker does.
    const int step = 30 * static_cast<int>(shell_data_map_.size() % 8);
    shell_data.aura->host()->SetBoundsInPixels(
        gfx::Rect(gfx::Point(step, step), initial_size));
  }
  aura::WindowTreeHost* host = shell_data.aura->host();
  shell_data.resize_observer = std::make_unique<HostResizeObserver>();
  shell_data.close_observer = std::make_unique<HostCloseObserver>(shell);
  host->AddObserver(shell_data.resize_observer.get());
  host->AddObserver(shell_data.close_observer.get());

  shell_data.window = host->window();
  shell_data.widget = host->GetAcceleratedWidget();
  shell_data.toolbar_observer = std::make_unique<ShellToolbarObserver>(shell);
  ui::SetHaikuToolbarObserver(shell_data.widget,
                              shell_data.toolbar_observer.get());
}

gfx::NativeWindow ShellPlatformDelegate::GetNativeWindow(Shell* shell) {
  DCHECK(shell_data_map_.contains(shell));
  return shell_data_map_[shell].window;
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  DCHECK(shell_data_map_.contains(shell));
  ShellData& shell_data = shell_data_map_[shell];
  ui::SetHaikuToolbarObserver(shell_data.widget, nullptr);
  extensions::ShellExtensions::ToolbarWindowDestroyed(shell_data.widget);
  aura::WindowTreeHost* host = shell_data.aura->host();
  host->RemoveObserver(shell_data.resize_observer.get());
  host->RemoveObserver(shell_data.close_observer.get());
  if (platform_->aura_shell == shell) {
    // Keep the startup window for the next Shell (it owns the Screen).
    host->Hide();
    platform_->aura_shell = nullptr;
  }
  shell_data_map_.erase(shell);
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  // Extensions see every Shell's contents (popups included): their frames
  // get the extension frame hosts and content scripts.
  extensions::ShellExtensions::WebContentsCreated(shell->web_contents());

  ShellData& shell_data = shell_data_map_[shell];
  aura::Window* content = shell->web_contents()->GetNativeView();
  aura::Window* parent = shell_data.aura->host()->window();
  if (!parent->Contains(content)) {
    parent->AddChild(content);
  }
  content->SetBounds(gfx::Rect(parent->bounds().size()));
  content->Show();
  extensions::ShellExtensions::ToolbarWindowCreated(shell_data.widget,
                                                    shell->web_contents());

  // The aura path never shows the host (native) window on its own -- unlike the
  // views delegate, which calls GetHost()->Show() explicitly. Without this the
  // BWindow stays hidden and only the desktop is visible.
  shell_data.aura->ShowWindow();

  // Nothing focuses the contents in this configuration: the aura delegate is
  // upstream's web-test one, where focus arrives from the test runner, and the
  // only local focus client is the shell's own. Until the user clicked the
  // page, document.hasFocus() was false and every key was dropped -- the
  // click works because RenderWidgetHostViewEventHandler calls
  // SetKeyboardFocus() on mouse-down.
  //
  // Focusing right here is not enough: WebContentsViewAura::Focus() reaches
  // RenderWidgetHostViewAura::Focus(), which does nothing unless the view
  // already has a focus client and its window can take focus, and neither is
  // settled while SetContents is still running. Posting the focus makes it
  // run once the window tree, the host's Show() and the view's own Show()
  // have all been processed.
  FocusContentsSoon(shell->web_contents()->GetWeakPtr(), 30);
}

void ShellPlatformDelegate::ResizeWebContent(Shell* shell,
                                             const gfx::Size& content_size) {
  shell->web_contents()->GetRenderWidgetHostView()->SetSize(content_size);
}

void ShellPlatformDelegate::EnableUIControl(Shell* shell,
                                            UIControl control,
                                            bool is_enabled) {
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    return;
  }
  ShellData& shell_data = it->second;
  if (control == BACK_BUTTON) {
    shell_data.back_enabled = is_enabled;
  } else if (control == FORWARD_BUTTON) {
    shell_data.forward_enabled = is_enabled;
  } else {
    // STOP_BUTTON is driven by SetIsLoading instead.
    return;
  }
  ui::HaikuToolbarSetNavigationEnabled(
      shell_data.widget, shell_data.back_enabled, shell_data.forward_enabled);
}

void ShellPlatformDelegate::SetAddressBarURL(Shell* shell, const GURL& url) {
  auto it = shell_data_map_.find(shell);
  if (it != shell_data_map_.end()) {
    ui::HaikuToolbarSetAddress(it->second.widget, url.spec());
    // Shell only retitles from TitleWasSet, which WebContents skips when a
    // restored history entry already carries its title (Back into the
    // back-forward cache), leaving the previous page's title up. The URL
    // changes on every such navigation, so retitle here as well.
    ui::HaikuToolbarSetTitle(
        it->second.widget,
        base::UTF16ToUTF8(shell->web_contents()->GetTitle()));
  }
}

void ShellPlatformDelegate::SetIsLoading(Shell* shell, bool loading) {
  auto it = shell_data_map_.find(shell);
  if (it == shell_data_map_.end()) {
    return;
  }
  ui::HaikuToolbarSetLoading(it->second.widget, loading);

  // Ask once per load, when it finishes: asking earlier gets the manifest of
  // whatever was there before.
  if (loading || shell->web_contents() == nullptr) {
    return;
  }
  const gfx::AcceleratedWidget widget = it->second.widget;
  ManifestManagerHost* host = ManifestManagerHost::GetOrCreateForPage(
      shell->web_contents()->GetPrimaryPage());
  if (host == nullptr) {
    return;
  }
  host->GetManifest(base::BindOnce(
      [](gfx::AcceleratedWidget widget, blink::mojom::ManifestRequestResult,
         const GURL& manifest_url, blink::mojom::ManifestPtr manifest) {
        std::string why_not = "no manifest";
        bool ok = false;
        std::string name;
        if (manifest) {
          ok = IsInstallable(*manifest, &why_not);
          name = AppNameOf(*manifest);
        }
        LOG(ERROR) << "[RCH] manifest url=" << manifest_url.spec()
                   << " installable=" << (ok ? 1 : 0) << " name=\"" << name
                   << "\"" << (ok ? "" : " why=") << (ok ? "" : why_not);
        ui::HaikuToolbarSetInstallable(widget, ok, name);
      },
      widget));
}

void ShellPlatformDelegate::SetTitle(Shell* shell,
                                     const std::u16string& title) {
  auto it = shell_data_map_.find(shell);
  if (it != shell_data_map_.end()) {
    ui::HaikuToolbarSetTitle(it->second.widget, base::UTF16ToUTF8(title));
  }
}

void ShellPlatformDelegate::MainFrameCreated(Shell* shell,
                                             RenderFrameHost* main_frame) {}

bool ShellPlatformDelegate::DestroyShell(Shell* shell) {
  return false;  // Shell destroys itself.
}

}  // namespace content
