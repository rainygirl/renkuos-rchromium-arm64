#!/bin/sh
#
# Build libchromium_haiku.so on a Haiku x86_64 machine reached over ssh, with
# its own gcc (the one that built libbe; see haiku_shim.h), and put it in the
# sysroot, where the ozone source_set's `libs = [ "chromium_haiku" ]` finds it.
# For hosts without Haiku's x86_64 cross gcc; build-shim.sh is the cross
# version.
#
#   build-shim-vm.sh <ssh-host> [sysroot]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
HOST=${1:?ssh host of a Haiku x86_64 machine with gcc and haiku_devel}
SYSROOT=${2:-$HOME/haiku-sysroot-x64}
OUT=${OUT:-$HERE/out}

mkdir -p "$OUT"
ssh "$HOST" 'rm -rf /tmp/rch-shim && mkdir -p /tmp/rch-shim'
scp -q "$HERE/haiku_shim.cc" "$HERE/haiku_shim.h" "$HOST:/tmp/rch-shim/"
ssh "$HOST" 'cd /tmp/rch-shim &&
	g++ -O2 -fPIC -c haiku_shim.cc -o haiku_shim.o &&
	g++ -shared haiku_shim.o -Wl,-soname,libchromium_haiku.so \
		-Wl,--hash-style=both -lbe -lstdc++ -lroot -o libchromium_haiku.so'
scp -q "$HOST:/tmp/rch-shim/libchromium_haiku.so" "$OUT/"

cp "$OUT/libchromium_haiku.so" "$SYSROOT/boot/system/lib/"
ln -sf ../../lib/libchromium_haiku.so "$SYSROOT/boot/system/develop/lib/libchromium_haiku.so"
echo "built $OUT/libchromium_haiku.so"
