# R Chromium -- arm64 (AArch64)

[English](README.md) | [日本語](README.ja.md) | **한국어**

Haiku/AArch64(Apple Silicon 및 기타 arm64 기기)에서 네이티브로 동작하는
Chromium입니다. 뒤로/앞으로/새로고침, 주소 입력란, 북마크로 이루어진 BeAPI
툴바가 붙어 있습니다. Noto Sans CJK를 내장해 한국어, 일본어, 중국어 페이지가
추가 폰트 없이 표시됩니다. RENKU arm64 이미지(TLSDESC 로더 패치가 적용된 Haiku
arm64)에서 동작하며, 순정 Haiku arm64 이미지에서는 로드되지 않습니다.

![Haiku arm64 데스크톱에서 news.naver.com을 렌더링하는 R Chromium](docs/images/screenshot-naver.png)

## pkgman으로 설치

R Chromium은 `pkgman.rainygirl.com` 패키지 저장소에 올라가 있습니다. 기기에서
Terminal을 열고 입력합니다:

```sh
pkgman add-repo https://pkgman.rainygirl.com/arm64
pkgman add-repo https://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

**renku-arm64 패치 세트로 만든 RENKU 이미지**에는 이 수정이 시스템 패키지에 이미
들어 있으므로(`haiku_rchromium_fixes`를 제공) `arm64-system`은 추가하지 마십시오.
`pkgman install rchromium`만 하면 되고 재부팅도 필요 없습니다. 그 이미지에
`arm64-system`을 추가하면 미디어 스택이 없는 시스템 패키지로 바뀝니다.

다른 arm64 시스템에서는 그다음 **재부팅**합니다. R Chromium은 RenkuOS nightly arm64 이미지에 아직 없는
커널·런타임 로더 수정 두 가지(TLSDESC 로더 패치, `query-valid-pte` 커널 패치)가
필요합니다. `arm64-system` 저장소가 두 수정을 넣은 `haiku` 시스템 패키지를
제공하며, `pkgman install rchromium`이 의존성으로 함께 설치합니다. 다음 부팅부터
적용되고, 그전에는 R Chromium이 `Troubles relocating: Bad data`로 종료되거나
커널이 패닉합니다.

패키지는 약 150 MB이니 `/boot`에 그만큼 여유 공간이 있어야 합니다.
`/boot/system/apps/RChromium/`에 설치되고, **Deskbar -> Applications**에
**R Chromium**이 추가되며 `rchromium` 명령이 생깁니다. 제거는
`pkgman uninstall rchromium`입니다.

`pkgman add-repo`가 `Operation not supported`로 실패하면, 그 이미지는 네트워크
킷에 TLS 지원 없이 빌드된 것입니다(RenkuOS nightly minimum 이미지가 그렇습니다).
이 이미지는 1970-01-01로 부팅하므로 먼저 시계를 맞추고 HTTP 주소를 사용합니다:

```sh
date -u 091612002026        # MMDDhhmmYYYY, 현재 UTC 시각
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64
yes | pkgman add-repo http://pkgman.rainygirl.com/arm64-system
pkgman install rchromium
```

설치 후 재부팅합니다. 이 이미지는 부팅할 때마다 시계가 다시 1970으로 돌아가므로,
R Chromium을 실행하기 전에 시계를 맞추세요. 안 맞추면 모든 HTTPS 사이트가
`ERR_CERT_DATE_INVALID`로 실패합니다.

`yes |`는 HTTPS 시도가 실패하며 남긴 저장소 설정 때문에 pkgman이 묻는
"overwrite?"에 답하기 위한 것입니다.

## 설치 스크립트로 설치

`install.sh`는 위 명령과 같은 일을 합니다 -- 시계가 틀리면 맞추고, 저장소 두 개를
등록하며(이미지에 TLS가 없으면 HTTP로 전환), `rchromium`을 설치하고, 시스템
패키지가 교체됐으면 재부팅하라고 알립니다. `sh`, `openssl`, `pkgman`만 있으면
되므로 minimum 이미지에서도 동작합니다. 체크아웃에서:

```sh
sh install.sh
```

체크아웃 없이 쓰려면 openssl로 받습니다(minimum 이미지에는 curl, wget, python3가
없습니다):

```sh
printf 'GET /rainygirl/renkuos-rchromium-arm64/main/install.sh HTTP/1.0\r\nHost: raw.githubusercontent.com\r\n\r\n' | openssl s_client -quiet -connect raw.githubusercontent.com:443 -servername raw.githubusercontent.com 2>/dev/null > /tmp/i.raw
{ while IFS= read -r l; do [ "$l" = $'\r' ] && break; done; cat; } < /tmp/i.raw > /tmp/install.sh
sh /tmp/install.sh
```

예전 tarball 방식은 `sh install.sh --tarball`로 남아 있습니다(python3가 필요한
`install.py`에 위임). 릴리스를 `~/config/non-packaged/apps/RChromium/`에 풀지만
커널·로더 수정은 넣지 않습니다.

## 실행

데스크톱의 **R Chromium**을 더블클릭하거나 **Deskbar -> Applications**에서
고릅니다. Terminal에서는:

```sh
rchromium https://news.naver.com
```

- **뒤로 / 앞으로 / 새로고침** -- 왼쪽의 아이콘 버튼. 페이지 로딩 중에는
  새로고침이 중지 버튼으로 바뀝니다.
- **주소 입력란** -- URL을 입력하고 Enter. `example.com`처럼 입력하면
  `https://example.com`을 엽니다.
- **별** -- 현재 페이지를 날짜 그룹("Today", ...) 아래에 북마크합니다.
- **메뉴** -- 검색 가능한 북마크 창을 엽니다. 항목을 더블클릭하면 열립니다.

## 제거

```sh
pkgman uninstall rchromium
```

또는 `sh install.sh --uninstall`. 패치된 `haiku` 시스템 패키지는 그대로
남습니다. 원래 패키지의 상위 호환입니다.

## 확장 기능

Chrome 확장(Manifest V3)은 프로필의 `Extensions` 폴더
(`~/config/settings/content_shell/Extensions`)에서 불러옵니다. 압축을 푼 확장
하나가 폴더 하나입니다. `--load-extension=<dir>`로 한 번만 불러올 수도 있습니다.
action이 있는 확장은 툴바에 버튼이 생기고, 배지와 팝업도 나옵니다. Brave Shields의
광고 차단을 테스트했고, 되는 것과 빠진 것은 [docs/EXTENSIONS.md](docs/EXTENSIONS.md)에
있습니다.

페이지는 각자의 프로세스에서 렌더링되고, 이 프로세스는 웹 콘텐츠를 다루기 전에 root
권한을 버립니다([docs/SANDBOX.md](docs/SANDBOX.md)). 그래서 공격당한 페이지가 사용자
파일을 쓸 수는 없지만, 완전한 샌드박스는 아닙니다.

## 기여자

- [idumych](https://github.com/idumych) -- 확장 기능, 렌더러 샌드박스, 멀티프로세스,
  아랍어·컬러 이모지 폰트, x86_64 Linux 호스트 빌드

## AI 활용 고지

이 프로그램은 Claude와 함께 작업해 만들었습니다.
