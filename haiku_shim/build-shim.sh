#!/bin/sh
#
# Build libchromium_haiku.so for Haiku x86_64 with the gcc that built libbe
# (see haiku_shim.h for why it cannot be Chromium's clang), and put it in the
# sysroot, where the ozone source_set's `libs = [ "chromium_haiku" ]` finds it.
#
#   build-shim.sh [sysroot] [gcc]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SYSROOT="${1:-/root/sysroot-x64}"
GXX="${2:-/root/gen-x64/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++}"
OUT="${OUT:-$HERE/out}"

mkdir -p "$OUT"
"$GXX" --sysroot="$SYSROOT" -O2 -fPIC -c "$HERE/haiku_shim.cc" -o "$OUT/haiku_shim.o"
"$GXX" --sysroot="$SYSROOT" -shared "$OUT/haiku_shim.o" \
	-Wl,-soname,libchromium_haiku.so -Wl,--hash-style=both \
	-L"$SYSROOT/boot/system/lib" -lbe -lstdc++ -lroot -o "$OUT/libchromium_haiku.so"

cp "$OUT/libchromium_haiku.so" "$SYSROOT/boot/system/lib/"
ln -sf ../../lib/libchromium_haiku.so "$SYSROOT/boot/system/develop/lib/libchromium_haiku.so"
echo "built $OUT/libchromium_haiku.so"
