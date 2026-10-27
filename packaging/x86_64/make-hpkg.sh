#!/bin/sh
#
# Package a deployable content_shell directory as rchromium-<ver>-x86_64.hpkg.
# Runs on Haiku x86_64 itself: `package`, `addattr` and `mimeset` are Haiku
# tools, and the icon and signature have to be attributes, which only BFS
# keeps.
#
# Usage: packaging/make-hpkg.sh <dist-dir> [out-dir]
#
# <dist-dir> holds what build-x64.sh's dist step collects: content_shell,
# content_shell.pak, icudtl.dat, snapshot_blob.bin, locales/, lib/ and fonts/.
set -e
DIST=${1:?dist dir}
OUT=${2:-.}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=154.0.8036.0
REV=1

for f in content_shell content_shell.pak icudtl.dat snapshot_blob.bin locales \
         lib/libchromium_haiku.so fonts/Noto-COLRv1.ttf; do
	[ -e "$DIST/$f" ] || { echo "missing $DIST/$f" >&2; exit 1; }
done

STAGE=$(mktemp -d /tmp/rchromium-hpkg.XXXXXX)
APP=$STAGE/apps/RChromium
mkdir -p "$APP" "$STAGE/bin" "$STAGE/data/deskbar/menu/Applications"
cp -R "$DIST"/. "$APP/"
rm -f "$APP"/*.log
cp "$HERE/assets/rchromium.hvif" "$APP/"

# The launcher. content_shell resolves its resources next to itself, and %A/lib
# is where libchromium_haiku.so and libtest_trace_processor.so are.
cat > "$APP/R Chromium" <<'EOF'
#!/bin/sh
self="$(readlink -f "$0" 2>/dev/null || echo "$0")"
cd "$(dirname "$self")" || exit 1
# An ssh session starts with LIBRARY_PATH unset, which would hide /boot/system/lib.
LIBRARY_PATH="$PWD/lib:${LIBRARY_PATH:-/boot/home/config/non-packaged/lib:/boot/home/config/lib:/boot/system/non-packaged/lib:/boot/system/lib}"
export LIBRARY_PATH
ulimit -n 8192 2>/dev/null
# No video capture backend on Haiku: the fake device keeps pages that list
# media devices from dereferencing a missing factory.
exec ./content_shell --ozone-platform=haiku \
	--disable-gpu --in-process-gpu --disable-gpu-compositing \
	--use-fake-device-for-media-stream "$@"
EOF
chmod 755 "$APP/R Chromium" "$APP/content_shell"
chmod -R a+rX "$APP"

# Deskbar and Tracker read the icon and signature from attributes.
addattr -f "$APP/rchromium.hvif" -c VICN BEOS:ICON "$APP/R Chromium"
addattr -t mime BEOS:TYPE text/x-shellscript "$APP/R Chromium"
addattr -t mime BEOS:APP_SIG application/x-vnd.rainygirl-RChromium "$APP/R Chromium"
addattr -f "$APP/rchromium.hvif" -c VICN BEOS:ICON "$APP/content_shell"

ln -s "../apps/RChromium/R Chromium" "$STAGE/bin/rchromium"
ln -s "../../../../apps/RChromium/R Chromium" \
	"$STAGE/data/deskbar/menu/Applications/R Chromium"

sed "s/@VERSION@/$VERSION-$REV/g" "$HERE/packaging/x86_64/rchromium.PackageInfo" \
	> "$STAGE/.PackageInfo"

mkdir -p "$OUT"
PKG="$OUT/rchromium-$VERSION-$REV-x86_64.hpkg"
rm -f "$PKG"
package create -C "$STAGE" "$PKG"
rm -rf "$STAGE"
ls -la "$PKG"
