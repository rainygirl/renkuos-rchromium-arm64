# Extensions in R Chromium

R Chromium is `content_shell`, which has no extension system. This adds one:
Chromium's own `//extensions` layer, embedded in content_shell, plus the few
`//chrome`-only APIs an extension needs, with extension icons in the native
BeAPI toolbar. The first target is
[Brave Shields](https://github.com/gasanache/brave-shields-extension)
(MV3, `declarativeNetRequest`-based ad blocking).

## What the target extension needs

| API | Lives in | Here |
| --- | --- | --- |
| `runtime`, `storage` (local, session), `scripting`, `declarativeNetRequest` (+ feedback), content scripts, web-accessible resources, MV3 service worker | `//extensions` | linked as is |
| `tabs` (`query`, `reload`, `create`, `onRemoved`) | `//chrome` | own implementation over content_shell's `Shell` windows |
| `action` (`setBadgeText`, `setBadgeBackgroundColor`, popup) | `//chrome` (model in `//extensions`) | own implementation, drawn in the BeAPI toolbar |
| `webNavigation` (`onCommitted`, `getFrame`) | `//chrome` | own implementation |
| `cookies` (`get`, `getAll`, `set`, `remove`) | `//chrome` | own implementation |
| `extension.isAllowedIncognitoAccess` | `//chrome` | own implementation (always false: no incognito) |

The schemas of the `//chrome` APIs are compiled from
`chrome/common/extensions/api/*.json` into our own target; none of `//chrome`'s
code is linked.

## Pieces

`app_shell`, the old minimal embedder, is gone from Chromium, so this follows
what `//chrome` does, trimmed. Since 2024 much of that moved into
`//extensions` (for desktop Android), which keeps the embedder small:

- **Renderer:** `extensions::ExtensionsRendererClient` already owns the
  Dispatcher wiring. Subclass it (two methods) and forward
  `ShellContentRendererClient`'s frame, script-injection and service-worker
  hooks to it.
- **Common:** an `ExtensionsClient` with `CoreExtensionsAPIProvider` plus our
  provider for the five `//chrome` APIs (schemas, features, permissions).
  `chrome-extension` registered as a standard, secure, CSP-bypassing,
  service-worker scheme in `ShellContentClient`.
- **Browser:**
  - `ExtensionsBrowserClient` (single profile, no incognito, no kiosk).
  - `ExtensionSystem` + factory: keyed services, `ExtensionRegistrar`,
    `UnpackedInstaller` for `--load-extension`; `extension_service()` is null.
  - `ShellBrowserContext` gains a JSON `PrefService` (`UserPrefs::Set`),
    registered with the keyed-service dependency manager.
  - `ShellContentBrowserClient` hooks: `chrome-extension://` URL loader
    factories (navigation, subresource, worker, service-worker update), the
    WebRequest proxy in `WillCreateURLLoaderFactory` (DNR is enforced there),
    navigation throttles, process map / site isolation for extension
    processes, renderer interface binders.
  - Extension resources repacked into `content_shell.pak`.
- **Toolbar:** one button per extension with an `action`: icon, badge text and
  colour; a click opens the popup page in a small native window. New calls
  across `haiku_toolbar_bridge.h` and the shim.

Sources live under `port/files/content/shell/{browser,common,renderer}/extensions/`,
built into `content_shell_lib` when `is_haiku`, with the patches to
existing content_shell files in `port/port-shell-extensions.py`.

## Plan

1. **Embedder skeleton.** Extensions load from `--load-extension=<dir>`;
   the background service worker starts; content scripts inject; DNR rules
   block requests. Check with Brave Shields' `dist/` (build: `npm run build`).
2. **Missing APIs:** `tabs`, `webNavigation`, `cookies`, `extension`, and
   `action`'s badge calls.
3. **Toolbar:** action buttons, badge, popup window.
4. Later: persistent install list (instead of the flag), an extensions page,
   permission prompts, `contextMenus`, `commands`, Web Store `.crx` installs.

## Status

This work was first written and tested on Linux, and ported here on
2026-10-25. There it ran as follows on x64, multi-process:

- Brave Shields 1.0.9 loaded, its MV3 service worker ran, its content scripts
  were injected, and its declarativeNetRequest rulesets blocked requests.
- The `tabs`, `webNavigation`, `cookies` (including `onChanged`), `action` and
  `extension` APIs worked.
- The shield appeared in the native toolbar with its per-tab badge, and its
  popup opened under the button.

**On Haiku** (Renku nightly build00062 x86_64 with the syscall filter,
2026-10-26), sandboxed and multi-process: Brave Shields 1.0.9 loads, its
service worker and content scripts run, its toolbar button shows the
per-tab count, and its popup works. On adblock-tester.com it blocks 12
requests and scores about 70 of 100; the checks it fails are URLs its
rulesets (capped at 5,000 rules each) do not cover. Turning Shields off
for a site lets every request through; turning it back on blocks them
again. Getting there took:

- DNR's `WebContentsHelper` on every WebContents (Chrome's `TabHelper`
  owns one), without which `allowAllRequests` rules never apply;
- navigations held until the start-up extensions and their rules have
  loaded (`StartupNavigationThrottle`): the start page otherwise loaded
  unfiltered and, with a rule that modifies request headers, hung;
- two declarativeNetRequest fixes in Chromium (`port-shell-extensions.py`):
  allow-listed frames survive ruleset updates, and are recorded at
  `ReadyToCommitNavigation` as well as `DidFinishNavigation`.

Two differences from that build matter:

- **Processes.** The launchers no longer pass `--single-process` (see
  AGENTS.md, "Multi-process again"), so extensions get their own renderer
  processes, as they did there. Run with `--single-process`, content puts
  every frame into its one in-process renderer: `IsSuitableHost()` always
  says yes, and the process limit is always reached. Extension pages and the
  service worker then share the renderer with web pages.
  `SiteInstanceGotProcessAndSite()` still records that process as the
  extension's in the `ProcessMap`, which is what lets `CanCommitURL()` accept
  `chrome-extension://` URLs.
- **The extension and loader paths.** Bundled extensions are loaded from
  `extensions/` next to the binary (`/boot/system/apps/RChromium/extensions`),
  each through a writable copy in the profile's `Bundled Extensions`. User
  extensions go in the profile's `Extensions` folder. The profile is
  `~/config/settings/content_shell` unless `--user-data-dir` says otherwise.

What that build left out is still left out: `contextMenus` (there is no page
context menu), `commands`, Web Store installs and permission prompts.

Brave Shields (gasanache/brave-shields-extension) has no licence. It is not
a Brave project; only its adblock-rust and adblock-resources parts are
MPL-2.0. So it is not bundled in anything published.

Embedder pieces that turned out to be required, beyond the list above:
`ExtensionsAPIClient` with a webstorePrivate delegate (its factory
dereferences it) and a messaging delegate; a `ProtocolHandlerRegistry`;
`ExtensionPrefs`, `PermissionsManager`, `RuntimeAPI` and `AudioAPI` prefs
registered by hand; state stores created with the extension system rather
than at `InitForRegularProfile()`; and the registrar delegate's
`OnExtensionInstalled` calling `AddNewOrUpdatedExtension()`. The `scripting`
permission is registered here: only its API is core.

Build-graph notes: content_shell_lib is a dependency of Chromium's component
test targets, which assert that nothing they depend on links `//extensions`
or `//chrome`. `port/port-shell-extensions.py` therefore empties
`disallowed_extension_deps_` in `components/BUILD.gn` (those tests are never
built here) and drops `//extensions/renderer`'s unused `//chrome:resources`
dependency.
