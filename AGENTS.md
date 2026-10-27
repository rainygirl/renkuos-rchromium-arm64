# R Chromium arm64 -- development notes

The AArch64 port. Read this before changing anything under `arm64/`.

## Non-negotiable goal

A Chromium browser that runs on Haiku/AArch64 at native Apple Silicon speed and
is installed like a real application on the RENKU arm64 image. As with x86, GN
success or a bare window is not completion: a real page rendered in a Haiku
window, installed on the Desktop with the blue Chromium icon, and no Qt.

## Where it actually stands (verified 2026-09-13)

**The native BeAPI toolbar works end to end on the RENKU arm64 VM.**
`content_shell` (Chromium 154) renders google.com in a Haiku window under a
hand-built toolbar drawn with BeAPI controls: icon-only Back/Forward/Reload,
add-bookmark and show-bookmarks buttons, and an address field. Verified by
screenshot and by driving it: typing a URL navigates (Shell::LoadURL), the
address field tracks the current URL, Back/Forward enable-state toggles both
ways, and re-navigation (including Back to a previously loaded page) repaints
cleanly -- the x86 localStorage re-navigation stall does not reproduce on 154
for these pages. Single window, no in-content views toolbar.

The toolbar was then converged on the x86 port's BControlLook style (user
decision (b), 2026-09-13): the stock BButton toolbar was replaced by the ported
ChromeButton/BrowserChromeView/BookmarkStore/BOutlineListView design, so the
look, the bookmark behaviour and the on-disk format now match
`../rchromium-native-x86/`. Verified on screen: the BControlLook toolbar
renders, and the star button files about:blank under a "Today" group in the
searchable bookmarks window.

**Known separate issue -- heavy pages fault in V8 on an aged session.** With
this build, about:blank renders under the toolbar, but loading google.com aborts
with a V8 CHECK (`std::numeric_limits<int>::max() >= length_`) / BUS_ADRALN.
Free RAM is ample (6 GB), so this is not exhaustion. The MAP_NORESERVE fix that
the x86 port carries as patch 0085 is ALREADY present in this 154 tree
(page_allocator_internals_posix.h, `#if defined(__HAIKU__)` -> add MAP_NORESERVE
to PROT_NONE reservations), so there is nothing to port. What remains is the
residual, intermittent form the x86 notes describe: even with MAP_NORESERVE,
reliable rendering of heavy pages needs a freshly booted machine -- accumulated
commit charge and address-space fragmentation from many content_shell launches
(this session ran a dozen-plus) eventually make a V8 CodeRange page unbacked.
An earlier arm64 run this session did render google.com, consistent with the
intermittence. This is independent of the toolbar; re-test heavy pages on a
fresh VM boot rather than treating it as a toolbar or missing-patch bug.

Fresh-boot re-verification (2026-09-13): after a VM reboot, google.com renders
under the toolbar with no crash (the aged-session V8 fault above did not recur,
confirming its diagnosis). news.naver.com is a separate matter -- it loads far
enough to run the page's JavaScript, then crashes. NOT a toolbar issue.

CORRECTED DIAGNOSIS (2026-09-13, symbolized). The earlier "fd 0 message-pump"
theory was WRONG. The `MessagePumpEpoll: unhandled poll revents 0x1000 on fd 0`
line is a separate/downstream log, not the fault. The actual SEGV (SEGV_MAPERR
address 0x10) was symbolized against the exact crashed binary
(out/haiku-arm64/content_shell, 2026-09-13 08:46) with llvm-symbolizer. The
reliable crash chain, top (faulting pc) first:

    v8::internal::AstRawString::Equal            <- pc, SEGV @ 0x10 (garbage key)
    v8::base::TemplateHashMapImpl<AstRawString>::Resize()
    v8::internal::AstValueFactory::GetString
    v8::internal::Parser::ParseVariableDeclarations / ...parser...
    v8::internal::Parser::DoParseProgram
    PushAllRegistersAndIterateStack              (stack marker)
    v8::internal::Parser::ParseOnBackground
    v8::internal::BackgroundCompileTask::Run()

So it is a V8 BACKGROUND JS-PARSE crash: the AstValueFactory string-table hashmap
(TemplateHashMapEntry: {AstRawString* key; uint32_t hash_and_exists_}, exists =
bit 31) holds an entry whose exists bit is set but whose key is null/garbage;
`Resize()` calls the match function (`AstRawString::Equal`) on it and derefs
key+0x10. No MessagePumpEpoll / fd-0 frame appears anywhere in the stack.
Consistent with x86 (87) never showing this -- 87 uses a different
compile/parse-thread path.

A prior session ALREADY hit this failure class and left a guard in
v8/src/base/hashmap.h `Resize()` (bounds the rehash loop by the old array end,
not just by `occupancy()`, and fprintf's "v8 hashmap Resize: occupancy
overcounts live entries by %u"). That guard IS compiled into the 08:46 binary,
yet the crash recurred -- so it is an IN-BOUNDS corrupted entry (exists bit set
on a slot with a garbage key), not the walk-off-the-end variant the guard fixes.
Backing memory IS zeroed (Initialize -> Clear -> memset) and the map is
single-thread per parse task, so the corruption is either external heap
corruption writing into the backing array, or a torn write to
`hash_and_exists_`.

Ruled OUT statically this session:
- PartitionAlloc/V8 page-size mismatch: Haiku arm64 is 4 KB
  (arch/arm64/arch_vm.h PAGE_SHIFT 12; posix/arch/arm64/limits.h PAGESIZE 4096),
  which MATCHES PartitionAlloc's hardcoded 4 KB. Note: PartitionAlloc's runtime
  page-size path (NONCONST_PAGE_SIZE) is only enabled for Android/Linux arm64,
  NOT Haiku -- harmless here only because Haiku arm64 is 4 KB.
- Thread stack bounds: V8 gets them via GNU `pthread_getattr_np` (no
  platform-haiku override) -> Haiku `__pthread_attr_get_np` fills stack_base/
  stack_end from `_kern_get_thread_info`; V8 computes stack_start = base+size =
  stack_end (top of a down-growing stack), which is correct. A 0x10 fault does
  not match a stack-bounds symptom anyway.

DECISIVE NEXT TEST (needs a live arm64 content_shell VM; see below): relaunch
news.naver.com with `--js-flags=--single-threaded` (disables V8 background
threads). Survives -> background-thread-specific (race / torn write on the
string table). Still crashes on the main thread -> external heap corruption.
Also watch stderr for the "occupancy overcounts" diagnostic; if it fires, the
overcount path is active and the printed delta says how far off. Follow-ups if
overcount is confirmed: instrument FillEmptyEntry/Remove, or build with
`v8_enable_verify_heap`.

VM STATUS (2026-09-13): the live test is currently gated. The one arm64 QEMU on
the M4 (10.0.0.116) is running someone else's install (/tmp/n.iso, hostfwd
2341->22, QMP /tmp/nsd.qmp, actively writing -- likely the "rworldradio Haiku
ARM64 install" session); do not kill it. Our former content_shell guest (was on
hostfwd 2224) is down, and there is no pre-built image with content_shell
deployed -- the renku-verify ISOs (~479 MB) are base Haiku for boot checks only.
content_shell arm64 binary is ready at
/haiku-build/chromium/src/out/haiku-arm64/content_shell (323 MB) in the
container; running the test means booting a base Haiku arm64 anyboot
(/root/gen-arm64/haiku-minimum-anyboot.iso or /haiku-build/renku-anyboot.iso) in
a second QEMU and deploying content_shell into it.

Address-bar scheme (2026-09-13): arm64 is correct -- the shim passes raw
address text to the delegate and shell_platform_delegate_haiku.cc prepends
`https://` (not http://) for bare hosts. The http:// issue the peer fixed is
x86-only.

Launch flags that work on the VM (software render, single process):
`--ozone-platform=haiku --no-sandbox --single-process --disable-gpu
--in-process-gpu --disable-gpu-compositing --user-data-dir=<writable>`; set
`TMPDIR` and `LIBRARY_PATH` (including the dir holding libchromium_haiku.so) to
a writable volume, since /boot is small.

Below is the earlier bring-up state, kept for context; the port README in
`port/README.md` stops at "`//base` builds" (early September) and is well behind.

- **The full `//chrome` target builds.** Chromium **154.0.8036.0**, `target_os=
  haiku target_cpu=arm64`, produced `out/haiku-arm64/chrome` (~497 MB, 2026-09-12
  11:54) in the `haiku-builder` container on the Mac mini M4 (`10.0.0.116`).
  This is the whole browser, not `content_shell`.
- **It runs and renders on the RENKU arm64 VM.** Launched from
  `/chromium/apps/Chromium/Chromium` with `--no-sandbox
  --remote-debugging-port=9222`; google.com and news.naver.com render (Korean
  layout, fonts, live cards). Screenshots exist under `~/Workspace/haiku/renku-*.png`.
- **`readelf -d` on the binary lists libbe/libroot/libnetwork/libchromium_haiku,
  no Qt.** The Qt boundary holds here for free -- this port never had Qt.
- **An hpkg was staged** (`chromium-154.0.8036.0-1-arm64.hpkg`, ~158 MB,
  2026-09-11) with the `packaging/Chromium.PackageInfo` metadata.

### What this is NOT yet

- **Not installed.** On the VM, `/chromium` is a manual scratch mount from a raw
  disk image; the browser is launched by hand over SSH. It is not in
  `/boot/system/apps`, not in the Deskbar Applications menu, and the hpkg is not
  in any repository. `.PackageInfo` on the mount is a zero-filled placeholder.
- **Not branded.** The window title bar and Deskbar entry say "Chromium", the
  icon is the stock one, and the signature is `application/x-vnd.Chromium-Ozone`.
  The blue R Chromium icon (`../assets/rchromium.hvif`), the name and an app
  signature have not been applied.
- **Not the R Chromium UI (and this is now the main job).** Today it is Chrome's
  own toolbar, omnibox and bookmarks. The decision (2026-09-13) is to converge
  both platforms on one product: `content_shell` wrapped in a hand-written BeAPI
  toolbar, implemented here on arm64 first and then ported to x86. That means
  dropping the `//chrome` target for `//content/shell:content_shell` and building
  the native UI. See `docs/browser-ui-plan.md`.

## How this port is put together

Four layers, each with its own build:

1. **Cross toolchain** (`toolchain-build/`). There is no prebuilt Haiku arm64
   toolchain, so the container cross-builds the sysroot, Python 3.14 and its
   libraries, gn/ninja, and clang/lld 19.1.7, packaging each as an arm64 hpkg
   the image build can consume.
2. **Rust** (`rust/`). Chromium's Rust cannot be disabled (`//skia:bridge_rust_side`
   is a hard dependency) and stock rustc has no `aarch64-unknown-haiku` target.
   `rust/` adds the 26-line target spec and swaps in a patched rustc;
   `//third_party/rust-toolchain` must point at it.
3. **The Haiku port** (`port/`). `apply-haiku-port.sh` turns `__HAIKU__` into
   `OS_HAIKU`/`IS_HAIKU`, adds a target OS and toolchain, and applies source
   fixes; `port-ozone.py` registers the Ozone platform and copies the BeAPI
   backend into `ui/ozone/platform/haiku`. `port/README.md` has the detail on
   what `//base` needed (epoll->poll, int_fast8_t, the POSIX-not-Linux long tail).
4. **The BeAPI shim** (`haiku_shim/`). `BWindow`/`BView` subclasses cannot live
   in Chromium's `-fno-rtti` clang objects -- libbe runs `dynamic_cast` on every
   window message and a null `type_info` faults. They are compiled with the gcc
   that built libbe into `libchromium_haiku`, which the ozone `source_set` links
   via `libs = [ "be", "chromium_haiku" ]`.

Plus the **`A0001` runtime_loader TLSDESC** kernel patch in
`../haiku_kernel_patches/`: clang emits `R_AARCH64_TLSDESC` and Haiku's loader
did not handle it, so nothing with a `thread_local` loaded until this landed.

## Reconciliation done in this restructure (2026-09-13)

The port generator had drifted from what actually built. `port/files/ozone/haiku/`
was the early-September staging (no `haiku_beapi`, no `haiku_shim`), while the
M4 container's `ui/ozone/platform/haiku/` carried the debugged 2026-09-12
sources. The applied sources were pulled back into `port/files/` so the
generator reproduces the working tree:

- `port/files/ozone/haiku/` updated to the Sep-12 sources; `haiku_beapi.{cc,h}`
  and `haiku_shim.h` added.
- `port/files/media/audio/haiku/audio_manager_haiku.cc` added (was never staged).
- `port/files/build/config/haiku/` and `.../toolchain/haiku/` updated to the
  Sep-12 versions.

Still not captured mechanically: any edits made directly in the M4 `src/` tree
outside these directories (the port scripts only regenerate what they name).
`port/haiku-port.diff` remains an early snapshot, not the source of truth -- the
scripts are. A clean-checkout reproduction has not been re-run since this
reconciliation; do that before trusting it.

## Remaining work

The headline job is the BeAPI native UI (decided 2026-09-13). It, and the
install/branding that follow, need the M4 container and a running RENKU arm64
image, which are driven from other sessions.

1. **Native BeAPI toolbar on content_shell -- DONE and running on the VM
   (2026-09-13).** Compile-verified and then verified live:
   - the toolbar itself in the shim (`haiku_shim.cc`: Back/Forward/Reload
     BButtons + address BTextControl + content-view inset) -- compiles and links
     into libchromium_haiku.so;
   - the ozone wiring (`haiku_toolbar_bridge.{h,cc}`, `haiku_beapi`,
     `haiku_window`) that carries toolbar events to the UI thread and pushes
     state back;
   - `content/shell/browser/shell_platform_delegate_haiku.cc`, selected by
     `port-content-shell-ui.py`, turning toolbar events into GoBackOrForward/
     Reload/Stop/LoadURL and pushing address/loading/enabled state down.
   Built as a full `content_shell` (root switched from //chrome:chrome; two
   POSIX-not-Linux fixes captured in port-content-shell-build.py) and run on the
   VM. Buttons currently use Unicode glyphs; true HVIF icon art is still a
   refinement. The bookmarks store/window are wired and compile, but were not
   exercised on screen yet. Remaining under this item: HVIF icons; an on-screen
   bookmarks test; then the x86 (Chromium 87) backport. Full plan and the
   CH154-vs-CH87 deltas: `docs/browser-ui-plan.md`.
2. **Brand it.** Apply `../assets/rchromium.hvif`, set the app name to R Chromium
   and a real signature, so it is not stock "Chromium".
3. **Install into the image.** Rebuild the hpkg **zlib-compressed, not zstd**
   (zstd hpkg files install silently empty on this kernel -- check bytes 18-19,
   `0001` zlib / `0002` zstd), verify `readelf -d NEEDED` against a known-good
   reference (a package's `requires:` does not prove the binary's `DT_NEEDED`),
   then deploy through `~/Workspace/renku-arm64/make-renku-repo.sh` to
   `renku-repo.coroke.net/arm64`, or bake it into the image definition.
4. **Make it appear in the Deskbar.** Promote the binary's icon and signature
   from resources to attributes (`resattr`), then `mimeset`; Deskbar reads
   attributes, not resources.
5. **Sandbox.** Currently `--no-sandbox` (no seccomp/namespaces/zygote on
   Haiku). Structural: a real design or an accepted limitation, documented.
6. **Re-run a clean-checkout build** after the reconciliation above and record
   the exact `gn gen`/`ninja` invocation and revision that reproduces the binary.

## The x86 sibling already has its own native toolbar

`../rchromium-native-x86/` is NOT a backport target: it independently grew an
equivalent native toolbar (Chromium 87), implemented with BControlLook-drawn
buttons (`chromium87_overlay/ozone/haiku_beapi_views.cc`, `haiku_browser_chrome.h`)
plus the aura ShellPlatformDelegate. Both platforms meet the same user spec
(icon toolbar + address field + date-grouped searchable bookmarks + blue-icon
Desktop install) and share the same design concepts -- a widget-keyed bridge so
content_shell never sees a BeAPI type, an explicit platform_->aura->ShowWindow()
in the delegate, and the SetAddressBarURL/SetIsLoading/EnableUIControl mapping.

They differ only in how the toolbar is drawn (this port: stock BButton via the
shim; x86: custom BControlLook drawing) and cannot share source across the 87 vs
154 API gap. Whether to converge the two on one style is an open user decision;
until it is made, neither is a port of the other. Do not rewrite one to match
the other without that decision.

## Relationship to renku-arm64, and deployment

This repository is the **canonical source** for the arm64 Chromium port. The
RENKU image builder consumes a Chromium port too, and today that is a **separate
copy** at `~/Workspace/renku-arm64/chromium/` (its `docker-build-renku-arm64.sh`
copies that directory into the build container). The two can drift.

The agreed plan (2026-09-13) resolves the drift in this order:

1. Implement the native BeAPI UI here first (see `docs/browser-ui-plan.md`).
2. Publish this repository to GitHub once the native UI works.
3. Point renku-arm64 at the published GitHub repo -- clone or submodule in the
   image build -- instead of its local `chromium/` copy.

Until step 2, do not rewire renku-arm64. Treat this repository as the source of
truth: any fix made in the renku-arm64 copy should be mirrored back here, not the
other way round. `~/Workspace/renku-arm64/chromium/SOURCE.md` records the same
intent on that side.

## Traps worth keeping

- **`is_haiku` is defined out of tree on purpose.** `BUILDCONFIG.gn` asks not to
  add `is_*` for lesser Unixes, but 150 files needed the term; one variable
  rebases better than the comparison spelled 150 times.
- **The gn root is `//chrome:chrome`,** not `//:gn_all`, to avoid dragging in
  every test target's platform assumptions.
- **hpkg `vendor` must be exactly `Haiku Project`** or `package_repo` refuses
  the index; `users` is a single line, not a brace block; adding a package
  changes the repo checksum so jam needs `HAIKU_NO_DOWNLOADS=1`.
- **Cross-built binaries can carry an absolute sysroot path in `DT_NEEDED`;**
  `patchelf` reduces those to a basename. Check `readelf -d` right after any
  large link.
- **`git`/other tooling is not present in the container tree** the way it is on
  the Haiku box; the M4 `src/` is not a git checkout, so there is no diff to
  read -- the port scripts are the only record of what was changed.

## AI disclosure

This port and these notes were produced largely with Claude Code.

## Summary

A native Haiku/AArch64 build of Chromium's `content_shell` that runs at native
speed on Apple Silicon (and arm64 hardware) rather than emulating x86, with a
hand-written BeAPI toolbar (address bar, back/forward/reload, bookmarks) drawn by
the ozone layer. news.naver.com and other pages render fully.

End users install it with the one-line `install.py` command in `README.md`
(English; `README.ja.md` and `README.ko.md` are translations). The README is
deliberately usage-only; everything else -- requirements, the release tarball,
the launcher flags, manual deployment, troubleshooting -- is in the next
section, and build steps and porting internals follow.

## Window title bar and resizing (fixed 2026-09-15)

The x86 port's two visible window bugs were reproduced on arm64 and fixed
here; the causes are the same on both.

**No title bar / title bar gone after a link opens a new window.** The window
was never undecorated: content_shell's `ShellPlatformDataAura` creates the one
shared host window with `properties.bounds = gfx::Rect(initial_size)`, i.e. at
(0,0), and Haiku draws the tab *above* the frame -- off the top of the screen.
Every new Shell (a link with `target=_blank`, `window.open`) then called
`ResizeWindow(size)` = `SetBoundsInPixels(gfx::Rect(size))`, which yanked the
window back to (0,0) if the user had moved it. Fixes:
- `haiku_shim.cc` `ShimWindow::KeepDecorOnScreen()` (constructor and
  `SetWindowBounds`): a decorated window is moved so its tab and border are on
  screen, using `GetDecoratorSettings()` ("tab frame", "border width") with
  25/5 px fallbacks; `FrameMoved` reports the result back.
- `shell_platform_delegate_haiku.cc` `CreatePlatformWindow`: keeps the host's
  current origin and applies only the size instead of `ResizeWindow()`.
- `haiku_window.cc` `SetToolbarTitle`: also sets the BWindow title (aura
  content_shell never calls `PlatformWindow::SetTitle`), so the tab shows the
  page title instead of "Chromium".
Note all Shells share one BWindow: a "new window" is a second WebContents
stacked in the same window, which is the upstream aura content_shell model.

**Resizing the window did not resize the page** (renderer stayed 800x600; the
toolbar re-laid out, the content did not). Cause: upstream `FillLayout` in
`shell_platform_data_aura.cc` fits children to the host *once*
(`has_bounds_`), because nobody resizes the upstream shell by hand. Fix: a
`HostResizeObserver` (`aura::WindowTreeHostObserver::OnHostResized`) in the
Haiku delegate refits every child of the root window to the host size. The
ozone side also got `HaikuWindow::OnSizeChangedFromWindowThread`: the shim's
`FrameResized` only knows the new size, and the old path posted a rect at
(0,0), silently corrupting the tracked origin on every resize.

**Frame accounting.** The shim now treats the rect Chromium passes as the
*content* rect: the BWindow is `kChromeHeight` (30 px) taller so the content
view is exactly that size, and `FrameMoved`/`GetWindowFrame` report the
content rect (origin below the toolbar). Before, Chromium believed the content
was 800x600 while the view was 800x570, so the bottom 30 px of every page were
cut off.

How it was verified: `--remote-debugging-port=9222` + an ssh tunnel + a
40-line raw-websocket CDP client (`Runtime.evaluate` of
`[innerWidth, innerHeight]`) before and after a QMP corner drag; `window.open`
from CDP for the new-window case; QMP screendumps for the tab.

## Typing into web pages (fixed 2026-09-19)

Symptom: clicking a form field showed the focus ring and caret, but typed
text never arrived (an X login could not be filled in). The address field
took typing normally.

**Fixed in the shim: keyboard focus.** `ShimView` never called `MakeFocus`,
so BWindow sent `B_KEY_DOWN` to the address field or to no view, and
`ShimView::KeyDown` never ran. The view now takes focus in
`AttachedToWindow` and `MouseDown`. Pressing Enter in the address field also
moves focus back to the page, as other browsers do. Before, the next
keystrokes edited the URL, and `SetAddress` kept skipping updates because the
field was focused. Verified on the renku-stock-2g VM over QMP: after a click,
`abc XYZ 123`, Backspace and Tab reach the page.

**Fixed in the ozone layer: punctuation.** `HaikuEventBridge::OnKey`
(`haiku_beapi.cc`) built its `KeyEvent` from `KeyboardCodeFromByte(bytes[0])`,
which knows only B_* keys, a-z and 0-9, so `. , - _ / @` arrived as
`Unidentified`/keyCode 0 and were never inserted. The event now carries a
`DomKey::FromCharacter()` decoded from the UTF-8 bytes (skipped when Ctrl,
Alt or Command is held, so shortcuts still work), with the DomCode and, if
needed, the key code derived from it.

**Fixed in the shell delegate: focus before the first click.** At startup
`document.hasFocus()` was false and keys were dropped even though `ShimView`
had focus; only a mouse press in the page fixed it, via
`RenderWidgetHostViewEventHandler::SetKeyboardFocus()`. Calling
`WebContents::Focus()` from `SetContents` is not enough on its own:
`RenderWidgetHostViewAura::Focus()` does nothing until the view has a focus
client and its window can take focus, and the view is created and shown
asynchronously. `shell_platform_delegate_haiku.cc` now retries the focus on
later turns of the loop (50 ms apart, up to 30 times) and stops as soon as
the view reports focus.

Verified against the real x.com login form on the renku-stock-2g VM:
`test.user_99@example.com` types into the username field with no click first,
the password field accepts text, and Tab moves between them.

**Building only the shim, without the Chromium container.** The local
`haiku-builder` container (colima) has Haiku's own arm64 cross gcc 13.3.0 at
`/root/renku-arm64-work/generated.arm64/cross-tools-arm64/bin`. Its sysroot
directory no longer exists, so point `--sysroot` at a directory whose
`boot/system/develop/headers` links to the `haiku_devel` package contents
(`objects/haiku/arm64/packaging/packages_build/minimum/hpkg_-haiku_devel.hpkg/contents/develop/headers`).
Link with `-L` pointing at real copies of `release/kits/libbe.so`,
`release/system/libroot/revisioned/libroot.so` and gcc_syslibs'
`libstdc++.so*`. The `.so` links inside `haiku_devel` are relative Haiku paths
and dangle on Linux. Built unchanged, the shim matches the release shim's
dynamic symbols and LOAD layout exactly. To test without repackaging, run
`content_shell` with `LIBRARY_PATH=/boot/home/shim:%A/lib:/boot/system/lib`.
The stock VM has no curl, python, awk or bash `/dev/tcp`; WebPositive can
download from a host `http.server` at `http://10.0.2.2:<port>/`.

## Driving the VM, and the tools that are missing inside it

**Where any of this lives.** None of it is in this repository, and the
directory is misnamed, which costs a session its first twenty minutes every
time. On `rainygirl@MacMiniM4`:

- `~/rtwitter-test/` -- despite the name, the arm64 Chromium VM workspace.
  `renku.image` (the Haiku arm64 guest), `xfer.img` (the FAT32 transfer disk
  the guest mounts from `/dev/disk/scsi/0/0/0/raw`), `cs/` (content_shell and
  its resources, staged for transfer), `buildall.sh`, `shot.py`, and the
  `diagNN.py` series -- each of which is one diagnostic edit to the Chromium
  tree, numbered in the order they were tried.
- `shot.py <sock> move|click|key|text|shot|drag` is the QMP driver. The
  running VM's socket is `/tmp/rtw.qmp`. Its shebang points at
  `~/Workspace/github/rreader/rreader-python/venv/bin/python3`, which is where
  PIL is; `SHOTDIR` chooses where screenshots land.
- The build is the `haiku-chromium` Docker container (`docker` is at
  `/usr/local/bin/docker` and is not on a non-interactive ssh `PATH`). The
  checkout is `/work/chromium/src`, the output `out/haiku-arm64`.
- The guest's `hostfwd` on 127.0.0.1:2231 goes to its port 22, but nothing
  listens there unless sshd has been started inside the guest, so the transfer
  disk is still the way in.
- Before trusting a build, check that the `diagNN.py` edits have been backed
  out: `grep -c Haiku v8/src/ast/ast-value-factory.{cc,h}
  v8/src/zone/accounting-allocator.cc` should print zeros.

Most of the time spent on the bugs below went into watching the guest, not
into the bugs. What works:

- The stock guest has **no `grep`, `sed` or `awk`**. Bash pattern matching is
  the substitute:
  `while read l; do case "$l" in *PATTERN*) echo "$l";; esac; done < file`
- Piping a long-running program through `tail` shows nothing until it exits.
  Use `head -N`, or redirect to a file and filter afterwards.
- The Terminal's scrollback cannot be reached with PgUp through QMP; the keys
  go to the shell as history.
- QMP typing drops characters when it outruns input_server. `shot.py` takes
  `KEYDELAY` (0.12 is reliable); long command lines still need checking
  against a screenshot before Enter.
- Copying the 323 MB content_shell inside the guest takes well over a minute.
  Scripted sequences that assume 40 s type their next command into the middle
  of the copy.
- `/dev/ports` is empty, so the serial port is not a way out; the FAT32
  transfer image is, but only after QEMU exits, and the guest's FAT writes do
  not always survive.

## Installing a web app as a Haiku application (2026-09-23)

The toolbar shows an install button on a page with a usable web app manifest,
and pressing it writes a Haiku application:

	~/config/non-packaged/apps/<Name>/<Name>            the launcher, +x
	~/config/settings/deskbar/menu/Applications/<Name>  a symlink to it

The launcher is a shell script that runs this same content_shell at the
manifest's `start_url` with `RCH_NO_TOOLBAR=1`, `RCH_APP_NAME=<Name>` and its
own `--user-data-dir`. Deskbar does not scan `non-packaged/apps`; it lists the
symlinks in that menu directory, so both are needed. Deleting them uninstalls.

Chrome's version of this is in `chrome/browser/web_applications`, which
content_shell does not have and does not need: `ManifestManagerHost` hands
over the parsed manifest and `WebContents::DownloadImage()` fetches its icons.

**Three layers, not two.** This port's BeAPI lives in a separate shim library,
so a toolbar button travels:

	haiku_shim.cc  BrowserChromeView -> Delegate::OnInstall()
	haiku_beapi.cc HaikuEventBridge::OnInstall() -> UI thread
	haiku_window.cc HaikuWindow::OnToolbarInstall() -> HaikuToolbarObserver
	shell_platform_delegate_haiku.cc  ShellToolbarObserver::OnInstall()

Adding the button to the shim and the handler to content_shell is not enough.
`HaikuEventBridge::OnInstall()` is the link, and leaving it out gives a button
that draws, shows the right tooltip and does nothing -- which is exactly what
the first build did.

**Trunk is not 87.** `WebContents::GetManifest()` has been removed;
`ManifestManagerHost::GetOrCreateForPage(contents->GetPrimaryPage())` replaces
it and answers with a `blink::mojom::ManifestPtr`. Its `icons` are typemapped,
so they are values inside a mojo pointer: `icon.src`, not `icon->src`. The
x86 port's version of this file needs both changes to come the other way.

**The installability test** is Chrome's minus the service worker -- a
manifest, a valid `start_url`, a name, a display mode of
standalone/fullscreen/minimal-ui, and an icon with purpose any or maskable.
Chrome wants a fetch handler because an installed app there should work
offline; here it is a Deskbar entry that opens a URL, and requiring one would
rule out most of what anyone would install.

Measured on the VM: squoosh.app judged installable, tooltip reads
"Install Squoosh", the launcher and symlink appear, the 512x512 manifest icon
is downscaled into the launcher's icon attributes, and Deskbar's Applications
menu lists Squoosh with the site's own icon.

## Close box, RCH_NO_TOOLBAR and RCH_APP_NAME (fixed 2026-09-30)

Until this date the arm64 build ignored all three. The installer wrote
`RCH_NO_TOOLBAR`/`RCH_APP_NAME` into launchers, but nothing read them, and
`ShimWindow::QuitRequested()` reached `WindowTreeHost::OnHostCloseRequested()`
with no observer at the end.

- `haiku_window.cc` builds the window without the toolbar when
  `RCH_NO_TOOLBAR` is set, and `RCH_APP_NAME` wins over the page title.
- `shell_platform_delegate_haiku.cc` has a `HostCloseObserver` that closes
  every Shell (they share the one host); Shell quits the loop after the last.
- Closing then crashed in `Compositor::ReleaseAcceleratedWidget()`:
  `~HaikuWindow` called `OnAcceleratedWidgetDestroyed()` on a
  WindowTreeHostPlatform that had already destroyed its compositor. The
  destructor no longer calls the delegate (X11/Wayland do not either).

Verified on the RENKU VM with rchromium rev 5: R Twitter opens without the
toolbar, titled "R Twitter"; the close box ends R Twitter and R Chromium
with exit status 0.

Build trap: run the same `ninja` that last built an output directory. The
depot_tools one found `out/haiku-arm64-nopa` written by another version,
printed "build log version is too old; starting over", and rebuilt all
70,896 edges (about 10 hours at `-j 14`). `locales/en-US.pak` is not a
content_shell output; the release reuses the one from the previous tarball.

## The mouse wheel and two-finger scrolling (fixed 2026-09-30)

Nothing scrolled: not the wheel, not two-finger scrolling from a trackpad
through QEMU's usb-tablet. There was no path for it at all. Haiku delivers the
wheel as a `B_MOUSE_WHEEL_CHANGED` message rather than a BView hook, `ShimView`
had no `MessageReceived()`, and BView's own dropped it; the Delegate had no
callback to carry it either.

- `haiku_shim.h` (both copies): `Delegate::OnMouseWheel()`, appended after the
  toolbar callbacks and non-pure, so no existing vtable slot moves. The shim and
  Chromium must still be rebuilt together.
- `haiku_shim.cc`: `ShimView::MessageReceived()` reads `be:wheel_delta_x/y`
  and, since the message carries no position, the pointer from `GetMouse()`.
- `haiku_beapi.cc`: a `MouseWheelEvent` of `-delta * kWheelDelta` -- Haiku
  counts notches with positive meaning down/right, Chromium the opposite.

Rebuilt in `haiku-chromium` on the M4 (`out/haiku-arm64-nopa`, the shipping
directory; the dry run with `third_party/ninja/ninja -n` showed 7 edges, so the
right ninja), the shim relinked with the flags from "SHIM LOAD FIX". Packaged as
rchromium 154.0.8036.0-7 and installed on the RENKU VM with `pkgman` from a
repository served on the Mac; wheel-down and wheel-up both scroll a Wikipedia
article.

The package was made without a Haiku machine: `package extract` rev 6 with
the Linux-hosted tool from a Haiku build tree (`vaio-p-builder`), swap
`content_shell` and the shim, bump `.PackageInfo`, `package create -z zlib -2`.
BFS attributes survive the round trip (the launcher keeps `BEOS:ICON`).

The RENKU arm64 image has no curl, wget, tar or python, and its bash has no
`/dev/tcp`; `pkgman` is the one thing in it that fetches over HTTP, which is
why the VM got the build as a package from a local repository
(`pkgman add-repo http://10.0.2.2:<port>/<dir>`).

## x.com's sign-in killed the browser, and it was WebHID (2026-09-30)

Entering a handle on x.com and pressing Continue ended the process every time,
in under a second. Not a clean exit -- a SEGV, which is why the window simply
vanished.

    Received signal 11 SEGV_MAPERR 0000000000000040
    [haiku] elr=...ccc lr=...808
    image 0x...+0xad48000 /boot/system/apps/RChromium/content_shell

Deterministic: two runs, different image bases, the same offsets --
`content_shell+0x5296ccc`, called from `+0x5bed808`. Disassembling those:

    device::DeviceService::BindHidManager(...)
      bl   device::HidManagerImpl::HidManagerImpl()
      ldr  x0, [x21, #0x140]                     <- lr points here

    device::HidService::Create()
      mov  x0, xzr ; ret                         <- nullptr on Haiku

    device::HidManagerImpl::HidManagerImpl()
      bl   HidService::Create
      str  x0, [x19, #0x10]                      <- hid_service_ = nullptr
      ldr  x0, [x19, #0x10]
      b    device::HidService::AddObserver       <- tail call, x0 = nullptr

    device::HidService::AddObserver(...)
      add  x0, x0, #0x40                         <- observer_list_
      b    base::ObserverList<>::AddObserver
           ldp x10, x8, [x0]                     <- faults at 0x40

`HidService::Create()` returns nullptr for a platform it does not know, and
`HidManagerImpl`'s constructor follows a `DCHECK(hid_service_)` -- compiled out
of a release build -- with `Observe()` on that null. x.com asks for the HID
device list because a passkey is a HID device.

`port/port-hid-haiku.py` and `port/files/services/device/hid/hid_service_haiku.{h,cc}`
are the fix: the same empty stub Fuchsia uses, plus a Haiku arm in `Create()`.
The constructor has to call `FirstEnumerationComplete()` or `GetDevices()`
never answers and a page waiting for the list waits forever.

**`navigator.hid.getDevices()` does not reproduce it.** With no granted
permissions it answers with an empty list without binding the service, and the
browser lives -- which is exactly the false negative that sent this
investigation off course for an hour. Only x.com's Continue binds it.

Rebuilt in `out/haiku-arm64-nopa`: three edges, not a fresh tree. Verified on
the RENKU VM by driving the page over CDP (`--remote-debugging-address=0.0.0.0`
plus a QMP `hostfwd_add tcp::9222-:9222`, since the guest has no sshd): the old
binary died within 1 s of the click, the new one is still running a minute
later with a password field in the DOM.

## The profile was in memory, and the switch had been renamed (2026-09-30)

Two separate things kept R Twitter from having a profile of its own.

**`--data-path` is not the switch any more.** content_shell renamed it:

    87, 114   --data-path       content/shell/browser/shell_browser_context.cc
    154       --user-data-dir   kContentShellUserDataDir, shell_switches.h:21

Chrome's `--user-data-dir` is a different switch, which is why R Twitter's
launcher used to carry a comment warning against it -- correct for 114, and
wrong here. On the guest the effect was plain: `~/config/settings/RTwitter`
was never created and `~/config/settings/content_shell`, R Chromium's own
profile, was modified time-first in `ls -t`. The launcher now passes both
names; each build reads the one it knows.

**And the network context is still in memory.**
`ConfigureNetworkContextParamsForShell()` sets no `file_paths` and no
`http_cache_directory`, so there is no cookie file and no disk cache -- the
same gap the x86 114 port closed. `port/port-shell-profile.py` closes it here.
One difference from 114: `http_cache_directory` moved out of
`NetworkContextParams` and into `NetworkContextFilePaths`, which the compiler
pointed out.

**And one more, which only a persistent store can reach.**
`enable_encrypted_cookies` defaults to true, and `network_context.cc:3425`
walks into a `NOTREACHED()` when a persistent store has it set and no
`cookie_encryption_provider` -- content_shell has no OSCrypt provider to give
it. Revision 9 of the arm64 package shipped without that line and died at
startup, before a window; revision 10 sets it false. On Haiku, which has no
keyring, the cookie file is plain sqlite either way, so nothing is lost but
the crash.

Web storage does land on disk, because this port does not force an
off-the-record context. That is why a page can look signed in after a restart
with the cookie jar gone, and why "the session survived" is not evidence that
cookies persist. It was read as such here once, and was wrong.

## The browser went away a minute into x.com, and it was V8 decommitting a page (2026-09-30)

Reported as "R Twitter quits after I sign in". It is not about signing in and
it is not a crash in the sense Haiku means: `debug_server` logs every team it
has to kill, and there was nothing in the syslog. What there was, in the
browser's own stderr, was this:

	#
	# Fatal error
	# Check failed: success.
	#
	#FailureMessage Object: 0x1d068d68860
	Received signal 30 BUS_ADRALN 000000000000
	[haiku] elr=0000012c124deda4 lr=0000012c124d6c78

and an exit status of 158, which is 128 + 30 and so SIGBUS.

Reading it: `elr` is `v8::base::OS::Abort`, whose whole body is `brk #0`
(Haiku delivers that as SIGBUS/BUS_ADRALN rather than SIGTRAP), and `lr` is
inside `V8_Fatal`. The `#` banner is V8's own fatal handler, not Chromium's,
so the message is a V8 `CHECK`, and `CHECK(success)` narrows to a handful of
sites. The one that matters here is `BoundedPageAllocator::FreePages`:

	success = page_allocator_->DecommitPages(raw_address, size);
	// Since we require zero-initialized pages, we must fail here if we
	// cannot decommit the range.
	CHECK(success);

To pick that site out of the four `bl V8_Fatal` calls in the same object, read
the second argument each one sets up -- `CHECK(x)` is
`V8_Fatal("Check failed: %s.", "x")` -- and dump the string it points at. Only
one says `success`. (The first LOAD segment of this binary has file offset 0
at vaddr 0, so a rodata address is also a file offset and `dd` will read it.)

**So `OS::DecommitPages` returned false**, which it only does after
`CHECK_EQ(ENOMEM, errno)`, so the `mmap` failed with ENOMEM:

	mmap(address, size, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)

Two things make that fail on Haiku where it would not on Linux.

- **No MAP_NORESERVE.** Haiku's `sys/mman.h` documents `MAP_NORESERVE` as
  "don't commit memory", and without it an anonymous mapping is committed
  whatever its protection -- so decommitting a range asks the kernel to find
  memory for a range that is about to hold nothing. Under the "low resource
  memory: note -> warning" the syslog shows around these failures, that is
  exactly the request that gets refused.
- **MAP_FIXED over a live range.** On Haiku that means deleting the area under
  it and creating another, and the new one has to be placed in an address
  space that a long-running renderer has fragmented. That is the same corner
  `VMUserAddressSpace::_InsertAreaSlot()` lives in -- see the x86 port's
  `haiku_kernel_patches/K0003`, which fixed a panic reachable from an ordinary
  `mmap()` once the address space is fragmented.

The fix (`port/port-v8.py`) does both halves: pass `MAP_NORESERVE` on Haiku,
and if the mapping still cannot be made, stop trying to make one. The callers
of `DecommitPages` do not need a new mapping; they need the range to stay
reserved and to read back as zero. So make it writable, zero it, and take the
permissions away again. That leaves the pages resident, which is why it is the
fallback and not the first choice -- and it prints one line to stderr the
first time it happens, so a log says which half was needed.

**Why it looked like a sign-in bug.** Signing in is when the page does the most
work, so it is when the allocator is most likely to free a page it must
zero -- but a logged-out load of `x.com/home` killed it in 14 seconds too. It
is not deterministic: three instrumented runs on the same profile survived
before one did not.

**What the fix was measured against, and what that is worth.** Very little, and
the honest answer is that the rate is too low to A/B in an afternoon:

	rev 10 (before)   logged out, 90 s    5 of 5 alive
	                  signed in, 150 s    6 of 6 alive
	                  4 at once, 180 s    4 of 4 alive
	rev 11 (after)    signed in, 150 s    6 of 6 alive

One death in something like fifteen launches, and the control never crashed,
so this pair of sixes says "no regression" and nothing else. Do not read it as
"fixed". The attempt to force the failure by memory pressure did not work
either: four browsers at once left 2.3 GB free on a 4 GB guest, which is not
pressure. What the fix rests on is the mechanism -- the process can no longer
end on this path because the path no longer ends the process -- and the line
it prints when the fallback runs is how a future log will say whether the
MAP_NORESERVE half was enough. Neither run above printed it.

A caveat on the fallback's own limits: it returns false, and the `CHECK` fires
as before, if `mprotect()` also fails. `mprotect()` over a sub-range needs the
same area split the `mmap` needed, so that is not impossible -- it is only
less likely, because a split is less than a delete and a create.

**And why the sign-in never stuck.** The cookie store writes to
`~/config/settings/RTwitter/Network/Cookies` on a timer, so a process that
dies a minute after the login has never written `auth_token`. Fix the death
and the session persists on its own; there was never a second bug there. A
clean run confirms it -- sign in, quit the browser, start it again, and the
timeline comes straight up.

## No authenticator, and an API that says otherwise (2026-09-30)

Found while driving x.com's sign-in form, and unrelated to everything above.

	PublicKeyCredential.isConditionalMediationAvailable()          true
	PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable()  false
	navigator.credentials.get({publicKey})    never resolves, never rejects
	navigator.credentials.create({publicKey}) never resolves, never rejects

There is nothing here for a credential request to reach: no platform
authenticator, and `HidService` is the stub added earlier in these notes,
which enumerates nothing. But the first answer above is what a site asks
before it offers a passkey, so x.com offers one -- and a handle whose account
has a passkey lands on "Sign in with passkey" with a spinner that never stops
and no way back to the password form. The hang is the second half: the request
is dispatched and nothing ever completes it, so the promise stays pending
rather than rejecting with NotAllowedError the way a timeout would.

The fix (`port/port-content.py`) turns the feature off in
`SetCustomizedRuntimeFeaturesFromCombinedArgs()`:

	WebRuntimeFeatures::EnableFeatureFromString("WebAuth", false);

which removes `window.PublicKeyCredential` -- the thing sites feature-detect --
so they offer passwords instead. That function runs *before* the
`--enable-blink-features` handling in the same file, so
`--enable-blink-features=WebAuth` puts it back, which is how to reproduce the
hang. Verified on the guest with 154.0.8036.0-12:

	default                            PublicKeyCredential = undefined
	--enable-blink-features=WebAuth    PublicKeyCredential = function

**This is a workaround, not a fix.** The right end state is a WebAuthn
implementation, or at least a request path that rejects promptly instead of
hanging. Undo this when there is something to authenticate with.

R Twitter's launcher passes `--disable-blink-features=WebAuth` as well. That
is deliberate duplication: it has to keep working against an R Chromium older
than this one.

### Reading a crash here without symbols

The release binary is stripped to 1,915 dynamic symbols, so nearest-symbol
lookup is useless -- it reports offsets of 9 MB. What works is
`llvm-objdump -d --start-address=<elr-imagebase>`: the disassembly carries
function symbols even when `llvm-symbolizer` has nothing to say. Beware ICF:
the crash site reads as `ObserverList<RunLoop::NestingObserver>::AddObserver`
and the HidService::Create call as `xsltStrxfrm`, because identical code was
folded. Follow the addresses, not the names.

## Symbolising a Haiku crash

Haiku executables are ET_DYN, so a runtime address means nothing alone. The
handler prints an image map; subtract the image's base and use
`aarch64-unknown-haiku-addr2line -Cfpie content_shell <offset>`. For a signal,
the unwinder cannot cross the signal frame -- use the printed `elr` instead of
the backtrace.

## Backtraces on Haiku (added 2026-09-19)

Every crash used to print `[end of stack trace]` with nothing above it, which
is why the bugs below took so long to find. `stack_trace_posix.cc` looks for
`<execinfo.h>`, which on Haiku is the HaikuPorts libexecinfo package rather
than part of the base system, so `HAVE_BACKTRACE` stayed unset and
`CollectStackTrace()` returned zero frames.

Nothing extra is needed: the build emits `.eh_frame` and libc++abi already
links an unwinder, so `_Unwind_Backtrace` walks the stack.
`port/files/base/debug/unwind_backtrace_haiku.h` does that and also prints the
image map -- a Haiku executable is ET_DYN, so a bare runtime address cannot be
handed to `addr2line` without knowing where its image was mapped. Subtract the
image base from the printed address and run
`aarch64-unknown-haiku-addr2line -Cfpie content_shell <offset>`.

**The unwinder cannot walk through a Haiku signal frame.** For a SIGSEGV the
trace stops at the handler and says nothing useful. What does say something is
`elr` (the faulting PC) and `lr` from `struct vregs`
(`headers/posix/arch/arm64/signal.h`), which the handler now prints as
`[haiku] elr=... lr=...`. That is what located the null dereference in the
media capture factory; the backtrace alone never would have.

`<ucontext.h>` does not exist on Haiku -- `<signal.h>` declares `ucontext_t`
and the arm64 `mcontext_t`.

## Thread stacks: 256 kB is not enough (fixed 2026-09-19)

Haiku gives the main thread 64 MB (`USER_MAIN_THREAD_STACK_SIZE`) and every
other thread 256 kB (`USER_STACK_SIZE`, `64 * B_PAGE_SIZE`). Chromium and V8
are written against the glibc default of 8 MB; V8 in particular sets its own
stack limit from `--stack-size`, which defaults to about 1 MB, so the
interpreter recurses past the end of a 256 kB stack long before its own guard
fires.

Chromium already fills this hook in for the platforms whose default is too
small -- `platform_thread_apple.mm` returns 8 MB because macOS gives 512 kB,
and iOS asks for 1 MB. This port left `GetDefaultThreadStackSize()` returning
0, which means "keep the platform default". It now returns 8 MB.

The x86 port had the identical hole; see its
`0090-give-non-main-threads-a-usable-stack-on-haiku.patch`, where the symptom
was unmistakable -- a Haiku crash report full of
`Builtins_InterpreterEntryTrampoline` frames ending in "Frame memory:
Unavailable (Bad address)".

## Thread names were never set (fixed 2026-09-19)

`PlatformThreadBase::SetName` skipped `rename_thread` for the main thread, as
Linux does, but tested `thread_info::team == getpid()`. That is the team id,
which is the same for every thread in the team, so *no* thread was ever
renamed and `top`, `ps` and the crash reporter showed them all alike. Haiku
gives a team the id of its main thread, so comparing the thread id against
`getpid()` is the test that was meant.

## The wake-up socket was never drained (fixed 2026-09-20)

Haiku has no `eventfd`, so the shim uses a UDP socket connected to itself
(`port/files/base/message_loop/epoll_shim_haiku.h`). A real eventfd is a
counter: `ScheduleWork()` bumps it, and the single `read()` in
`MessagePumpEpoll::HandleWakeUp()` empties it, after which the descriptor is
no longer readable. The socket is a queue instead -- every `ScheduleWork()`
leaves its own 8-byte datagram -- so one read removed one datagram and any
backlog kept `poll()` returning immediately. That is what had a renderer
thread burning a full core with nothing rendering.

The shim's comment said spurious wake-ups were fine, and they are; what is
not fine is waking until the queue happens to empty. `HandleWakeUp()` now
drains to EAGAIN on Haiku.

Fixing this did not change the V8 corruption below.

## x.com and the V8 AstValueFactory corruption (cause found 2026-09-23)

**It is PartitionAlloc.** Built with `use_partition_alloc_as_malloc = false`
(plus the two flags that have to move with it), x.com ran 10 of 10 without a
crash against the shipping binary's 7 of 10, interleaved in one boot. The
section "The allocator under V8 is PartitionAlloc" below has the measurement
and how it was nearly missed. Everything between here and there is the three
weeks of looking in the wrong place, kept because the eliminations are still
sound and the methodology notes cost more to learn than to write down.

**It is not x.com's bug.** Five sites, three runs each, then ten runs of the
one that mattered:

	https://x.com/i/flow/login   XX.
	https://x.com/               ..X
	https://ko.wikipedia.org/    .X.      and 2 of 10 on a rerun
	https://news.naver.com/      ...
	https://www.google.com/      ...

A plain Wikipedia article takes the same `Check failed` about one run in five.
x.com is the reliable reproducer, not the cause, and the section title and
everything below it are narrower than the problem. Whatever this is, it wants
a page with real JavaScript and a few seconds of work, not anything x.com does
in particular.

Loading `https://x.com/i/flow/login` crashes the renderer, usually as
`Check failed: std::numeric_limits<int>::max() >= length_.` in
`v8::base::Vector::length()`, sometimes as a SIGSEGV. Both land in
`AstRawString::Equal`, reached from `AstValueFactory::GetString` through the
string table's `Resize`/`Probe`: a key in that map does not point at a live
`AstRawString`.

What the memory looks like at the failure: the object's 16 bytes hold other
strings' bytes (`.countries-zh`, `ondemand`), or a pointer plus a 32-bit hash,
or `length_ == (size_t)-1`. It differs every run. The surrounding zone memory
is live and coherent, and an `AstRawString` can only be allocated from
`AstStringConstants`' own zone or from `ast_raw_string_zone` -- so what is
being read is not a freed string but memory that something else is now using.

Ruled out, each with a diagnostic build rather than by argument: thread stack
overflow (8 MB stack, 16 kB used at the failure); V8's own stack limit
(`--stack-size=200` changes nothing); Haiku dynamic TLS (a six-thread
local-dynamic test passes, and the whole PT_TLS segment is 1144 bytes); ICU
symbol collision (content_shell exports no ICU symbols); `memcpy`/`memmove`
overrun (48 sizes x 16 alignments, clean); arm64 outline atomics (all 101
helpers are defined in the binary); the rehash loop walking off the end of the
old map; the allocator handing out overlapping blocks
(**this one was measured against libroot's allocator, not the one the browser
uses -- see the PartitionAlloc section below**); a use-after-free of a
released zone segment (checked with a ring of released segments that is
invalidated when the address is handed out again); use of a destroyed
`AstValueFactory`; and V8's lazy compile dispatcher
(`--js-flags=--no-lazy-compile-dispatcher` still crashes).

**Read the next paragraph with the 2026-09-20 measurement in mind: every
"makes it go away" here may be an artefact.** The crash is intermittent. Twelve
runs of the same binary on the same page, three per configuration, came out:

	baseline                CRASH  ok     CRASH
	--single-threaded       ok     CRASH  ok
	--single-threaded-gc    CRASH  CRASH  ok
	--no-concurrent-sweeping ok    CRASH  ok

Every configuration both crashed and survived, `--single-threaded` included.
Ten runs per arm then settled it:

	baseline          XXXXXXXXXX   10 of 10 crashed
	--single-threaded .X..XX.XXX    6 of 10 crashed

	(X = crashed, . = still up after 70 s)

So **`--single-threaded` does not make the crash go away.** It lowers the rate,
which is what a timing change does, and on a 70-second limit some of those
dots are probably runs that had not got far enough yet rather than runs that
were safe. Baseline is deterministic, which makes it a good control; nothing
else measured here is.

That matters beyond the flag. "It goes away under `--single-threaded`" is what
made this look like a race between V8's own threads, and the list below --
every entry of it -- was built by running a diagnostic build once and seeing
whether the crash appeared. At one run per hypothesis, a change that does
nothing at all looks like a fix a third of the time. Treat the rest of this
section as leads, not as eliminated.

What does make it go away: `--js-flags=--single-threaded`, and, unhelpfully,
almost any change that shifts timing or layout -- guard pages under the hash
map's allocator, leaking zone segments instead of freeing them, adding a
member to `AstValueFactory`, adding an atomic to `GetString`, and, as of
2026-09-20, building the whole tree with `dcheck_always_on=true`. That pattern is
the strongest evidence there is that this is a race, and it is also why every
diagnostic has to be weighed against the possibility that it hid the bug
rather than explained it.

Where to look next: what this port supplies underneath V8 that could break the
sequencing Chromium assumes. The message pump is the first suspect, because
`eventfd` here is a UDP socket connected to itself
(`port/files/base/message_loop/epoll_shim_haiku.h`) and a lost or misdelivered
wake-up would let work run out of order. A build with `dcheck_always_on=true`
is the other obvious move: V8's zone and parser carry thread-affinity DCHECKs
that would name the violation outright.

### What the dcheck_always_on build said (2026-09-20)

The build finished, and it never got as far as V8. Three of this port's own
defects fired first, and two of them are the kind of thing the V8 corruption
has looked like all along -- an unsynchronised cross-thread access that a
release build says nothing about.

Three build fixes were needed before it would compile and start:

1. `base/message_loop/message_pump_epoll.cc` -- upstream's epoll_ctl debug
   history (`TODO(361611793)`) is DCHECK-only and reads the `epoll_event`
   handed to `epoll_ctl`. Haiku has neither. `port-message-pump.py` guards the
   three sites.
2. `gpu/config/gpu_control_list.cc` -- `GetOsType()` returned `kOsAny`, which
   in a release build silently makes every software-rendering and driver-bug
   entry match nothing, and with DCHECKs on is fatal on the first GPU-info
   lookup. `port-gpu.py` puts Haiku with Linux.
3. `ui/ozone/platform/haiku/haiku_window_manager.cc:29` --
   `DCHECK failed: thread_checker_.CalledOnValidThread()`. **A real bug.** viz
   calls `CreateCanvasForWidget()`, and so `GetWindow()`, on the compositor
   thread while windows are added and removed on the UI thread. The class
   asserted the opposite and held a `base::IDMap`, which is a hash map with no
   synchronisation at all. Note that locking `IDMap` is not the fix: it
   carries its own `SEQUENCE_CHECKER` on every method and means the same
   single-sequence promise. It is now a `std::map` under a `base::Lock`.

Then, on the first composited frame:

	FATAL:base/sequence_checker.cc:21 DCHECK failed:
	checker.CalledOnValidSequence(&bound_at)

	viz::DelayBasedTimeSource::OnTimerTick
	  viz::DisplayScheduler::OnBeginFrame ... viz::Display::DrawAndSwap
	  viz::SoftwareRenderer::FinishDrawingFrame
	  ui::HaikuCanvasSurface::PresentCanvas       <- haiku_surface_factory.cc
	  base::internal::WeakReference::IsValid

**The second real bug.** The canvas surface held a
`base::WeakPtr<HaikuWindow>` and dereferenced it every frame from the
compositor thread; a WeakPtr belongs to the sequence that created it, which is
the UI thread. In a release build that check is gone and what is left is an
unsynchronised read of `HaikuWindow::window_` racing the UI thread's
`DestroyWindow()`. The shim's own `PresentBitmap()` locks the looper and is
safe from anywhere, so the handle was the only unsafe part: it is now
`HaikuPresentTarget`, ref-counted, one pointer under a lock, cleared by the UI
thread in `Detach()`.

With those three fixed, the DCHECK build reaches the page, renders its splash
and goes on grinding without a single V8 complaint. **That was timing, not a
fix.** Rebuilt from the same sources with `/root/args.nodcheck.bak` restored,
x.com dies exactly as it always has:

	# Fatal error
	# Check failed: std::numeric_limits<int>::max() >= length_.

So the entry for the ruled-out list is: **the two Ozone cross-thread defects
are not the cause** (14), and, more usefully, **`dcheck_always_on` does not
reproduce the bug at all, so it cannot diagnose it** (15). That closes the
route this file recommended. Nothing was learned about V8's own thread and
zone DCHECKs, because the bug never occurred for them to fire on.

The two fixes are worth keeping on their own merits -- they are real
unsynchronised cross-thread access -- but they belong in the port's hygiene,
not in this investigation.

`/root/args.dcheck.bak` and `/root/args.nodcheck.bak` in the container hold
the two arg sets, and the DCHECK binary is kept at `/root/dcheck-build/` so it
does not have to be built again. A full rebuild between the two is about
65,000 edges and roughly 2.5 hours at `-j 14`; the container has 14 CPUs, so
do not leave ninja at the `-j 11` it was last found running with.

Note also that the x86 port fails differently on the same page -- V8's
embedded builtin code reads as zeros there -- so do not assume one fix covers
both. **(Settled 2026-09-20: the x86 zeros were not V8 at all. Its binary had
142 page-aligned zero pages written into `.text` and `.rodata` by a link run
outside `linkretry-verified.sh`, 59 of them over `Builtins_JSEntry`. A gated
relink plus a missing `/boot/home/rchromium-fonts.conf` got x.com rendering
there. Nothing carries over to this port -- the arm64 binary is built in a
container with memory to spare -- except the habit of checking that the bytes
you are reasoning about are actually in the file.)**

Comparing against an upstream Linux arm64 build of the same revision would
settle whether this is the port at all, but that build is blocked: Chromium's
Rust host build tools are x86_64 and fail under this aarch64 container, and
`enable_rust=false` breaks content_shell's mojom rust target.

### The allocator under V8 is PartitionAlloc, and it is the cause (2026-09-22, settled 09-23)

Every native test written against this crash -- 45 million concurrent
allocations with no overlap, the mmap reservation test, `ordertest.c` -- was
built with `aarch64-unknown-haiku-gcc` and linked against libroot. So all of
them exercised **Haiku's allocator**. The browser does not use it:

	out/haiku-arm64/gen/.../partition_alloc/buildflags.h
	  PA_BUILDFLAG_INTERNAL_USE_PARTITION_ALLOC_AS_MALLOC()  1
	  PA_BUILDFLAG_INTERNAL_USE_ALLOCATOR_SHIM()             1
	  PA_BUILDFLAG_INTERNAL_USE_RAW_PTR_BACKUP_REF_IMPL()    1
	  PA_BUILDFLAG_INTERNAL_ENABLE_BACKUP_REF_PTR_SUPPORT()  1
	  PA_BUILDFLAG_INTERNAL_USE_PARTITION_COOKIE()           0

`malloc` in content_shell is PartitionAlloc, every `raw_ptr<T>` is
BackupRefPtr, and PartitionAlloc's platform layer is something this port wrote
(`port-feature-flags.py` patches its separate copy of the platform detection,
its `sys/syscall.h` include and its POSIX page allocator;
`stack_trace_haiku.cc` is ours). The one component that has never been tested
is the one the corruption happens in. That is not a small gap -- it is the
gap.

Two details make it more than a general suspicion:

- **The map that comes back wrong is malloc memory, not zone memory.**
  `AstRawStringMap` is
  `base::TemplateHashMapImpl<..., base::DefaultAllocationPolicy>`
  (`v8/src/ast/ast-value-factory.h:233`), and `DefaultAllocationPolicy` is
  plain `malloc`/`free`. So the hash table whose keys stop pointing at live
  `AstRawString`s lives in PartitionAlloc, while the strings themselves live
  in a Zone. "A key does not point at a live AstRawString" is a corrupt
  *pointer in the table*, not an overwritten string -- which fits the table's
  backing array being damaged far better than it fits the zone being handed
  out twice, and the zone is exactly what has already been cleared.
  (`AstValueFactory`'s constructor `memcpy`s that array out of
  `AstStringConstants::string_table()` on every parse, on every thread --
  `base/hashmap.h:202`.)

- **BackupRefPtr decides what it owns by testing addresses against
  PartitionAddressSpace's pools**, which are many-gigabyte PROT_NONE
  reservations. Whether Haiku's mmap honours a reservation of that size, and
  whether the pool test then answers correctly, has not been checked. A pool
  test that answers wrongly has `raw_ptr` writing refcounts into memory
  PartitionAlloc does not own, which is the shape of the damage.

`ENABLE_BACKUP_REF_PTR_FEATURE_FLAG` is 1, so BRP is runtime-switchable.
Ten runs each against `repro2.html`, interleaved (`serve/xr2brp.sh`):

	baseline  XX.X.X....   4 of 10 crashed
	no-brp    .X....XXX.   4 of 10 crashed

**BackupRefPtr is not it** (16). Identical rates, and turning BRP off changes
slot layout, which is the one kind of change this bug has always been
sensitive to -- so this is a null result that would have been hard to get by
accident.

That does not clear PartitionAlloc. BRP is the pointer-authentication layer on
top of it; the thread cache, the slot spans and the freelist encoding are
untouched by that flag and untested by anything. The test that does clear it
is `use_partition_alloc_as_malloc=false`, which swaps the whole allocator for
Haiku's, and which the port already builds -- see the `malloc_dump_provider.cc`
note in `port-content.py`: that file is compiled only when PartitionAlloc is
not the malloc, and Haiku's missing `mallinfo()` is already handled there.
`base/allocator/allocator.gni` asserts that BRP cannot be used without
PartitionAlloc-Everywhere, so `enable_backup_ref_ptr_support = false` goes with
it; and the shim then has no dispatch to fall back to -- its non-PA default is
`allocator_shim_default_dispatch_to_glibc.cc`, which calls `__libc_malloc`,
and Haiku's libroot has no such symbol -- so `use_allocator_shim = false` goes
with it too. All three are one decision: do not use PartitionAlloc.
`out/haiku-arm64-nopa`, 40,030 edges after the first attempt failed at the
link on exactly that missing dispatch.

Both binaries went onto the transfer disk at once (`serve/reload-xfer-nopa.sh`
grows it from 700 MB to 1.4 GB), so the arms could be interleaved in one boot.
`serve/xpa.sh`, ten runs each against `repro2.html`:

	partitionalloc  .....X....   1 of 10
	libroot         ..........   0 of 10

That was read as "PartitionAlloc is not it" and it was wrong. The baseline had
moved from 4 of 10 to 1 of 10 between two runs of the same binary on the same
page, with a VM reboot the only thing in between, and a 1-in-10 control cannot
separate anything from anything. The same table rerun against x.com, which is
the heavier page, in the same boot, ten runs each interleaved:

	partitionalloc  .XXX.XX.XX   7 of 10
	libroot         ..........   0 of 10

and again, immediately, same boot, same script:

	partitionalloc  X.XXXXXX..   7 of 10
	libroot         ..........   0 of 10

**It is PartitionAlloc.** Fourteen of twenty against none of twenty, which
Fisher's exact test puts near 1e-5, and it is the first thing in three weeks
that has separated at all -- every other arm, including `--single-threaded`
and `dcheck_always_on`, only moved the rate around. The crashes are the usual
`Check failed: std::numeric_limits<int>::max() >= length_`, sometimes followed
by `Received signal 30 BUS_ADRALN 0x0`.

The build without it is a working browser, not merely a surviving one: x.com's
login card renders complete -- the X mark, the three provider buttons with
their glyphs, the terms text, the footer links -- and clicking "Email or
username" and typing `rainygirl` fills the field and turns Continue black.

`port/apply-haiku-port.sh` and `port/iterate.sh` now pass the three flags by
default. What the port gives up is PartitionAlloc's hardening (BackupRefPtr,
its guard pages and freelist checks) and its thread cache, so allocation is
slower and a use-after-free is no longer caught for free. That is the trade,
and it is worth taking until the actual defect inside PartitionAlloc's Haiku
platform layer is found.

Where to look for that defect, in the order the evidence supports:

- **`partition_alloc/page_allocator_internals_posix.h`**, which this port
  already patched once for `sys/syscall.h`. PartitionAlloc reserves its pools
  as large PROT_NONE mappings and then `mprotect`s pieces of them readable;
  whether Haiku's mmap honours a reservation of that size, and whether
  `mprotect` on part of one behaves, has never been checked.
- **The thread cache.** It is per-thread, it hands out and takes back slots
  without the central lock, and the crash needs more than one thread and
  disappears under any change of timing.
- **`partition_alloc_base/` platform code in general.** The port's own
  `port-feature-flags.py` records that PartitionAlloc keeps a third, separate
  copy of the platform detection, and that `PA_BUILDFLAG(IS_POSIX)` was 0
  until it was patched. Anything else that detection got wrong is still
  wrong.

The cheapest next measurement is `PA_BUILDFLAG(USE_PARTITION_COOKIE)`: build
with PartitionAlloc **and** cookies on, and see whether PartitionAlloc detects
the corruption itself and says where. That is a much smaller build than this
one and it turns "the allocator is at fault" into "the allocator says this
slot was overrun/double-freed at this size".

The lesson in the first table is worth as much as the finding in the second: a
null result measured against a weak control is not a null result. The note
under it said the control had no power and then drew a conclusion from it
anyway. When an arm comes out clean, check what the baseline was doing before
believing it.

### A local page that parses as hard as x.com does not crash (2026-09-22)

`serve/genjs.py` generates `repro.html`: eight `<script src>` bundles of about
800 kB each, 1,400 functions per bundle with unique identifiers and unique
object keys, seven of every eight never called so they are lazily skipped and
reparsed later, plus a driver that keeps calling `new Function()` and `eval()`
on source the process has not seen before. Served from the M4 over QEMU's user
network at `http://10.0.2.2:8000/repro.html`, so the bytes are identical every
run -- which x.com's are not, and that is why its baseline rate moved from
10/10 on 09-21 to 3/6 on 09-22 and why no flag can be bisected against it.

First run: **103,840 fresh parses in 50 seconds, no crash.** So raw parser
volume is not the trigger. What the page stops doing after its first few
seconds is the thing to look at: `new Function()` and `eval()` parse on the
main thread, and V8 only streams a script that is still arriving over the
network. `repro2.html` (`serve/gen2.py`) keeps four `<script src="gN.js?v=K">`
in flight for the whole run so background streaming parses never stop, which
is the shape of a real site's bundle load.

Ten runs of each, interleaved (`serve/xrep.sh`, 60 s limit):

	local  ....X..XX.    3 of 10 crashed
	x.com  .XXXXX.X.X    7 of 10 crashed

`repro2.html`, which keeps four `<script src="gN.js?v=K">` in flight for the
whole run so background streaming parses never stop, came out at **4 of 10**
in the run below. So the thing repro.html stopped doing after its first few
seconds was not the missing ingredient either; a local page sits at three or
four in ten whatever it parses and however.

**A page served from this machine does reproduce it.** That is the first time
this crash has been seen on anything whose bytes are known and repeatable, and
it retires the reason no flag could be bisected: x.com's rate moved from 10/10
to 3/6 to 7/10 across three days because the site does not serve the same
bundles twice, and nothing can be measured against a control that moves.

Two cautions. 3 of 10 against 7 of 10 does not separate (Fisher p is about
0.18), so this says the local page crashes, not that it crashes less. And 3 in
10 is a weak control in its own right -- an arm that comes out 0 of 10 against
it proves much less than one that comes out 0 of 10 against a 10 of 10
baseline. Strengthening the reproducer comes before using it, which is what
`repro2.html` is for.

## fd 0 closed under the browser: the intermittent naver crash (fixed 2026-09-15)

Symptom (2026-09-14/15 builds, single-process launcher, heavy pages): a few
seconds into news.naver.com,

    MessagePumpEpoll: unhandled poll revents 0x1000 on fd 0   (POLLNVAL)
    net_errors_posix.cc: Socket operation on non-socket
    platform_shared_memory_region_posix.cc: fcntl(0, F_GETFL) failed: Bad file descriptor
    FATAL base/types/expected_internal.h:310 Check failed: state_ == State::kValue

Root cause, caught with Haiku's strace (`strace -s -i -e
close,open,socket,socketpair,dup,dup2,create_pipe,accept -o log ./content_shell
...`; syscall names are given WITHOUT the `_kern_` prefix): 78 `close(0)` calls
in ~90 s, each from the thread that had just read
`/boot/system/settings/network/resolv.conf` and `hostname`, each paired with a
second `close(<stale number>)`. That is `res_nclose()` after `res_ninit()`.
Chromium memsets a `struct __res_state` to zero and calls `res_ninit()`;
glibc/BSD set `_vcsock` and `_u._ext.nssocks[]` to -1 there, Haiku's libbind
copy does not, and its `res_nclose()` closes every slot that is not -1 -- so
fd 0 (stdin) goes first, and afterwards whichever descriptor was most recently
handed out as 0 (a net socket, then a shared-memory file) is closed under its
owner. Two callers:
- `net/dns/dns_reloader.cc` (`DnsReloader`, per getaddrinfo call, per thread:
  `res_nclose`+`res_ninit` on every resolver-generation change) -- now
  compiled out on Haiku (`USE_RES_NINIT` excludes `IS_HAIKU`), as it already
  is on Android/Apple/Fuchsia; Haiku's libnetwork keeps its own state.
- `net/dns/public/scoped_res_state.cc` (`ScopedResState`, every DNS config
  read) -- on Haiku the destructor sets `_vcsock` and all `nssocks[]` to -1
  before `res_nclose()`; the state never sent a query so nothing leaks.
Both edits are in `port/port-net.py`. This also explains why earlier runs of
the same binary sometimes survived: the race is between the resolver's stray
close and whoever currently owns fd 0.

## CJK text and the font manager (fixed 2026-09-15)

Korean (and any CJK) text rendered as boxes wherever the page's first-choice
family lacked the glyph -- naver's nav happened to resolve straight to the
system Noto Sans CJK KR, its headlines did not. Cause: Chromium's Haiku font
manager was Skia's `SkFontMgr_New_Custom_Directory("/boot/system/data/fonts")`
and `SkFontMgr_Custom::onMatchFamilyStyleCharacter()` is literally
`return nullptr;`, so Blink's per-character fallback
(`font_cache_haiku.cc` -> `MatchFamilyStyleCharacter`) never found anything.
Its default family was also "whatever the scanner met first" (none of Skia's
defaults "Arial", "Verdana", ... exist on Haiku).

Fix, in `skia/ext/font_utils.cc` (now recorded as
`port/files/skia/ext/font_utils.cc`; this file, `skia/BUILD.gn`'s Haiku
directory-manager block and
`third_party/blink/renderer/platform/fonts/haiku/font_cache_haiku.cc` had
only existed in the container tree before -- the latter two are still
unrecorded drift): `HaikuFontMgr` wraps one `SkFontMgr_Custom` per font
directory, in priority order
`<exe dir>/fonts`, `/boot/system/data/fonts`, `/boot/system/non-packaged/data/fonts`,
`~/config/non-packaged/data/fonts`, `~/config/data/fonts`, and implements
`onMatchFamilyStyleCharacter`: try the requested family, then the Noto Sans
CJK regional face for the request's BCP 47 tags (ko -> KR, ja -> JP,
zh-Hant/TW -> TC, zh-HK/MO -> HK, other zh -> SC), then any CJK face, then
every other family, each tested with `SkTypeface::unicharToGlyph() != 0`.
`onLegacyMakeTypeface` falls back to "Noto Sans" before the scanner's first
family. Web fonts (`makeFromStream` & co.) delegate to the first directory
manager; all of them share the FreeType scanner.

Bundled fonts: the release tarball carries `fonts/NotoSansCJK-Regular.ttc` and
`fonts/NotoSansCJK-Bold.ttc` (Noto CJK OTCs from github.com/notofonts/noto-cjk,
`Sans/OTC/`, 10 faces each: Sans + Mono for JP/KR/SC/TC/HK; ~19-20 MB apiece)
plus `fonts/LICENSE-NotoSansCJK.txt` (SIL OFL 1.1). The RENKU image only ships
`NotoSansCJKkr-{Regular,Bold}.otf`, so JP/SC/TC glyph variants come from the
bundle. `packaging/make-release-tarball.sh` requires the three files.

## Arabic and colour emoji (added 2026-10-26)

A Renku nightly has no Arabic or emoji font, so both rendered as boxes.
`packaging/fetch-fonts.sh <dist>/fonts` (run by build-x64.sh's dist step)
now fetches every bundled font from pinned tags, checks SHA-256 sums and
caches them in `~/.cache/rchromium-fonts`: the CJK OTCs above (noto-cjk
`Sans2.004`, byte-identical to the old release tarball's),
`NotoSansArabic-{Regular,Bold}.ttf` (notofonts/arabic `NotoSansArabic-v2.013`,
hinted static) and `Noto-COLRv1.ttf` (googlefonts/noto-emoji `v2.051`), each
with its OFL 1.1 licence as `LICENSE-<family>.txt`.

Arabic needs no code: per-character fallback's last pass finds Noto Sans
Arabic, and `<app>/fonts` is scanned first. Emoji did: Chromium's FreeType is
built without `TT_CONFIG_OPTION_COLOR_LAYERS` and `FT_CONFIG_OPTION_USE_PNG`,
so neither COLRv1 nor CBDT draws through the FreeType scanner (Linux avoids
this by scanning with Fontations). `HaikuFontMgr` therefore reopens the
"Noto Color Emoji" family's file with `SkTypeface_Make_Fontations`, drops the
FreeType copy from its family list, answers `matchFamily("Noto Color Emoji")`
with it, returns it first when the request carries Blink's `und-Zsye` tag
(emoji presentation), and uses it as the very last fallback otherwise.

## End-user install: install.py, the release tarball, and the launcher

### Requirements on the device

- **Haiku on AArch64** -- the real renku-arm64 desktop (booted on Apple Silicon
  under QEMU/HVF, or arm64 hardware).
- **`A0001` runtime_loader TLSDESC patch in the image.** clang emits
  `R_AARCH64_TLSDESC` relocations that a stock Haiku loader cannot resolve, so
  the binary will not load without it. The patch is
  `haiku_kernel_patches/A0001-arm64-runtime-loader-tlsdesc.patch`; grafting it
  into an existing image's `haiku.hpkg` is described under "TLSDESC" below.
- **The standard Haiku fonts.** Text is shaped and rasterised against the system
  fonts; the stock RENKU/Haiku font set is enough (Korean on news.naver.com
  renders with it).
- **python3** (in the base image). The minimal RENKU image has no `curl`,
  `wget`, `tar`, `grep` or `sed`, which is why the one-liner and the installer
  are Python (`urllib` + `tarfile` + `hashlib`), not a `curl | sh`.
- About 400 MB free on the target volume (the unpacked tree is ~375 MB with fonts; the
  installer downloads next to the install dir and deletes the old copy before
  unpacking so it never needs two copies at once).

### What install.py does

`install.py` (thin wrapper: `install.sh`) downloads
`rchromium-arm64.tar.gz` from the latest GitHub release of
`rainygirl/renkuos-rchromium-arm64`, verifies it against the `.sha256` published
next to it, unpacks it into `~/config/non-packaged/apps/RChromium/`, and then:

1. writes a launcher script `RChromium/R Chromium` (see "Launcher flags"),
2. stamps `rchromium.hvif` on it as `BEOS:ICON` (`addattr -t icon`) -- stock
   `content_shell` has no signature or icon resources, and Tracker/Deskbar show
   the attribute through the symlinks,
3. symlinks the launcher to `~/config/non-packaged/data/deskbar/menu/Applications/R Chromium`,
   `~/Desktop/R Chromium`, and `~/config/non-packaged/bin/rchromium`.

Tracker runs an executable script on double-click (verified on the VM: the
Desktop link launches, and the team shows up in Deskbar as "R Chromium").
`--uninstall` removes the three links and the app dir; `--prefix DIR`,
`--file TARBALL` and `--url URL` exist for testing (`--prefix /chromium/RChromium`
on the test VM, whose `/boot` is a 450 MB volume). Arguments after the
`python3 -c "..."` one-liner reach the script through `sys.argv` as usual.

### Release tarball

`packaging/make-release-tarball.sh <dir> [out]` packs a deployable directory
into `rchromium-arm64.tar.gz` + `.sha256` (top-level dir `RChromium/`):

```
RChromium/content_shell
RChromium/content_shell.pak
RChromium/icudtl.dat
RChromium/snapshot_blob.bin            # must match the content_shell build
RChromium/v8_context_snapshot.bin      # ditto
RChromium/locales/en-US.pak
RChromium/lib/libchromium_haiku.so     # the BeAPI shim, 4 KB-page relinked
RChromium/lib/libtest_trace_processor.so   # DT_NEEDED by content_shell
RChromium/rchromium.hvif               # from assets/, added by the script
```

Publish it as the two assets of a GitHub release; `install.py` fetches
`releases/latest/download/rchromium-arm64.tar.gz`, so the asset name is fixed
and the release must not be a draft/pre-release.

Which binary is in the release (2026-09-15, third cut): `content_shell`
linked in the M4 `haiku-builder` container from the current tree (2026-09-14
tree with the baked kInProcessGPU/kDisableGpu switches) plus the title-bar and
resize fixes, the HaikuFontMgr CJK fallback, and the resolver fd-0 fix --
sha256 90f42b3bc925bb6b26bc049879c6bb4bf9a5f0b7991715246705f62e3f09a10f,
323735264 bytes -- with the shim rebuilt from `haiku_shim/haiku_shim.cc`
(sha256 e4b9ba3cc22db572307005bdfd31abf0b1a29b075b8cb4faef7b6359b693ac7f,
325344 bytes; build lines under "SHIM LOAD FIX" below:
`aarch64-unknown-haiku-g++ -O2 -fPIC -c` then
`-shared ... -Wl,-z,max-page-size=0x1000`), and `fonts/` (Noto Sans CJK
Regular+Bold OTCs + OFL license). The pak/icudtl/snapshot files are
byte-identical across the 09-13/14/15 builds. Verified on the VM: naver
(Korean headlines), ja.wikipedia, zh.wikipedia all render with correct glyphs,
window resize and new windows behave, and the fd-0 crash did not recur across
the three page loads (it reproduced 3 of the last 4 naver loads before the
resolver fix).

History of that choice: the first cut shipped the 2026-09-13 binary from the
test VM (sha256 not recorded, 323726096 bytes) because the 09-14 build had
failed once on the VM in the launcher's `--single-process` mode (FATAL in
`base/types/expected_internal.h:310` right after
`platform_shared_memory_region_posix.cc: fcntl(0, F_GETFL) failed: Bad file
descriptor`) and showed a blank page in its intended no-flag multi-process
mode. The rebuilt 09-15 binary (same tree + fixes) then ran the full
single-process test sequence without that crash, so the fd-0 failure is
intermittent, not deterministic -- keep an eye on it (fd 0 handling; see
`port-message-pump.py` and the `MessagePumpEpoll ... fd 0` note above). The
multi-process blank page on this VM is still real and is why the launcher uses
`--single-process`.

### Launcher flags

The launcher execs
`content_shell --ozone-platform=haiku --no-sandbox --single-process --disable-gpu --in-process-gpu --disable-gpu-compositing "$@"`
from the app dir with
`LIBRARY_PATH=%A/lib:~/config/non-packaged/lib:~/config/lib:/boot/system/non-packaged/lib:/boot/system/lib`
(`%A/lib` is how the runtime loader finds the shim next to the binary).

Why every flag, measured on the VM with the release binary:
- no flags: the display compositor runs in a separate viz process, which has no
  window manager -> `CHECK(surface_ozone)` FATAL (the crash analysed under
  "naver crash SOLVED" below);
- `--in-process-gpu --disable-gpu` only (multi-process renderers): no crash,
  page finishes loading, but the content area stays white -- frames are not
  delivered to the BWindow from out-of-process renderers on this build;
- the full set with `--single-process`: news.naver.com renders completely.
  (Passing these to a binary that already bakes the switches is harmless: the
  baked code checks `HasSwitch` first.)

### Manual deployment (without install.py)

Put the binary, its resources, and the shim together on any writable BFS
volume, e.g. `cs/`:

```
cs/content_shell                 # the binary
cs/content_shell.pak             # resources ...
cs/icudtl.dat
cs/snapshot_blob.bin
cs/v8_context_snapshot.bin
cs/locales/
cs/lib/libchromium_haiku.so      # the native BeAPI toolbar shim
cs/lib/libtest_trace_processor.so
```

then `cd cs && LIBRARY_PATH=$(pwd)/lib:/boot/system/lib ./content_shell <flags> <URL>`
with the launcher flags above. On the test VM `/boot` is tiny, so also point
`TMPDIR` at a writable volume.

### Troubleshooting

- **`libchromium_haiku.so: Troubles handling dynamic section` at launch** -- the
  shim was not linked with 4 KB pages. Relink with
  `-Wl,-z,max-page-size=0x1000` (see the shim-load section below).
- **`Bad data relocating` / the binary refuses to load** -- the image's loader
  lacks the `A0001` TLSDESC patch.
- **`install.py` fails with "No space left on device"** -- ~350 MB is needed on
  the volume holding the prefix, and the Desktop/Deskbar links need a few KB on
  `/boot`. On the test VM, stale `core-*` dumps on the Desktop were what filled
  `/boot`.
- **Window opens, content stays white** -- the launcher flags were bypassed
  (e.g. the binary was run directly); use `rchromium` or the Desktop link.
- **Black QMP screenshot** -- the VM's screen blanker; an `input-send-event`
  mouse move wakes it.

## Repository layout

```
port/              the port generator: apply-haiku-port.sh + port-*.py + files/
rust/              the aarch64-unknown-haiku Rust toolchain patch and installer
toolchain-build/   cross-build of the sysroot, Python 3.14, gn/ninja, clang/lld
haiku_shim/        libchromium_haiku source: BeAPI subclasses compiled with the
                   system gcc, kept out of Chromium's clang objects (RTTI reasons)
packaging/         the hpkg PackageInfo
```

The Chromium checkout itself is not vendored here (tens of GB). `port/` is the
scaffolding applied on top of a fetched `chromium/src`.

## Building

Inside the `haiku-builder` container (Linux, cross-compiling to Haiku arm64):

```sh
# 1. Toolchain and build-time packages (sysroot, python, gn/ninja, clang/lld).
#    Hours; each step is skipped if its output already exists.
toolchain-build/build-all-arm64.sh

# 2. Rust with an aarch64-unknown-haiku target (Chromium's Rust cannot be
#    switched off, and stock rustc has no Haiku arm64 target).
rust/build-rust-arm64.sh
rust/install-rust-toolchain.sh <chromium-src>

# 3. Apply the Haiku port to a fetched Chromium checkout.
port/apply-haiku-port.sh <chromium-src>
python3 port/port-ozone.py <chromium-src>   # registers the Ozone platform + copies ui/ozone/platform/haiku

# 4. Configure and build.
cd <chromium-src>
gn gen out/haiku-arm64 --args='
    target_os="haiku" target_cpu="arm64" is_debug=false symbol_level=0
    haiku_sysroot="/root/pybuild/sysroot"
    haiku_gcc_lib_dir="<print-file-name of crtbeginS.o dir>"'
ninja -C out/haiku-arm64 chrome
```

The BeAPI shim library is built separately from `haiku_shim/` with the same gcc
that built `libbe` (not Chromium's clang) and linked as `libchromium_haiku`;
`haiku_shim/haiku_shim.h` explains why. It must be relinked with 4 KB pages
(`-Wl,-z,max-page-size=0x1000`), or the arm64 loader mis-maps its gcc-default
64 KB segments -- see the shim-load section below and the README deploy notes.

## Relationship to the x86 port

`../rchromium-native-x86/` has its own, independently written native toolbar
(Chromium 87, BControlLook-drawn) that meets the same user spec. It is not a
backport of this one and this is not a backport of it -- the Chromium 87 vs 154
APIs differ, so no source is shared. What IS shared is the design: a widget-keyed
bridge so content_shell never sees a BeAPI type, RTTI isolation in a separately
built shim, an explicit `platform_->aura->ShowWindow()` in the delegate, and the
SetAddressBarURL / SetIsLoading / EnableUIControl hook mapping. Both toolbars
already exist and work; do not reimplement one to match the other unless the user
asks to converge the two styles. (See also "## The x86 sibling already has its
own native toolbar" above for the chronological detail.)

### naver --single-threaded live test: staged, blocked on one step (2026-09-13)

Built an isolated arm64 content_shell repro VM on the M4 to run the decisive
`--js-flags=--single-threaded` test. Progress and the exact blocker:

- Second QEMU (does NOT touch the running rworldradio VM): boot renku image with
  `snapshot=on`, plus a 1.5 GiB sparse data disk `/tmp/csdata.img` as a second
  usb-storage; port 2325, QMP `/tmp/cstest.qmp`, serial `/tmp/cstest-serial.log`.
  Launcher: `/tmp/cstest-launch.sh` on the M4 (uses
  `/opt/homebrew/bin/qemu-system-aarch64` -- qemu is NOT on the non-interactive
  ssh PATH). Guest login `baron@127.0.0.1` key `~/.ssh/renku_guest`, add
  `-o UserKnownHostsFile=/dev/null` (host key changes per image).
- In-guest: `mkfs -q -t bfs /dev/disk/usb/1/0/raw csdata`, then
  `mount -t bfs /dev/disk/usb/1/0/raw /boot/home/cs`. /boot has only ~200 MB
  free (450 MB BFS), so content_shell (323 MB) MUST go on the data disk.
- Deployed the EXACT crashed binary set (out/haiku-arm64/content_shell 08:46,
  the one symbolized) via `docker exec haiku-builder cat FILE | ssh guest 'cat > DEST'`
  (script `/tmp/csdeploy2.sh`): content_shell, content_shell.pak, icudtl.dat,
  snapshot_blob.bin, v8_context_snapshot.bin, locales/en-US.pak, and
  lib/{libchromium_haiku.so (haiku_shim 08:44, the matching shim),
  libtest_trace_processor.so}. Launch: `LIBRARY_PATH=<csdir>/lib:/boot/system/lib
  TMPDIR=<csdir>/tmp ./content_shell --ozone-platform=haiku --no-sandbox
  --single-process --disable-gpu --in-process-gpu --disable-gpu-compositing
  --user-data-dir=<csdir>/profile https://news.naver.com` (add
  `--js-flags=--single-threaded` for the test).

BLOCKER -- loader/image compatibility. content_shell + libtest_trace_processor.so
use arm64 TLSDESC relocations (124 / 9) and the shim needs libstdc++ symbol
versioning (GLIBCXX_3.4.29, CXXABI_1.3.9). The bootable renku-pipeline images on
the M4 (renku-fix2, renku-nossldev; login = renku_guest key) are NOT compatible:
  - renku-fix2: runtime_loader lacks TLSDESC -> `libtest_trace_processor.so:
    Troubles relocating: Bad data`.
  - renku-nossldev: handles TLSDESC but its loader rejects the shim ->
    `libchromium_haiku.so: Troubles handling dynamic section` (a different-vintage
    loader; /root/haiku-renku's parse_dynamic_segment would accept this shim).
The only compatible image is the container's `/root/gen-arm64/haiku-minimum-anyboot.iso`
(09-12 01:47), built from the SAME /root/haiku-renku tree as content_shell/shim/
loader (TLSDESC-patched loader, matching libstdc++). BUT it authorizes a DIFFERENT
ssh key (pubkey ...IFKSUH..., whose private half is not on the M4 or in the
container) instead of renku_guest (...IPrjCi...), so it cannot be logged into.

To unblock: inject the renku_guest pubkey into the gen anyboot's
`home/config/settings/ssh/authorized_keys`. The image's BFS partition starts at
byte offset 4194304 (MBR part0, type 0xeb); extract with
`dd if=iso of=bfs.part bs=512 skip=8192 count=921600`, edit with the host tool
`/root/gen-arm64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell`
(`cp :/tmp/newauth /myfs/home/config/settings/ssh/authorized_keys`), `dd` the
partition back (`seek=8192 conv=notrunc`), copy to M4, boot on 2325 with
csdata.img attached, mount, run baseline then `--single-threaded`. This write was
blocked by the auto-mode "Unauthorized Persistence" classifier (writing ssh
authorized_keys into a boot image); it needs the user's explicit go-ahead.
Alternative without image modification: drive the gen anyboot via QMP keyboard in
a Terminal, write logs to the shared csdata disk, then read them from a renku_guest
image with csdata attached.

### naver --single-threaded live test: TLSDESC loader blocker SOLVED, shim blocker + Docker wedge (2026-09-13, later)

Pushed the live-test setup much further. Key results:

TLSDESC LOADER FIX (reusable). content_shell (124 R_AARCH64_TLSDESC relocs) and
libtest_trace_processor.so (9) need a runtime_loader with TLSDESC support. The
renku-verify boot images on the M4 (renku-fix2, renku-nossldev; login = baron +
~/.ssh/renku_guest) have loaders that LACK it ("Troubles relocating: Bad data").
The gen-arm64 anyboot (/root/gen-arm64/haiku-minimum-anyboot.iso) has a TLSDESC
loader but is a "minimum" profile that hangs at the boot splash in QEMU (never
reaches userland/network/sshd), so it is unusable for a networked test. Fix:
graft the TLSDESC loader into a renku-verify image's haiku.hpkg. Steps (all in
the haiku-builder container, which has 846G free vs the M4's ~2G):
  1. Copy renku-nossldev.iso into the container; parse MBR part0 (type 0xeb, LBA
     8192 = byte offset 4194304); `dd ... skip=8192 count=921600` to /tmp/noss.part.
  2. Rebuild the loader from CURRENT source (the checked-out
     src/system/runtime_loader/elf_load_image.cpp is NEWER than the prebuilt
     00:59 loader, so use jam, not the stale binary):
     `cd /root/gen-arm64 && /haiku-build/buildtools/jam/bin.linuxx86/jam -q -j1 runtime_loader`
     -> objects/haiku/arm64/release/system/runtime_loader/runtime_loader.
  3. Replace it inside the image's haiku pkg with the host package tool:
     `package add -f -C <dir> /tmp/haiku_noss.hpkg runtime_loader` (the loader is
     at package path "runtime_loader", TOP level = /boot/system/runtime_loader,
     NOT "system/runtime_loader"). Extract/put the hpkg with bfs_shell
     (`cp /myfs/system/packages/haiku-...hpkg :/host`, and back), always issue
     `sync` before `quit` (bfs_shell's unmount reports "busy" but sync flushes).
  4. dd the partition back into an iso copy (`seek=8192 conv=notrunc`), copy the
     iso to the M4, boot on 2325 with csdata.img attached.
Result: the grafted image BOOTS in ~40s to sshd (loader<->libroot ABI across
hrev99000 libs + hrev99002+175 loader is compatible), and content_shell now gets
PAST libtest_trace_processor.so (TLSDESC resolved). So the TLSDESC blocker is
solved. Host tools: bfs_shell/package/anyboot at
/root/gen-arm64/objects/linux/x86_64/release/tools/.

OPEN BLOCKER -- the shim fails to load: `libchromium_haiku.so: Troubles handling
dynamic section`. Instrumented the loader (FATAL uses dprintf -> guest
/var/log/syslog, and printf to stdout only while !gProgramLoaded, so read the
guest syslog, not just the app's stderr). syslog shows `runtime_loader: DBG no
symhash` -> parse_dynamic_segment's `if (!image->symhash) return false` fires:
image->symhash is null for the shim even though `readelf -d` shows it HAS DT_HASH
(0x120) (and, after relinking with `-Wl,--hash-style=both`, GNU_HASH too). The
DT_HASH case sets symhash = d_ptr + regions[0].delta, which should be non-null.
content_shell and libtest (both have DT_HASH + GNU_HASH) load fine; only the
gcc-built, libstdc++/symbol-versioned shim hits this. NOT yet root-caused --
needs runtime tracing of the dynamic-tag loop for the shim (print each d_tag +
the symhash assignment). The shim relinked with GNU_HASH is at
container:/tmp/libshim_gnu.so; deployed at csdata:/boot/home/cs/lib/.

INFRA: Docker Desktop's engine wedged after many rapid `docker exec` streams
(even `docker version`/`docker ps` hang; `killall Docker; open -a Docker` from
SSH relaunched the app - 22 procs - but the engine did not come back, likely
needs a Mac GUI restart of Docker Desktop). All container work is blocked until
Docker is healthy. Artifacts persist in the container's writable layer; the last
good grafted+debug image is on the M4 at /tmp/nossdbg.iso, csdata.img holds the
deployed content_shell. Launcher /tmp/cstest-launch.sh (ISO= line points at the
image; boot on port 2325, QMP /tmp/cstest.qmp).

NOTE on the ultimate fix: even once content_shell runs and --js-flags=
--single-threaded confirms the hypothesis, the actual V8 fix needs a Chromium
rebuild (a second ninja), which the port rules forbid in this state -- so the
diagnostic is the deliverable here, and the fix itself is a follow-up build.

### naver --single-threaded live test: DONE. Shim-load fixed, decisive results (2026-09-13, resolved)

The live test ran to completion. Two loader blockers were fixed, then the
decisive experiments were run.

SHIM LOAD FIX (root-caused). libchromium_haiku.so failed
`Troubles handling dynamic section`. Instrumented runtime_loader
(FATAL->dprintf lands in the guest /var/log/syslog, not the app's stderr): the
loop in parse_dynamic_segment read `d[0].d_tag==0` immediately, i.e.
image->dynamic_ptr pointed at zeroed memory, so symhash stayed null. Cause: the
shim is gcc-linked and uses ld's default arm64 `max-page-size=0x10000` (64 KB),
which gives its RW LOAD segment a 0x10000 offset<->vaddr skew (LOAD offset
0x215d8 / vaddr 0x315d8; DYNAMIC vaddr 0x32210). Haiku's arm64 runtime_loader
mis-maps that layout so DYNAMIC's mapped address is wrong. clang-built
content_shell/libtest use offset==vaddr and are fine. FIX: relink the shim at
4 KB page alignment (matches Haiku arm64) --
`aarch64-unknown-haiku-g++ -shared haiku_shim.o -Wl,-soname,libchromium_haiku.so
-Wl,--hash-style=both -Wl,-z,max-page-size=0x1000 -lbe -lstdc++ -lroot`. With
that, the shim loads and content_shell runs to the page. (This should be folded
into the shim's real build.)

DECISIVE RESULTS (content_shell running naver on the grafted TLSDESC image):
- Baseline naver: crashes with the same V8 stack as csn2.log (AstRawString::Equal
  <- TemplateHashMapImpl::Resize <- AstValueFactory::GetString <-
  Parser::ParseOnBackground <- BackgroundCompileTask::Run). Repro confirmed.
- `--js-flags=--single-threaded`: STILL crashes, identical stack -> NOT a
  background-thread race / torn write.
- Free RAM at crash ~4.0 GB of 4.28 GB -> NOT memory exhaustion.
- `--disable-http-cache --v8-cache-options=none` + empty profile: STILL crashes
  -> NOT the disk/V8 code cache or storage path.
- Address-space overlap test (the peer's hypothesis B, run natively on arm64 via
  a cross-compiled C probe): reserve 256 MB PROT_NONE|MAP_NORESERVE, then do anon
  + file mmaps -> ALL land ABOVE the reservation, overlap_hits=0; mprotect RW/RWX
  commit into the reservation works. So on arm64 Haiku, foreign mmaps never land
  inside a V8/PA-style reservation -> hypothesis B is REFUTED on arm64. (It may
  still hold on x86, which is 32-bit / ~2 GB user space and address-cramped; the
  x86 session sees a parallel V8 StringTable crash whose fault address is in the
  >2 GB kernel range. Same V8 string-table subsystem, likely DIFFERENT root cause
  per architecture.)

CONCLUSION: the arm64 naver crash is a DETERMINISTIC corruption of V8's
AstValueFactory string_table_ (base::TemplateHashMapImpl) during parse of naver's
heavy JS -- independent of threading, memory pressure, disk/code cache, and
address-space overlap. An entry ends up with its exists bit set but a null/garbage
key (the prior "occupancy overcounts" guard in v8/src/base/hashmap.h Resize is a
symptom of the same thing). Remaining candidates: a V8 arm64 codegen/zone-
alignment bug, or inconsistent AstRawString hashing that makes Probe double-insert
on naver's specific strings. Pinning it needs V8 instrumentation = a Chromium
rebuild (a second ninja), which the port rules forbid in this state -- so this is
the handoff point: root-cause direction established and every environmental cause
ruled out; the fix is a follow-up build task.

Repro env (reusable): grafted TLSDESC image /tmp/nosstrace.iso on the M4 (boot
via /tmp/cstest-launch.sh on port 2325), content_shell + 4 KB-relinked shim on
csdata.img mounted at /boot/home/cs. The arm64 B-probe source is in the session
scratchpad (btest.c) and cross-compiles with aarch64-unknown-haiku-gcc.

### naver crash: ROOT CAUSE confirmed via instrumented rebuild + fix direction (2026-09-13, option 1)

User authorized lifting the no-second-ninja rule for a targeted V8 instrumentation
build. Results:

INSTRUMENTED DUMP (built content_shell with a scan in ast-value-factory.cc
GetString that dumps any string_table_ entry whose key pointer is null/misaligned):
    V8DBG stomped entry: key=0x0 hash=0x7fffffff occ=1639 cap=4096
        map=0x1c009cc000 entry=0x1c009cc000
    V8DBG entry16: 00 00 00 00 00 00 00 00  ff ff ff ff ff ff ff ff
    Received signal 11 SEGV_ACCERR 001c009cbfe0   (reading 0x20 before map base)
Reading: the FIRST entry of the string table's backing (== the allocation base)
is overwritten with {key = 0x0, hash_and_exists_ = 0xffffffff}. The exists bit
(31) is set so it looks "occupied", but key is null -> Resize/Probe feeds a null
key to AstRawString::Equal -> crash. The stomp pattern is 8 bytes 0x00 then 8
bytes 0xFF (= {ptr=0, u64=0xffffffffffffffff}) -- a specific sentinel, not random
heap junk. The page immediately before the backing is PROT_NONE (the -0x20 read
faulted SEGV_ACCERR) -> the backing is the START of a PartitionAlloc slot span.
So: a browser-side write puts {0, -1} at the start of a PA slot that happens to
be the renderer's V8 string-table backing.

MECHANISM confirmed by the three-way process test (both platforms):
  - empty profile path + single-process = renders fine
  - valid profile path + single-process = crash during heavy V8 work
  - valid profile path + MULTI-process  = renderer passes V8, no stomp
=> It is NOT a renderer/V8 internal defect. In single-process a browser-side
subsystem shares the address space and stomps the renderer's V8 heap/zone;
isolating the renderer into its own process removes the stomp.

FIX EXPERIMENTS (arm64):
  - off-the-record main context (ShellBrowserContext(true), shell_browser_main_parts.cc
    :141) -- the x86 session's fix: makes the default storage partition in-memory,
    so on-disk leveldb/disk_cache/etc. never start (verified: the profile dir gets
    only DevToolsActivePort, no Code Cache/LocalStorage/blob_storage/DIPS). On arm64
    this fixes example.com and a data: JS page (100k loop + DOM) -- so off-the-record
    does NOT break JS -- but naver STILL crashes with the same parse stack. So on
    arm64 the stomper is NOT the on-disk storage subsystem; it is an in-process
    browser component active during heavy page load (in-memory storage service,
    network service, or something that scales with the page).
  - MULTI-process (drop --single-process): naver runs with NO V8 crash, NO
    font_cache CHECK (arm64 has standard Haiku fonts in /boot/system/data/fonts/
    {ttfonts,otfonts,psfonts}), only a benign webrtc cpu_info ERROR. This is the
    working arm64 mitigation for the V8 stomp. (Visual render not confirmable on
    renku-nossldev, which boots headless -- sshd but no app_server/desktop.)

STATUS: root cause is a single-process browser-side write of {0,-1} into a PA slot
holding the renderer's V8 string table. off-the-record is insufficient on arm64;
multi-process avoids it. Remaining to fully fix: either (a) identify and fix the
exact browser subsystem doing the stray write (needs writer-side instrumentation),
or (b) ship content_shell multi-process (needs the renderer child-process launcher
to be solid on Haiku; on arm64 it already runs naver without the font/V8 crashes).
The x86 session is pursuing the same on its hardware (its 32-bit JSEntryTrampoline
SIGILL under off-the-record is suspected fragmentation, since arm64 64-bit runs JS
fine under off-the-record).

### naver crash: subsystem bisection (2026-09-14, option 1 continued)

To pin WHICH browser subsystem writes {0,-1}, ran a bisection (all single-process,
off-the-record build; the VM is not confounded -- the negative cases below prove
it, and naver reproduces deterministically):
  - Local 2 MB JS, 60,000 unique identifiers (file://, max string-table pressure,
    NO network): NO crash. => not parse volume / string-table size alone.
  - Same 2 MB JS over HTTP (one network request + heavy parse): NO crash.
    => not "any network".
  - example.com (static, https): NO crash. daum.net (another heavy https portal):
    NO crash. => not general heavy-https.
  - news.naver.com (https, hundreds of concurrent subresources): CRASH, every time,
    same V8 string-table stack (BackgroundCompileTask + PartitionRoot::Alloc).
  - off-the-record (in-memory storage, no on-disk leveldb/cache): still CRASH.
  - multi-process: NO crash.
Conclusion: the trigger is naver-SPECIFIC heavy concurrent load -- many subresources
fetched concurrently WHILE V8 background-parses naver's specific JS -- in a single
address space. A browser-side thread (network-service-class, active for the
concurrent fetches; NOT on-disk storage, NOT parse volume) writes {ptr=0,
u64=0xffffffffffffffff} to the start of a PartitionAlloc slot that happens to hold
the renderer's V8 AstValueFactory string-table backing, corrupting entry[0]
(exists bit set, null key) -> deterministic crash. Isolating the renderer
(multi-process) removes it.

Pinning the exact subsystem/line further needs either ASAN (large/uncertain on
Haiku arm64) or a live write-catch (intractable: dynamic hot victim, async one-shot
foreign write, no gdb/watchpoint timing on the guest). Practical fix remains
multi-process; the deep fix is finding the stray {0,-1} writer in the network/
browser path under concurrent load.

### naver: SOLVED via multi-process (2026-09-14)

Proof the crash is fixed: run content_shell WITHOUT --single-process (multi-process,
renderer isolated) and load news.naver.com. DevTools /json/list on the renderer
reports:
    "title": "Naver News", "url": "https://news.naver.com/", "type": "page"
i.e. naver's HTML+JS parsed and executed (it set the page title) with NO crash --
the exact page that crashes deterministically in single-process. So the fix does
not need ASAN / the exact stray-write line: isolating the renderer removes the
single-process browser-side stomp of the renderer's V8 string table.

ASAN is not available here (Chromium's bundled clang ships asan runtimes only for
Linux targets, none for aarch64-unknown-haiku; sanitizers.gni has no Haiku), so the
exact writer line stays open -- but it is not needed to make naver work.

FIX to ship: run RChromium arm64 multi-process (drop --single-process). The native
BeAPI toolbar is drawn by the browser process (shim/ozone), the page by the
renderer process, which matches real Chromium's split -- so multi-process is
compatible with the native toolbar. Remaining engineering: make the renderer
child-process launcher solid on Haiku and set multi-process as the default launch.
To confirm visually, boot a desktop-capable arm64 image (the renku-verify images
boot headless -- sshd but no app_server desktop on ramfb -- so screenshots are
black; functional proof is via DevTools as above).

### Shipping multi-process (2026-09-14)

--single-process was never in the port code -- only in the launch commands in this
file. The Haiku child-process launcher is a complete first-class implementation
(content/browser/child_process_launcher_helper_haiku.cc, 135 lines: direct launch,
no zygote/sandbox, GlobalDescriptors fd remap, EnsureProcessTerminated reaping,
priority, OpenFileToShare; sandbox_type.cc has the IS_HAIKU branches). So
multi-process is already supported; the only change to ship is dropping
--single-process from the launch.

CANONICAL LAUNCH (multi-process, the fix -- use this, NOT --single-process):
  LIBRARY_PATH=<csdir>/lib:/boot/system/lib TMPDIR=<writable> \
  content_shell --ozone-platform=haiku --no-sandbox \
    --disable-gpu --in-process-gpu --disable-gpu-compositing \
    --user-data-dir=<writable>
(With --in-process-gpu the viz/display compositor runs in the browser process and
presents through the ozone Haiku surface exactly as in single-process, so the
present-to-BWindow path is unchanged; only the renderer runs in its own process.)

Regression (multi-process, DevTools /json confirms the renderer loaded each page
with no crash): news.naver.com -> title "Naver News"; www.google.com -> page
target present. No V8 stomp, no font_cache CHECK. The native BeAPI toolbar is drawn
by the browser process (shim/ozone) and the page by the renderer, matching real
Chromium's split, so multi-process is compatible with the native toolbar.

Not yet done: (1) VISUAL confirmation of the composited page in the BWindow --
blocked because the renku-verify images boot headless (no app_server desktop on
ramfb; screenshots are black) and the desktop-capable gen-arm64 anyboot hangs at
the boot splash in this QEMU config; needs a desktop-capable arm64 image or
on-device (renku-arm64) verification. (2) The exact browser subsystem doing the
{0,-1} stray write in single-process (needs ASAN, unavailable on Haiku) -- not
required now that multi-process avoids it.

### CORRECTION (2026-09-14): naver is NOT solved by multi-process -- on-device visual test

The earlier "SOLVED via multi-process" claim was WRONG. It rested on the headless
renku-nossldev test, where DevTools showed the DOM (title "Naver News") but the
renderer had not reached full render and there was no window to see. The on-device
visual test corrects it.

Method: grafted the TLSDESC runtime_loader into the DESKTOP image renku-arm64.iso
(hrev99002_173; MBR part0 BFS at 4194304; package add the loader into
haiku-...hpkg, same workflow as nossldev), booted it on the M4 with ramfb+QMP and
the content_shell csdata.img as an AHCI second disk (a second USB disk hangs the
splash; AHCI shows as /dev/disk/scsi/0/0/0/raw). renku-arm64.iso boots to a full
Haiku DESKTOP on ramfb (QMP screenshots are real, not black). Login: baron with
the image's key = the LOCAL Mac's ~/.ssh/id_ed25519 (pubkey ...IFKSUH...; the
same key the container bakes), so to ssh from the M4 that private key must be on
the M4.

Real-hardware matrix (QMP screenshots):
                simple data: page          news.naver.com
  single-proc   RENDERS (bg #cfe + <h1>)   CRASH (whole app dies, window gone)
  multi-proc    window+toolbar, content     renderer CRASH (window+toolbar stay,
                BLANK (compositing not       content blank; SEGV_MAPERR ...010)
                delivered to the BWindow)

So:
- The native BeAPI toolbar UI DOES render on device (window, address field showing
  https://news.naver.com/, back/forward/reload/star/menu). Simple pages render in
  single-process. The TLSDESC-loader graft + 4 KB-relinked shim work on device.
- naver's page content does NOT render. The renderer crashes with the V8 string-
  table stomp (SEGV @ +0x10) during heavy background compile of naver's JS, in
  BOTH single-process (whole process dies) and multi-process (renderer dies).
  Because it still crashes with the renderer ISOLATED (multi-process), the stomp is
  RENDERER-INTERNAL V8, not a browser-side write -- correcting the earlier
  "browser-side stomp" reading (that too came from the headless false-negative).
- Not avoided by: process model, --js-flags=--single-threaded, --js-flags=
  "--no-lazy --no-concurrent-recompilation --no-parallel-compile-tasks-*",
  off-the-record/in-memory storage, or disabling http/V8 cache.
- Multi-process additionally does not composite renderer content into the BWindow
  on this port (blank content even for a simple page that single-process draws
  fine) -- a separate cross-process present bug.

Bottom line: naver is still broken. The real fix is the V8 string-table corruption
during heavy background compile (the deterministic {0,-1} stomp of the
AstValueFactory backing), which needs V8-level debugging that is currently blocked
(no ASAN runtime for Haiku; a live write-catch is intractable here). Single-process
renders simple/light pages; heavy pages like naver crash.

### naver crash ROOT CAUSE confirmed: use-after-free of the live V8 string table (2026-09-14)

Traced the naver crash to a definitive use-after-free (NOT a stomp, NOT link
corruption):

- The corrupted "entry" bytes are {0x00 x8, 0xFF x8}. This is exactly a
  PartitionAlloc EncodedNextFreelistEntry for a nullptr next: the default freelist
  is EncodedFreelistPtr; Transform() is ReverseBytes on little-endian, so
  Transform(0)=0 (encoded next), and the shadow is Inverted()=~0=0xffffffffffffffff.
  So a freed PA slot that is the LAST on its freelist (next==nullptr) has first 16
  bytes {0, ~0} -- precisely the observed pattern. The string-table backing is a
  freed PA slot being read as a hashmap entry.
- V8DBG showed the corrupted entry is entry[0] of the CURRENT live map_
  (impl_.map_, == the allocation base, PROT_NONE guard page just before it =
  PA slot start), found by scanning string_table_ at GetString entry. So it is the
  LIVE string_table_ backing that has been freed -- while the AstValueFactory is
  still parsing and calling GetString -- not the old array Resize frees, and not
  after the factory's destruction. Something frees the live map_ slot mid-parse
  (a stray free / double-allocation of that address by other code).
- NOT link corruption: JS executes correctly on arm64 (a data: page computed
  6*7=42 and sum(i^2, i=0..999)=332833500 and rendered them). So the V8 embedded
  builtins blob is intact (unlike the x86 session's link-corruption SIGILL, which
  was its old ld with low-memory flags overwriting the blob -- an x86-only issue).
- PA is not generally broken (JS array allocations work). The UAF is specific to
  naver's heavy load.

The AstValueFactory string_table_ is deep-copied from AstStringConstants'
string_table_ (ctor `string_table_(string_constants->string_table())` ->
TemplateHashMapImpl(const*, ...) allocates a new backing and memcpy's), so it owns
its backing; it is freed in the (defaulted) dtor or in Resize (old array). The
crash reading the CURRENT map_ as freed means an external free of the live slot.

Fix direction: this is a V8/allocator lifetime bug -- a stray free or double-alloc
of the live AstValueFactory string-table backing during heavy background compile
on arm64. Pinning the exact free-site needs allocator-level tracing (log
base::Malloc/base::Free or PA alloc/free of that address with backtraces) or a
dcheck_always_on / PA double-free-detection build -- both heavy rebuilds. The
crash is avoided only by not parsing that much (light pages render fine; heavy
pages like naver crash). Multi-process does NOT help (the UAF is in the renderer's
own V8), and neither do --single-threaded, --no-lazy/--no-parallel-compile,
off-the-record, or cache flags.

### naver on-device: renders intermittently; general UAF, needs ASAN-class tooling (2026-09-14)

On the real renku-arm64 desktop image (renku-arm64.iso, TLSDESC loader grafted into
its haiku hpkg, content_shell on an AHCI-attached csdata.img; boots to the RENKU
desktop on ramfb, QMP screenshots are real):
- news.naver.com RENDERED fully once (native BeAPI toolbar + full naver News: nav
  tabs, news cards, LIVE thumbnails KBS/YTN/MBN, Korean text) with the
  PADBG-instrumented build. But a re-run CRASHED. So it is INTERMITTENT -- a
  Heisenbug, the hallmark of the use-after-free.
- The instrumented build (PADBG PA-free watch + WatchedAllocationPolicy on the
  string table) perturbs the heap enough to sometimes dodge the UAF, sometimes not.
- Crucially, the re-run crash did NOT trip PADBG (which watches only the string-
  table hashmap backing) and instead hit base::Vector::length()'s
  CHECK_GE(INT_MAX, length_) (BUS_ADRALN 0 via OS::Abort). So the corrupted victim
  VARIES (hashmap entry vs Vector length) -- it is a GENERAL use-after-free: a stale
  pointer frees some live V8/base allocation, its slot gets the PA freelist {0,~0},
  and whatever later reads that slot crashes. Not one structure -> watching one
  structure (as PADBG did) cannot catch it.

So a narrow/principled fix (e.g., zone-backing just the string table) would not fix
the general UAF (the Vector victim proves other allocations are hit too). A CERTAIN
fix needs to catch the erroneous free of a live allocation, which requires
ASAN-class tooling (quarantine + redzones + shadow): ASAN has no Haiku runtime
(clang bundles Linux targets only), and the Haiku-native alternatives (build with
use_partition_alloc_as_malloc=false + Haiku guarded/quarantine malloc, or a
PartitionAlloc quarantine/*Scan build) are heavy rebuilds with memory-blowup risk
for a full browser and may themselves perturb the layout. JS itself is correct on
arm64 (6*7=42, sum(i^2,0..999)=332833500 rendered), so this is NOT the x86 session's
link-corruption bug -- that was fixed separately by byte-restoring the V8 embedded
blob; arm64's is a real heap UAF.

State: root cause DEFINITIVELY diagnosed (general renderer-side UAF, freelist {0,~0}
proof + Heisenbug). Native toolbar works; light pages render reliably; heavy pages
like naver render sometimes and crash sometimes. A certain fix is gated on getting
ASAN-class UAF tooling onto Haiku arm64 (a real porting effort), or landing an
upstream V8 lifetime fix once the culprit is identified with such tooling.

### naver crash SOLVED -- real cause is software-compositing wiring, NOT UAF/blob (2026-09-14, corrected)

Every earlier naver diagnosis in this file (V8 background-parser UAF; PartitionAlloc
freelist {0,~0} use-after-free; base::Vector length corruption; "general Heisenbug
UAF"; and the peer's embedded-blob load/link-corruption hypothesis) was WRONG. On-device
testing with an instrumented build settled it definitively.

Method:
- Built content_shell with the V8 embedded-blob integrity check forced ON in release
  (isolate.cc SetEmbeddedBlob, normally #ifdef DEBUG): recomputes the in-memory blob
  hash at every isolate init, FATALs on mismatch, prints "RCHROMIUM blobverify PASS
  data=.. code=.." on match. Also ran with --js-flags=--verify-snapshot-checksum.
- Deployed via bfs_shell into csdata.img, ran on the real renku-arm64 desktop VM,
  drove the guest Terminal over QMP (mouse via usb-tablet abs pointer + send-key;
  screendump for output). Guest paths: csdata mounts at /csdata; symlink
  /boot/home/cs -> /csdata reproduces the loader's expected layout. content_shell +
  resources live at /csdata (== /boot/home/cs).

Findings:
- blobverify PASSED in every process, every run (data=8469670f code=6a9f10cd, identical
  to build-time), and --verify-snapshot-checksum raised no CHECK. So BOTH the embedded
  builtins blob and the startup snapshot are byte-perfect in memory at runtime. That
  kills the load/link-corruption hypothesis outright (a FATAL would fire in any process
  whose blob was wrong; none did). readelf confirms the blob segments are offset==vaddr
  (no skew), so the gcc-64KB loader bug that hit the shim cannot touch them.
- The actual crash, caught deterministically:
    ERROR ui/gl/init/gl_factory.cc:87  List of allowed GL implementations is empty.
    ERROR viz_main_impl.cc:190         Exiting GPU process due to errors during init
    FATAL components/viz/service/display_embedder/output_surface_provider_impl.cc:167
          Check failed: surface_ozone.
  The stack-scan symbols (fontations_ffi$..BridgeFontRef, _v8_internal_Node_Print) are
  nearest-export noise, not the real frames. This is a DETERMINISTIC CHECK, not a UAF:
  same faulting offset (image + 0xaee8000) every crash run.

Root cause (in the port's own code):
- Haiku has no GL, so viz falls back to software compositing. The software output
  device for OZONE is built by HaikuSurfaceFactory::CreateCanvasForWidget
  (ui/ozone/platform/haiku/haiku_surface_factory.cc), which returns nullptr when the
  factory has no window manager -- and OzonePlatformHaiku::InitializeGPU() builds the
  GPU-process factory with HaikuSurfaceFactory(nullptr) on purpose (windows live in the
  browser process). In the DEFAULT multi-process model the display compositor runs in a
  SEPARATE gpu/viz process, so window_manager_ is null there -> CreateCanvasForWidget
  null -> CHECK(surface_ozone) FATAL. The source comment already says "Chrome asks for
  viz to run in-process on this platform", but nothing actually enforces it
  (ozone_platform_haiku.cc has no GetPlatformProperties override, no in-process forcing).
- --in-process-gpu alone moves viz into the browser process (window_manager_ now
  present, surface_ozone PASSES) but then in-process viz tries to create a GL context
  and hits gl_factory_ozone.cc:62 NOTREACHED ("Expected Mock or Stub, actual:0" ==
  kGLImplementationNone unhandled).
- --disable-gpu alone leaves viz out-of-process -> surface_ozone again.

FIX (verified on device, renders news.naver.com fully, reproduced 2x back-to-back,
zero crashes; contrast: 100% crash/hang without):
    content_shell --in-process-gpu --disable-gpu https://news.naver.com
  --in-process-gpu puts the display compositor in the browser process (the process that
  owns the BWindow / HaikuWindowManager) and --disable-gpu forces software compositing
  so no GL context is ever created. The earlier "render / hang / crash" variance
  ("Heisenbug") was just which way the same broken software-compositing path fell over
  on a given run; forcing the correct path makes naver deterministic.

Permanent fix DONE (baked in; no flags needed): port/port-content-shell-ui.py now
also patches content/shell/app/shell_main_delegate.cc BasicStartupComplete to append
switches::kInProcessGPU + switches::kDisableGpu when BUILDFLAG(IS_HAIKU). Rebuilt
content_shell, redeployed to csdata, and confirmed on the renku-arm64 desktop that
`./content_shell https://news.naver.com` (NO flags) renders news.naver.com fully
(broadcaster logos + LIVE video thumbnail images loaded), zero crashes. The x86
session's link-corruption fix is unrelated; arm64 never had a blob problem.

## Multi-process again (2026-10-25)

The launchers passed `--single-process` because, on the 2026-09-13/14
builds, frames from out-of-process renderers never reached the window
("Launcher flags" above). That was never retested after the fd-0 fix
(09-15: `res_nclose()` closing whatever descriptor was 0) and the wake-up
socket fix (09-20), either of which can break a renderer's IPC.

Retested with the published x86_64 build (v154.0.8036.0-13, unchanged) on
Haiku R1/beta6 hrev59866+92 in VirtualBox, 4 GB, launched with the usual
flags minus `--single-process`, `ulimit -n 8192`:

- example.com, news.naver.com (live video included), x.com and Wikipedia
  render in the window. A browser, renderers and utility processes
  (network, storage) run as separate teams.
- x.com, three fresh runs each way, 90 s apiece: no crash multi-process or
  single-process.
- Two earlier multi-process runs lost an x.com renderer in
  `base::TerminateBecauseOutOfMemory` (the crash site, disassembled from
  the stripped binary, is `OnNoMemoryInternal`). The first had naver,
  YouTube and Wikipedia open in other windows of a 4 GB machine; neither
  reproduced afterwards. The page itself stayed up, but debug_server's
  alert held each crashed renderer suspended, and the relaunched one
  crashed into the next alert.

So the launchers drop `--single-process`, and:

- They set `ulimit -n 8192` (the web-app launcher and install.py did not):
  Haiku's default is 256 descriptors, and multi-process passes shared
  memory and IPC channels as descriptors.
- Child processes call `disable_debugger(1)` (port-content-shell-ui.py), so
  a crashed renderer dies and shows as crashed instead of waiting behind an
  alert. `RCH_CHILD_DEBUGGER=1` keeps the alert for debugging.

arm64 was not retested.

## Renderers drop root (2026-10-25)

Haiku has no seccomp-bpf, namespaces or broker, so the launchers passed
`--no-sandbox`, and `RendererMainPlatformDelegate::EnableSandbox()` returned
false (which stops the renderer's run loop, so the flag was mandatory). Every
Haiku application runs as uid 0, but the kernel does check POSIX
credentials. Measured with a probe on R1/beta6 hrev59866, a team that drops
from root to uid/gid 65534:

| | |
|---|---|
| get root back (`setuid(0)`) | denied |
| write or create files owned by the user (uid 0, 0644/0755) | denied |
| signal or kill a uid-0 team | denied |
| list or clone a uid-0 team's areas | denied |
| read world-readable files (most of the disk) | allowed |
| list a uid-0 team's ports and `write_port()` to them | allowed |
| open an internet socket, fork | allowed |
| create Chromium-style shared memory in `/tmp` (drwxrwxrwx) | works |

Credentials are per team: a thread created before the drop loses root too.

So `EnableSandbox()` now drops to uid/gid 65534 (setgroups, setgid, setuid,
then checks that root cannot be regained; any failure is fatal), and the
launchers no longer pass `--no-sandbox`. A renderer started by a non-root
user keeps that user's rights and says so in the log.

This is a mitigation, not a sandbox. Port writes are the hole that matters:
a compromised renderer can send BMessages to Tracker or the registrar and
have them launch or delete anything. Closing it needs a per-team
restriction in the kernel (no ports outside an allowlist, no new sockets,
limited file access), which neither Haiku nor Renku has; that is the next
step, for Renku.

## PR #1 on arm64 (2026-10-28)

Built in `out/haiku-arm64-nopa` on the M4 and run on the RENKU arm64 VM from a
BFS copy, with install.py's flags (no `--single-process`, no `--no-sandbox`):
news.naver.com renders multi-process; two renderers run as uid/gid 65534,
utility processes as root; a test MV3 extension shows its badge and popup in
the toolbar and its declarativeNetRequest rule blocks example.org
(`ERR_BLOCKED_BY_CLIENT`); Arabic, Persian, CJK and colour emoji (skin tones,
ZWJ, flags, keycaps) render from the bundled fonts.

Two things broke on a tree that was already ported, both fixed in the merge:

- `haiku-port.diff` applied with `--forward` added ui/gfx's full Haiku block
  next to the older one, and gn stopped on duplicate objects.
  `port/port-gfx-sources.py` keeps one.
- The reconstructed `port/files/build/` lost `-z max-page-size=4096`. lld then
  aligns arm64 segments to 64K and the runtime_loader refuses the binary with
  "Could not map image: Bad data". Check with `llvm-readelf -lW`: every LOAD
  but the text one should have Align 0x1000.
