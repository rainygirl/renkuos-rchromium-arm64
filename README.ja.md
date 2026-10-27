# R Chromium -- arm64 (AArch64)

[English](README.md) | **日本語** | [한국어](README.ko.md)

Haiku/AArch64 (Apple Silicon やその他の arm64 マシン) でネイティブに動作する
Chromium です。戻る/進む/再読み込み、アドレス欄、ブックマークからなる BeAPI
ツールバー付きです。Noto Sans CJK を同梱しているので、日本語・韓国語・中国語の
ページが追加フォントなしで表示されます。RENKU arm64 イメージ (TLSDESC ローダーパッチを適用した
Haiku arm64) で動作し、素の Haiku arm64 イメージではロードできません。

![Haiku arm64 デスクトップで news.naver.com を描画する R Chromium](docs/images/screenshot-naver.png)

## pkgman でインストール

R Chromium は `pkgman.rainygirl.com` パッケージリポジトリで公開しています。
デバイスで Terminal を開いて入力します:

```sh
pkgman add-repo https://pkgman.rainygirl.com/arm64
pkgman add-repo https://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

**renku-arm64 のパッチセットで作った RENKU イメージ**では、修正がシステム
パッケージに入っている(`haiku_rchromium_fixes` を提供)ため、`arm64-system` は
追加しないでください。`pkgman install rchromium` だけで済み、再起動も不要です。
そこに `arm64-system` を追加すると、メディアスタックのないシステムパッケージに
置き換わります。

他の arm64 環境では、その後 **再起動** します。R Chromium には、RenkuOS nightly の arm64 イメージに
まだ入っていないカーネルとランタイムローダーの修正 2 つ (TLSDESC ローダーパッチと
`query-valid-pte` カーネルパッチ) が必要です。`arm64-system` リポジトリが両方を
含む `haiku` システムパッケージを公開しており、`pkgman install rchromium` が
依存関係として一緒に入れます。次回起動から有効になり、それまでは R Chromium が
`Troubles relocating: Bad data` で終了するか、カーネルがパニックします。

パッケージは約 150 MB なので、`/boot` にその分の空きが必要です。
`/boot/system/apps/RChromium/` にインストールされ、**Deskbar -> Applications**
に **R Chromium** が追加され、`rchromium` コマンドが使えるようになります。削除は
`pkgman uninstall rchromium` です。

`pkgman add-repo` が `Operation not supported` で失敗する場合、そのイメージは
ネットワークキットが TLS 対応なしでビルドされています (RenkuOS nightly の
minimum イメージがそうです)。このイメージは 1970-01-01 で起動するので、先に
時計を合わせてから HTTP のアドレスを使います:

```sh
date -u 091612002026        # MMDDhhmmYYYY、現在の UTC 時刻
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

インストール後に再起動します。このイメージは起動のたびに時計が 1970 に戻るので、
R Chromium を起動する前に時計を合わせてください。合わせないとすべての HTTPS
サイトが `ERR_CERT_DATE_INVALID` で失敗します。

`yes |` は、HTTPS の試行が失敗して残ったリポジトリ設定について pkgman が
「overwrite?」と尋ねるのに答えるためのものです。

## インストールスクリプトでインストール

`install.sh` は上のコマンドと同じことをします -- 時計が狂っていれば合わせ、
2 つのリポジトリを登録し (イメージに TLS がなければ HTTP に切り替え)、
`rchromium` をインストールし、システムパッケージが置き換わったら再起動を促します。
`sh`、`openssl`、`pkgman` だけで動くので minimum イメージでも使えます。
チェックアウトから:

```sh
sh install.sh
```

チェックアウトなしで使うには openssl で取得します (minimum イメージには curl、
wget、python3 がありません):

```sh
printf 'GET /rainygirl/renkuos-rchromium-arm64/main/install.sh HTTP/1.0\r\nHost: raw.githubusercontent.com\r\n\r\n' | openssl s_client -quiet -connect raw.githubusercontent.com:443 -servername raw.githubusercontent.com 2>/dev/null > /tmp/i.raw
{ while IFS= read -r l; do [ "$l" = $'\r' ] && break; done; cat; } < /tmp/i.raw > /tmp/install.sh
sh /tmp/install.sh
```

以前の tarball 方式は `sh install.sh --tarball` として残っています (python3 が
必要な `install.py` に委譲)。リリースを `~/config/non-packaged/apps/RChromium/`
に展開しますが、カーネルとローダーの修正は入りません。

## 実行

デスクトップの **R Chromium** をダブルクリックするか、**Deskbar -> Applications**
から選びます。Terminal からは:

```sh
rchromium https://news.naver.com
```

- **戻る / 進む / 再読み込み** -- 左側のアイコンボタン。ページ読み込み中は
  再読み込みが停止ボタンに変わります。
- **アドレス欄** -- URL を入力して Enter。`example.com` のように入力すると
  `https://example.com` を開きます。
- **星** -- 現在のページを日付グループ ("Today", ...) の下にブックマークします。
- **メニュー** -- 検索可能なブックマークウィンドウを開きます。項目を
  ダブルクリックすると開きます。

## アンインストール

```sh
pkgman uninstall rchromium
```

または `sh install.sh --uninstall`。パッチ済みの `haiku` システムパッケージは
そのまま残ります。元のパッケージの上位互換です。

## 拡張機能

Chrome 拡張 (Manifest V3) は、プロファイルの `Extensions` フォルダー
(`~/config/settings/content_shell/Extensions`) から読み込みます。展開した拡張
1 つがフォルダー 1 つです。`--load-extension=<dir>` で 1 回だけ読み込むこともできます。
action のある拡張はツールバーにボタンが付き、バッジとポップアップも出ます。Brave
Shields の広告ブロックをテストしました。動くものと省いたものは
[docs/EXTENSIONS.md](docs/EXTENSIONS.md) にあります。

ページはそれぞれのプロセスで描画され、そのプロセスは Web コンテンツを扱う前に root
権限を手放します ([docs/SANDBOX.md](docs/SANDBOX.md))。乗っ取られたページがユーザーの
ファイルを書き込むことはできませんが、完全なサンドボックスではありません。

## 貢献者

- [idumych](https://github.com/idumych) -- 拡張機能、レンダラーのサンドボックス、
  マルチプロセス、アラビア語とカラー絵文字のフォント、x86_64 Linux ホストでのビルド

## AI 利用について

このプログラムは Claude と一緒に作成しました。
