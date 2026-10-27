# R Chromium -- arm64 (AArch64)

**English** | [日本語](README.ja.md) | [한국어](README.ko.md)

Chromium running natively on Haiku/AArch64 -- Apple Silicon and other arm64
machines -- with a BeAPI toolbar: back/forward/reload, an address field, and
bookmarks. Noto Sans CJK, Noto Sans Arabic and Noto Color Emoji are bundled,
so Korean, Japanese, Chinese and Arabic pages, and colour emoji, render
without extra fonts. It runs on the RENKU arm64 image (Haiku arm64 with the TLSDESC
loader patch); stock Haiku arm64 images cannot load it.

![R Chromium rendering news.naver.com on the Haiku arm64 desktop](docs/images/screenshot-naver.png)

## Install with pkgman

R Chromium is published in the `pkgman.rainygirl.com` package repository.
Open Terminal on the device:

```sh
pkgman add-repo https://pkgman.rainygirl.com/arm64
pkgman add-repo https://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

On a **RENKU image built with the renku-arm64 patch set** the system package
already carries the fixes (it provides `haiku_rchromium_fixes`), so skip
`arm64-system`: `pkgman install rchromium` is all it takes, with no reboot.
Adding `arm64-system` there would replace the system package with one that has
no media stack.

On other arm64 systems, then **reboot**. R Chromium needs two fixes in the kernel and the runtime
loader that the RenkuOS nightly arm64 image does not carry yet (the TLSDESC
loader patch and the `query-valid-pte` kernel patch). The `arm64-system`
repository publishes a `haiku` system package with both; `pkgman install
rchromium` pulls it in as a dependency, and it takes effect on the next boot.
Until then R Chromium exits with `Troubles relocating: Bad data` or panics the
kernel.

The package is about 150 MB; keep that much free on `/boot`. It installs into
`/boot/system/apps/RChromium/`, adds **R Chromium** to
**Deskbar -> Applications**, and adds an `rchromium` command. Remove it with
`pkgman uninstall rchromium`.

If `pkgman add-repo` fails with `Operation not supported`, the image was built
without TLS support in its network kit (the RenkuOS nightly minimum image is).
Set the clock first -- that image boots at 1970-01-01 -- and use the HTTP
address instead:

```sh
date -u 091612002026        # MMDDhhmmYYYY, the current UTC time
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

and reboot afterwards. The clock is lost again on every boot of that image;
set it before starting R Chromium or every HTTPS site fails with
`ERR_CERT_DATE_INVALID`.

`yes |` answers the "overwrite?" question pkgman asks when a failed HTTPS
attempt left a repository entry behind.

## Install with the installer script

`install.sh` does the same as the commands above -- sets the clock if it is
wrong, registers both repositories (falling back to HTTP when the image has no
TLS), installs `rchromium` and tells you to reboot when the system package was
replaced. It needs nothing but `sh`, `openssl` and `pkgman`, so it also works
on the minimum image. From a checkout:

```sh
sh install.sh
```

Without a checkout, fetch it with openssl (the minimum image has no curl, wget
or python3):

```sh
printf 'GET /rainygirl/renkuos-rchromium-arm64/main/install.sh HTTP/1.0\r\nHost: raw.githubusercontent.com\r\n\r\n' | openssl s_client -quiet -connect raw.githubusercontent.com:443 -servername raw.githubusercontent.com 2>/dev/null > /tmp/i.raw
{ while IFS= read -r l; do [ "$l" = $'\r' ] && break; done; cat; } < /tmp/i.raw > /tmp/install.sh
sh /tmp/install.sh
```

The old tarball path still exists as `sh install.sh --tarball` (delegates to
`install.py`, which needs python3); it unpacks the release into
`~/config/non-packaged/apps/RChromium/` without the kernel and loader fixes.

## Run

Double-click **R Chromium** on the Desktop, or pick it from
**Deskbar -> Applications**. From Terminal:

```sh
rchromium https://news.naver.com
```

- **Back / Forward / Reload** -- icon buttons on the left; Reload turns into Stop
  while a page is loading.
- **Address field** -- type a URL and press Enter; a bare `example.com` opens
  `https://example.com`.
- **Star** -- bookmarks the current page under a date group ("Today", ...).
- **Menu** -- opens a searchable bookmarks window; double-click an entry to
  open it.

## Uninstall

```sh
pkgman uninstall rchromium
```

or `sh install.sh --uninstall`. The patched `haiku` system package stays; it
is a superset of the stock one.

## Extensions

Chrome extensions (Manifest V3) load from the `Extensions` folder of the
profile, `~/config/settings/content_shell/Extensions`, one unpacked extension
per folder; `--load-extension=<dir>` loads one for a single run. Each one with
an action gets a button in the toolbar, with its badge and popup. Brave
Shields' ad blocking has been tested; see [docs/EXTENSIONS.md](docs/EXTENSIONS.md)
for what works and what is left out.

Pages render in their own processes, and those drop root before they touch
web content ([docs/SANDBOX.md](docs/SANDBOX.md)). That keeps a compromised
page from writing your files, but it is not a full sandbox.

## Contributors

- [idumych](https://github.com/idumych) -- extensions, the renderer sandbox,
  multi-process, Arabic and colour emoji fonts, the x86_64 Linux host build

## AI disclosure

This program was written with Claude.
