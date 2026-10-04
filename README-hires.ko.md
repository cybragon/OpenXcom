# OpenXcom Extended 8.7.1-hires-1 (비공식 고해상도 글자 포크)

[English](README-hires.en.md) · [변경 내역](CHANGELOG-hires.md)

> **비공식 포크입니다.** OpenXcom Extended(OXCE) 8.7.1을 바탕으로 고친 개인 빌드이며,
> OXCE/OpenXcom 공식 배포판이 아닙니다. 이 빌드의 문제는 **원작 팀(OXCE, OpenXcom)에 문의하지 말아 주세요.**

- 버전 이름: `8.7.1-hires-1` (게임 창 제목: `OpenXcom Extended 8.7.1-hires-1 (v2026-09-19)`. 괄호 안 날짜는 바탕이 된 OXCE 8.7.1의 날짜 그대로)
- 릴리스 태그(예정): `v8.7.1-hires-1`
- 바탕: OXCE 8.7.1 (커밋 `441cae1b0`)
- 라이선스: GNU GPL v3 이상 (`LICENSE.txt`)

## 무엇이 바뀌었나

### 1. 고해상도 TTF 글자 오버레이
게임은 원래 320x200 화면을 그린 뒤 확대합니다. 그래서 확대 배율이 클수록 글자의 도트가 커집니다.
이 포크는 글자만 Windows에 설치된 트루타입 글꼴(FreeType)로 **출력 해상도에 맞춰** 다시 그려 그 위에 얹습니다.
- OpenGL 출력과 소프트웨어 출력 모두에서 동작합니다.
- 줄바꿈, 정렬, 글자 칸 위치는 원래 비트맵 글꼴과 같게 유지합니다(화면 배치가 바뀌지 않음).
- 옵션 > "글꼴" 탭에서 켜고 끄고, 글꼴 종류별(큰 글꼴, 작은 글꼴, 지도 큰, 지도 작은)로 글꼴과 크기를 바꿀 수 있습니다. 바로 적용됩니다.
- 화면이 원본 크기(320x200 배율 1)보다 클 때만 의미가 있습니다.

### 2. 글자 크기 규칙
모든 글자(라틴, 숫자, 한글, 한자, 가나)를 **글꼴 종류마다 크기 하나**로 그립니다.
그 크기는 한글 **'각'**의 높이(본체)와 테두리가 아래 값이 되도록 정합니다(원본 1배 px 기준).
여기에 화면 배율과 크기 옵션을 곱합니다.

| 글꼴 종류 | 본체 | 테두리(위아래 각각) | 원본 줄 높이 |
|---|---|---|---|
| FONT_BIG | 11 | 1 | 16 |
| FONT_SMALL | 7 | 0.5 | 9 |
| FONT_GEO_BIG | 7 | 1 | 9 |
| FONT_GEO_SMALL | 5 | 0.5 | 7 |

- '각'의 높이는 실제로 '각'을 그리는 글꼴(기본은 대체 글꼴인 맑은 고딕)으로 잽니다. 주 글꼴(Segoe UI)도 같은 크기(px/em)로 그립니다.
- 글꼴 크기는 1/4 px 단위로 찾습니다. 목표 높이가 소수이면 내림한 높이를 넘지 않는 가장 큰 크기를 씁니다.
- 세로 위치: '각'의 본체를 원본 비트맵 글꼴 대문자의 윗선과 기준선 사이에 맞춥니다. 모든 글자가 같은 기준선을 씁니다.
- 영문 내림획(g, p, q, y, j)과 '각'보다 큰 한글(뷁 등)은 본체 밖으로 조금 넘칠 수 있습니다.

### 3. 칸보다 긴 문자열은 가로만 줄이기
한 줄 문자열이 칸보다 넓으면 높이는 두고 가로만 최소 80%까지 줄입니다. 정렬은 줄인 폭을 기준으로 합니다.
80%로도 넘치면 정렬 기준점에서 삐져나옵니다. 일반 글자 칸, 목록(TextList) 칸, 버튼(TextButton)에 적용됩니다.

### 4. 천 단위 구분 빈칸
`2 100`의 빈칸이 숫자와 한 덩어리로 배치되고, 폭은 숫자 하나의 절반입니다.

### 5. 지구 화면 스케일 "x1, UI 최적화"
옵션 > 비디오 > 지구 화면 스케일에 새 항목이 생깁니다.
- 지구본은 x1(넓은 지도)로 보이고, 사이드바와 창은 확대해서 보입니다.
- 세로로 긴 해상도에서는 사이드바를 더 크게 확대합니다.
- 지구 화면 배경을 지구본 영역 전체를 덮도록 채웁니다.
- 지구본 이름과 표시는 2배로, 요격 화면도 이 모드에 맞춰 그립니다.
- 국가, 도시 이름이 보이기 시작하는 확대 단계를 모드에서 정할 수 있습니다(`docs/globe-zoom-levels/README.md`).
- 과학자/공학자 여유 인원 표시를 노란색으로 보여 줍니다.

### 6. UI 그래픽 필터
"x1, UI 최적화" + 소프트웨어 출력(OpenGL 끔)에서 Scale, HQX, xBRZ 필터를 고르면 사이드바와 창 같은 UI 그림도 그 필터로 확대합니다.
고해상도 글꼴을 꺼도 적용됩니다. 필터를 고르지 않으면 이전과 같습니다. OpenGL 셰이더 필터에는 적용되지 않습니다.

### 7. 기지 화면
`maximizeInfoScreens`가 켜져 있으면 기지 화면도 320x200 크기로 키워서 보여 줍니다.

## 설치 (Windows x64)
1. zip을 풀면 `Extended-8.7.1-hires-1` 폴더가 생깁니다. 기존 OpenXcom 설치와 따로 둡니다.
2. **원작 게임 데이터는 들어 있지 않습니다.** 직접 가지고 있는 X-COM: UFO Defense(Steam, GOG 등)의 데이터
   (GEODATA, GEOGRAPH, MAPS, ROUTES, SOUND, TERRAIN, UFOGRAPH, UFOINTRO, UNITS …)를 `UFO\` 폴더에 복사해 넣으세요.
   TFTD가 있으면 `TFTD\`에 넣습니다(선택).
3. `OpenXcom-portable.bat`로 실행합니다. 설정, 세이브, 로그는 모두 이 폴더의 `user\` 안에 저장됩니다.
4. 서명되지 않은 exe라서 처음 실행할 때 SmartScreen 경고가 뜰 수 있습니다.

자세한 내용은 zip 안의 `INSTALL.txt`를 보세요.

## 기본 글꼴
글꼴 파일은 동봉하지 않습니다. 시스템에 설치된 글꼴을 씁니다.
- 주 글꼴: **Segoe UI** (`segoeui.ttf`)
- 대체 글꼴: **맑은 고딕** (`malgun.ttf`). 한글처럼 주 글꼴에 없는 글자를 그립니다.
- 찾는 순서: `oxceHiResFont` → `oxceHiResFontFallback` → segoeui.ttf → malgun.ttf → gulim.ttc → batang.ttc → NanumGothic.ttf → NotoSansCJK-Regular.ttc → DejaVuSans.ttf → AppleSDGothicNeo.ttc.
  글자마다 이 순서에서 그 글자가 있는 첫 글꼴로 그립니다. 하나도 찾지 못하면 원래 비트맵 글꼴로 돌아갑니다.
- 찾는 위치(Windows): `%WINDIR%\Fonts`, `%LOCALAPPDATA%\Microsoft\Windows\Fonts`, 레지스트리에 등록된 글꼴.
- 바꾸는 법: 옵션 > "글꼴" 탭에서 주 글꼴을 고르거나, `user\options.cfg`의 `oxceHiResFont`(주), `oxceHiResFontFallback`(대체)에 파일 이름이나 전체 경로를 적습니다(여러 개는 `;`로 구분).
  새 글꼴은 .ttf/.otf/.ttc 파일을 우클릭해 "설치"한 뒤 쓰면 됩니다.

## 옵션
"글꼴" 탭에서 바꿀 수 있는 것 외에는 `options.cfg`에서만 바꿀 수 있습니다.

| 이름 | 기본값 | 의미 |
|---|---|---|
| `oxceHiResOverlay` | `false` (동봉 `user\options.cfg`에서는 `true`) | 고해상도 오버레이 전체 켜기/끄기 |
| `oxceHiResText` | `true` | 고해상도 TTF 글자 ("글꼴" 탭의 켜기/끄기) |
| `oxceHiResFont` | `""` (자동 = Segoe UI) | 주 글꼴. 파일 이름이나 경로, `;`로 여러 개 |
| `oxceHiResFontFallback` | `""` (자동 = 맑은 고딕) | 대체 글꼴 |
| `oxceHiResFontMap` | `""` | 글꼴 종류별 글꼴과 크기. 예: `FONT_BIG=NanumGothic.ttf,110;FONT_SMALL=,90` ("글꼴" 탭이 씀) |
| `oxceHiResFontSize` | `100` | 전체 글자 크기(%) |
| `oxceHiResTextOutlineScale` | `100` (0~400) | 테두리 두께(%). 글꼴 종류별 기본 테두리(위 표)에 곱함. 예전 `oxceHiResTextOutline`을 대체 |
| `oxceHiResTextBold` | `45` | 굵게(1/10 % em). 0이면 굵게 하지 않음 |
| `oxceHiResTextHinting` | `1` | 0 끔, 1 light, 2 normal, 3 mono |
| `oxceHiResTextAntialias` | `true` | 글자 안티에일리어싱 |
| `oxceHiResTextLayout` | `1` | 1 = 문자열 단위로 TTF 간격을 써서 배치, 0 = 글자마다 원래 비트맵 글자 칸 가운데에 배치 |
| `oxceGeoscapeUiOptimized` | `false` | 지구 화면 스케일 "x1, UI 최적화" (비디오 옵션에서 고르면 저장됨) |

## 알려진 한계
- 한글 '각' 기준 크기 하나를 쓰므로, 라틴 대문자는 원본 비트맵 대문자보다 조금 낮게 보입니다(글꼴에 따라 다름). 예: FONT_BIG x3에서 '각' 본체 33 px일 때 같은 크기의 H 높이는 NanumGothic 26 px, DejaVu Sans 27 px입니다(테두리 제외, 개발 환경 Linux에서 측정. Segoe UI/맑은 고딕 값은 측정하지 못했습니다).
- 작은 글꼴 목록은 줄끼리 테두리가 맞닿습니다. 내림획과 '각'보다 큰 한글은 아랫줄 테두리와 겹칠 수 있습니다.
- 모드가 새로 정의한 글꼴 종류는 줄 높이로 크기를 추정합니다(줄 높이 × 0.7).
- UI 그래픽 필터는 소프트웨어 출력에서만 동작하고 OpenGL 셰이더에는 적용되지 않습니다.
- 글자 외의 그림(스프라이트, 배경 등)은 원래 해상도 그대로입니다.
- 한글 등 IME 입력은 이 포크에서 손대지 않았습니다.
- 테두리, 굵게, 힌팅 같은 일부 옵션은 게임 안 메뉴가 없고 `options.cfg`에서만 바꿀 수 있습니다.
- 성능 영향은 따로 측정하지 않았습니다.
- 시험한 범위는 리눅스 가상 화면(Xvfb)에서 자동으로 캡처해 비교한 것과, Windows PC 한 대에서 직전 빌드를 직접 확인한 것이 전부입니다.
- 세이브 파일 형식은 바꾸지 않았습니다. 세이브 머리글의 버전 문자열만 `Extended 8.7.1-hires-1`로 기록됩니다.

## 앞으로의 목표
- OpenGL 셰이더 필터에서도 UI 그래픽 필터 지원
- 한글 등 다국어 입력(IME)
- 모든 리소스(그림, 글꼴 등)를 고해상도로 대체할 수 있게 하기
- 날짜 형식 통일

**도움을 환영합니다.** 버그 제보, 시험, 코드, 번역, 고해상도 리소스 등 어떤 도움이든 반갑습니다.

## 양해와 사과
이 포크의 코드는 제가 직접 짠 것이 아니라 **AI 에이전트(Grok Bot)에게 맡겨 작성했습니다.**
검토와 시험은 했지만 안정성이나 보안을 보증할 수 없습니다. 세이브는 꼭 백업해 두시고, 문제가 생기더라도 너그럽게 양해해 주시면 감사하겠습니다.

## 감사
OpenXcom Extended를 만들고 이끌어 온 **Meridian**과 모든 OXCE 기여자분들,
그리고 OpenXcom 원개발자와 기여자분들께 깊이 감사드립니다. 이 포크는 그분들의 작업 위에 얹은 작은 수정일 뿐입니다.

## 라이선스와 소스
- OpenXcom과 이 포크는 GNU GPL v3 이상으로 배포됩니다(`LICENSE.txt`). 함께 링크한 라이브러리의 고지는 `THIRD_PARTY.txt`와 `licenses\`에 있습니다.
- 소스: https://github.com/cybragon/OpenXcom 의 `hires-1` 브랜치 / 태그 `v8.7.1-hires-1`
- 수정 사실과 수정한 파일 목록은 `CHANGELOG-hires.md`에 있습니다.
