#!/bin/sh
#
# Puts the bundled fonts into <dest> (normally <dist>/fonts). Chromium's
# Haiku font manager (skia/ext/font_utils.cc) scans <app>/fonts before the
# system font directories, so these fill what a stock Haiku/RENKU install
# lacks:
#
#   NotoSansCJK-{Regular,Bold}.ttc   Chinese, Japanese, Korean (noto-cjk Sans2.004)
#   NotoSansArabic-{Regular,Bold}.ttf  Arabic, Persian, Urdu (notofonts/arabic v2.013)
#   Noto-COLRv1.ttf                  colour emoji (noto-emoji v2.051), drawn by
#                                    Fontations: FreeType here has no COLR support
#
# All three are SIL Open Font License 1.1; each licence goes next to its fonts.
# Downloads are pinned to tags and checked against SHA-256 sums, and cached
# in $FONT_CACHE (default ~/.cache/rchromium-fonts) so rebuilds stay offline.
#
# Usage: packaging/fetch-fonts.sh <dest>
set -e
DEST=${1:?destination dir}
CACHE=${FONT_CACHE:-$HOME/.cache/rchromium-fonts}
mkdir -p "$DEST" "$CACHE"

CJK=https://github.com/notofonts/noto-cjk/raw/Sans2.004/Sans/OTC
ARABIC=https://github.com/notofonts/arabic/releases/download/NotoSansArabic-v2.013/NotoSansArabic-v2.013.zip
EMOJI=https://github.com/googlefonts/noto-emoji/raw/v2.051/fonts

# fetch <url> <cache name> <sha256>
fetch() {
	if [ -f "$CACHE/$2" ] && echo "$3  $CACHE/$2" | sha256sum -c - >/dev/null 2>&1; then
		return
	fi
	curl -fsSL -o "$CACHE/$2.part" "$1"
	echo "$3  $CACHE/$2.part" | sha256sum -c - >/dev/null || {
		echo "checksum mismatch: $1" >&2
		rm -f "$CACHE/$2.part"
		exit 1
	}
	mv "$CACHE/$2.part" "$CACHE/$2"
}

fetch $CJK/NotoSansCJK-Regular.ttc NotoSansCJK-Regular.ttc \
	b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a
fetch $CJK/NotoSansCJK-Bold.ttc NotoSansCJK-Bold.ttc \
	faa5f3656a78b2e2d450d27fe8382c778bc2b6bb5ea29c986664a6a435056ceb
fetch https://github.com/notofonts/noto-cjk/raw/Sans2.004/LICENSE LICENSE-NotoSansCJK.txt \
	6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2
fetch $ARABIC NotoSansArabic-v2.013.zip \
	1301aceaea84c501cf2e6dcfb3182e2328c8eae5725817fcb239672bda7154f1
fetch $EMOJI/Noto-COLRv1.ttf Noto-COLRv1.ttf \
	0ae57fe58645638523ba35f388d93739d292539a9acb84df5700c81b1e1a28d2
fetch $EMOJI/LICENSE LICENSE-NotoColorEmoji.txt \
	6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2

cp "$CACHE"/NotoSansCJK-Regular.ttc "$CACHE"/NotoSansCJK-Bold.ttc \
	"$CACHE"/LICENSE-NotoSansCJK.txt "$CACHE"/Noto-COLRv1.ttf \
	"$CACHE"/LICENSE-NotoColorEmoji.txt "$DEST/"
unzip -o -q -j "$CACHE/NotoSansArabic-v2.013.zip" -d "$DEST" \
	NotoSansArabic/hinted/ttf/NotoSansArabic-Regular.ttf \
	NotoSansArabic/hinted/ttf/NotoSansArabic-Bold.ttf
unzip -o -q -p "$CACHE/NotoSansArabic-v2.013.zip" OFL.txt > "$DEST/LICENSE-NotoSansArabic.txt"
echo "fonts in $DEST"
