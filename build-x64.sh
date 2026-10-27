#!/bin/bash
# Build content_shell for Haiku x86_64 on an x86_64 Linux host. Adapted from
# rainygirl/renkuos-rchromium-x86_64's build-x64.sh, which cross-builds from
# arm64 Linux; here the V8 snapshot tools run natively, so no Rosetta and no
# multiarch libc. Re-runnable.
#
#   SRC=~/chromium-haiku/src SYSROOT=~/haiku-sysroot-x64 ./build-x64.sh
#
# Inputs, prepared once:
#   $SRC      a Chromium 154.0.8036.0 checkout (gclient sync done)
#   $SYSROOT  boot/system/{lib,develop} of a Haiku x86_64 system with
#             haiku_devel, e.g. from R1/beta6:
#               ssh <haiku> 'cd / && tar cf - boot/system/develop/headers \
#                 boot/system/develop/lib boot/system/lib \
#                 boot/system/develop/tools/lib/gcc' | tar xf -
#             plus libchromium_haiku.so (haiku_shim/build-shim-vm.sh).
#
# Env: SKIP_PORT=1 skips re-applying the port; JOBS (default 12); KEEP
# (ninja -k, default 0 = keep going past failures).
set -e
export PATH=$HOME/depot_tools:$PATH
export DEPOT_TOOLS_METRICS=0
R=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=${SRC:-$HOME/chromium-haiku/src}
SYSROOT=${SYSROOT:-$HOME/haiku-sysroot-x64}
OUTDIR=out/haiku-x64
GCCLIB=$(ls -d "$SYSROOT"/boot/system/develop/tools/lib/gcc/x86_64-unknown-haiku/* | head -1)

step() { printf '\n########## %s\n' "$*"; }

[ -e "$SYSROOT/boot/system/lib/libchromium_haiku.so" ] || {
	echo "no libchromium_haiku.so in $SYSROOT: run haiku_shim/build-shim-vm.sh" >&2
	exit 1
}

if [ -z "$SKIP_PORT" ]; then
	step "port"
	bash "$R/port/apply-haiku-port.sh" "$SRC"
	python3 "$R/port/port-ozone.py" "$SRC"
	python3 "$R/port/port-x64.py" "$SRC"
fi

step "gn gen"
cd "$SRC"
mkdir -p $OUTDIR
cat > $OUTDIR/args.gn <<EOF
target_os = "haiku"
target_cpu = "x64"
is_debug = false
symbol_level = 0
dcheck_always_on = false
haiku_sysroot = "$SYSROOT"
haiku_gcc_lib_dir = "$GCCLIB"

# Haiku's own malloc, not PartitionAlloc (arm64 AGENTS.md, "The allocator
# under V8 is PartitionAlloc").
use_partition_alloc_as_malloc = false
enable_backup_ref_ptr_support = false
use_allocator_shim = false

# The context snapshot generator would link all of Blink a second time for
# the host. Without it Blink sets up each context at run time instead.
use_v8_context_snapshot = false
EOF
gn gen $OUTDIR

step "ninja"
ninja -C $OUTDIR -j "${JOBS:-12}" -k "${KEEP:-0}" content_shell

step "dist"
# What packaging/x86_64 expects, bundled fonts included.
DIST=${DIST:-$R/out/dist-x64}/RChromium
STRIP=third_party/llvm-build/Release+Asserts/bin/llvm-strip
rm -rf "$DIST"
mkdir -p "$DIST/lib" "$DIST/locales"
$STRIP -o "$DIST/content_shell" $OUTDIR/content_shell
[ -e $OUTDIR/libtest_trace_processor.so ] &&
	$STRIP -o "$DIST/lib/libtest_trace_processor.so" $OUTDIR/libtest_trace_processor.so
cp $OUTDIR/content_shell.pak $OUTDIR/icudtl.dat $OUTDIR/snapshot_blob.bin "$DIST/"
cp $OUTDIR/ui/en-US.pak "$DIST/locales/" 2>/dev/null ||
	cp $OUTDIR/locales/en-US.pak "$DIST/locales/"
cp "$SYSROOT/boot/system/lib/libchromium_haiku.so" "$DIST/lib/"
sh "$R/packaging/fetch-fonts.sh" "$DIST/fonts"
echo "BUILD-DONE $DIST"
