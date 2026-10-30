#!/bin/bash
#
# Apply the Haiku port to a Chromium checkout.
#
# Step 4 of the arm64 work. Chromium has no Haiku support of any kind: of
# 505,289 files in the tree, 13 mention Haiku and every one of those is inside
# a vendored Rust crate. This is the scaffolding that has to exist before any
# of it can be built -- platform detection, a target OS, a toolchain.
#
# Re-runnable: each step checks whether it has already been done.
#
set -e
SRC="${1:-/haiku-build/chromium/src}"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

[ -d "$SRC/build/config" ] || {
	echo "not a Chromium checkout: $SRC" >&2
	exit 1
}

echo "Applying the Haiku port to $SRC"

# 1. Platform detection: turn __HAIKU__ into OS_HAIKU and BUILDFLAG(IS_HAIKU),
#    and put Haiku in the OS_POSIX set.
python3 "$HERE/port-build-config.py" "$SRC/build/build_config.h"

# 2. A target the build system recognises.
python3 "$HERE/port-buildconfig-gn.py" "$SRC/build/config/BUILDCONFIG.gn"

# 3. A Rust ABI triple. Chromium's Rust cannot be switched off -- skia's
#    bridge_rust_side is a hard dependency -- so the target has to have one.
python3 "$HERE/port-rust-gni.py" "$SRC"

# 4. Source fixes: platform lists, POSIX facilities Haiku spells differently,
#    the message pump, crubit. Order matters only in that port-rust-gni.py
#    must precede port-crubit.py -- both edit build/config/rust.gni.
python3 "$HERE/port-feature-flags.py" "$SRC"
python3 "$HERE/port-base-posix.py" "$SRC"
python3 "$HERE/port-message-pump.py" "$SRC"
python3 "$HERE/port-perfetto-os-lists.py" "$SRC"
# These were written but never wired in here, so a clean checkout came out
# missing every edit they carry -- //content, //net, //v8 and the libc crate's
# Haiku arm64 source list.
python3 "$HERE/port-content.py" "$SRC"
python3 "$HERE/port-net.py" "$SRC"
python3 "$HERE/port-v8.py" "$SRC"
python3 "$HERE/port-gpu.py" "$SRC"
python3 "$HERE/port-rust-libc.py" "$SRC"
python3 "$HERE/port-crubit.py" "$SRC"
python3 "$HERE/port-content-shell-ui.py" "$SRC"
python3 "$HERE/port-content-shell-build.py" "$SRC"

# 5. The toolchain and config files, which have no upstream counterpart to
#    patch and are simply added.
# A HidService. Without one, HidManagerImpl's constructor observes a null
# pointer and the browser dies the moment a page asks for HID -- which x.com's
# sign-in page does, because a passkey is a HID device.
python3 "$HERE/port-hid-haiku.py" "$SRC"

# Cookies and an HTTP cache on disk. content_shell leaves the network context
# in memory, so a sign-in lasts exactly as long as the process.
python3 "$HERE/port-shell-profile.py" "$SRC"

# slack:, mailto: and other schemes the browser does not serve go to the
# application Haiku has for them, through /bin/open.
python3 "$HERE/port-shell-external-protocol.py" "$SRC"

for f in $(cd "$HERE/files" && find . -type f); do
	mkdir -p "$SRC/$(dirname "${f#./}")"
	cp "$HERE/files/${f#./}" "$SRC/${f#./}"
	echo "  added ${f#./}"
done

# The gn platform lists. These were fixed by autofix.py/fixdeps.py in a
# fix-and-repeat loop and never captured in a script; haiku-port.diff holds
# them. Hunks the scripts above already applied are skipped (--forward), so
# do this last. Three hunks fail as already applied in a different form
# (signin, screen_ai, unowned_user_data). Verified on a clean 154.0.8036.0
# checkout, x64, 2026-10-25: gn gen succeeds after this.
# Its build/rust/std/rules/BUILD.gn edit is left out: it matches the patched
# arm64 rustc's std sources (rust/install-rust-toolchain.sh syncs those), and
# breaks the stock rustc that x64 builds with.
awk '/^diff --git /{skip = ($3 == "a/build/rust/std/rules/BUILD.gn")} !skip' \
	"$HERE/haiku-port.diff" |
	patch -d "$SRC" -p1 --forward --no-backup-if-mismatch -r - > /dev/null || true
# On a tree ported before the diff carried ui/gfx's block, the above adds a
# second one next to the old.
python3 "$HERE/port-gfx-sources.py" "$SRC"

# 6. Chromium's extension system in content_shell (docs/EXTENSIONS.md). The
#    embedder came in with files/ above; the schemas of the //chrome APIs it
#    implements are taken from this checkout, and port-shell-extensions.py
#    wires it into content_shell. After port-content-shell-ui.py, whose
#    BUILD.gn edit it anchors next to.
for s in action cookies extension tab_groups tabs web_navigation windows; do
	cp "$SRC/chrome/common/extensions/api/$s.json" \
		"$SRC/content/shell/common/extensions/api/"
done
python3 "$HERE/port-shell-extensions.py" "$SRC"

cat <<'USAGE'

Done. Configure with:

  gn gen out/haiku-arm64 --args='
      target_os="haiku"
      target_cpu="arm64"
      is_debug=false
      symbol_level=0
      haiku_sysroot="/root/pybuild/sysroot"
      use_partition_alloc_as_malloc=false
      enable_backup_ref_ptr_support=false
      use_allocator_shim=false
  '

The three allocator flags are not optional. With PartitionAlloc as malloc,
x.com kills the renderer in V8's AstValueFactory about seven runs in ten --
measured twice, ten runs each, interleaved against a build with these flags in
the same boot, which came out 0 of 10 both times. See AGENTS.md, "The
allocator under V8 is PartitionAlloc". They move together: allocator.gni
asserts BackupRefPtr needs PartitionAlloc-Everywhere, and without
PartitionAlloc the shim's only non-PA dispatch is the glibc one, which calls
__libc_malloc and does not link on Haiku.
USAGE
