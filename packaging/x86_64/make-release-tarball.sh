#!/bin/sh
#
# Packs a deployable content_shell directory into the release tarball that
# a Haiku x86_64 host turns into an hpkg (make-hpkg.sh). Run on the build host (macOS/Linux).
#
# Usage: packaging/make-release-tarball.sh <dir-with-content_shell> [out-dir]
#
# <dir> must hold: content_shell, content_shell.pak, icudtl.dat,
# snapshot_blob.bin, locales/, and
# lib/libchromium_haiku.so + lib/libtest_trace_processor.so (both DT_NEEDED),
# and fonts/ as packaging/fetch-fonts.sh fills it (CJK, Arabic and colour
# emoji with their licences; Chromium's Haiku font manager scans <app>/fonts
# first).
# The app icon (assets/rchromium.hvif) is added automatically.
set -e
SRC=${1:?source dir}
OUT=${2:-.}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
NAME=rchromium-x86_64
for f in content_shell content_shell.pak icudtl.dat snapshot_blob.bin \
         locales lib/libchromium_haiku.so \
         lib/libtest_trace_processor.so fonts/NotoSansCJK-Regular.ttc \
         fonts/NotoSansCJK-Bold.ttc fonts/LICENSE-NotoSansCJK.txt \
         fonts/NotoSansArabic-Regular.ttf fonts/NotoSansArabic-Bold.ttf \
         fonts/LICENSE-NotoSansArabic.txt fonts/Noto-COLRv1.ttf \
         fonts/LICENSE-NotoColorEmoji.txt; do
	[ -e "$SRC/$f" ] || { echo "missing $SRC/$f" >&2; exit 1; }
done
STAGE=$(mktemp -d)
mkdir -p "$STAGE/RChromium"
cp -R "$SRC"/. "$STAGE/RChromium/"
rm -f "$STAGE"/RChromium/*.log
cp "$HERE/assets/rchromium.hvif" "$STAGE/RChromium/"
mkdir -p "$OUT"
tar -C "$STAGE" -czf "$OUT/$NAME.tar.gz" RChromium
(cd "$OUT" && shasum -a 256 "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
rm -rf "$STAGE"
ls -la "$OUT/$NAME.tar.gz" "$OUT/$NAME.tar.gz.sha256"
