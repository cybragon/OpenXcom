# OpenXcom Extended 8.7.1-hires-1 – CHANGELOG / modification notice

This is a **modified version** of OpenXcom Extended (OXCE) 8.7.1 (upstream commit `441cae1b0`),
an unofficial fork. It is distributed under the same license, the GNU General Public License
version 3 or later (see `LICENSE.txt`). See `README-hires.en.md` / `README-hires.ko.md`.

- Version name: `8.7.1-hires-1` (release tag `v8.7.1-hires-1`, branch `hires-1` at https://github.com/cybragon/OpenXcom)
- Modified: 2026-09-25 – 2026-10-04
- The original OpenXcom / OXCE copyright notices in every file are kept. Files listed below as
  "added" were written for this fork; files listed as "modified" were changed for this fork on the
  dates above.

## 8.7.1-hires-1 (2026-10-04)

- **Hi-res TTF text overlay** (`oxceHiResOverlay`, `oxceHiResText`): game text drawn with system
  TrueType fonts (FreeType) at the output resolution over the scaled 320x200 frame, OpenGL and
  software paths. Layout and line breaks stay those of the bitmap fonts. Per font type face and
  size (Options > Fonts).
- **Glyph size rule**: one font size per font type for all characters, chosen so that Hangul `각`
  (measured with the face that draws it) has the body + outline height FONT_BIG 11 + 1,
  FONT_SMALL 7 + 0.5, FONT_GEO_BIG 7 + 1, FONT_GEO_SMALL 5 + 0.5 (1x px, times output scale and
  size options); 1/4 px font sizes; aligned to the capitals of the bitmap font.
- **Default fonts**: primary Segoe UI (`segoeui.ttf`), fallback Malgun Gothic (`malgun.ttf`), then
  gulim, batang, NanumGothic, Noto Sans CJK, DejaVu Sans, Apple SD Gothic Neo. No fonts bundled.
- **Outline thickness** (`oxceHiResTextOutlineScale`, %, default 100, 0–400): multiplies the
  per-font default outline (replaces the former `oxceHiResTextOutline`).
- **Horizontal squeeze**: a single line wider than its Text box is narrowed (height kept) down to
  80 %; alignment uses the squeezed width.
- **Thousands separators** are part of the number run, half a digit wide.
- **Geoscape "x1, UI optimized"** (`oxceGeoscapeUiOptimized`): globe at x1, UI (sidebar, windows)
  magnified; sidebar magnified on tall resolutions; cover-scaled geoscape background; 2x globe
  labels/markers; dogfights; per-country/per-city label zoom levels (docs/globe-zoom-levels);
  yellow slacking indicator.
- **UI graphic filters**: in "x1, UI optimized" (software path) UI graphics are magnified with the
  selected software filter (Scale / hqx / xBRZ), also with hi-res text off.
- **Basescape**: base screens maximized to 320x200 with `maximizeInfoScreens`.
- Version string `Extended 8.7.1-hires-1` (version number for mods and rulesets unchanged: 8.7.1.0).
- Windows x64 cross build (MinGW-w64, static), Unicode font paths, packaging scripts.

## Added files
- `CHANGELOG-hires.md`
- `README-hires.en.md`
- `README-hires.ko.md`
- `docs/globe-zoom-levels/README.md`
- `docs/globe-zoom-levels/test-mod/globe_zoom_test/globe_zoom_test.rul`
- `docs/globe-zoom-levels/test-mod/globe_zoom_test/metadata.yml`
- `install/hires/INSTALL.txt`
- `install/hires/OpenXcom-portable.bat`
- `install/hires/THIRD_PARTY.txt`
- `install/hires/user/options.cfg`
- `scripts/hires-overlay-build-win64.sh`
- `scripts/hires-overlay-package-win64.sh`
- `src/Basescape/MaximizedBasescape.cpp`
- `src/Basescape/MaximizedBasescape.h`
- `src/Engine/HiResFont.cpp`
- `src/Engine/HiResFont.h`
- `src/Engine/HiResLayer.cpp`
- `src/Engine/HiResLayer.h`
- `src/Engine/ScaledPanel.cpp`
- `src/Engine/ScaledPanel.h`
- `src/Engine/UiImageScaler.cpp`
- `src/Engine/UiImageScaler.h`
- `src/Geoscape/GeoSidebarLayout.h`
- `src/Menu/OptionsFontsState.cpp`
- `src/Menu/OptionsFontsState.h`

## Modified files
- `CMakeLists.txt`
- `README.md`
- `bin/common/Language/OXCE/en-GB.yml`
- `bin/common/Language/OXCE/en-US.yml`
- `bin/common/Language/OXCE/ko.yml`
- `bin/standard/xcom1/interfaces.rul`
- `src/Basescape/BasescapeState.cpp`
- `src/Basescape/BasescapeState.h`
- `src/Basescape/PlaceLiftState.cpp`
- `src/Basescape/PlaceLiftState.h`
- `src/Battlescape/BriefingState.cpp`
- `src/Battlescape/Inventory.cpp`
- `src/Battlescape/InventoryState.cpp`
- `src/Battlescape/Map.cpp`
- `src/CMakeLists.txt`
- `src/Engine/Font.cpp`
- `src/Engine/Font.h`
- `src/Engine/Game.cpp`
- `src/Engine/Options.cpp`
- `src/Engine/Options.h`
- `src/Engine/Options.inc.h`
- `src/Engine/Screen.cpp`
- `src/Engine/State.cpp`
- `src/Engine/State.h`
- `src/Engine/Surface.cpp`
- `src/Engine/Zoom.cpp`
- `src/Geoscape/BuildNewBaseState.cpp`
- `src/Geoscape/DogfightState.cpp`
- `src/Geoscape/GeoscapeState.cpp`
- `src/Geoscape/GeoscapeState.h`
- `src/Geoscape/Globe.cpp`
- `src/Geoscape/Globe.h`
- `src/Geoscape/SelectDestinationState.cpp`
- `src/Interface/Text.cpp`
- `src/Menu/CutsceneState.cpp`
- `src/Menu/OptionsBaseState.cpp`
- `src/Menu/OptionsBaseState.h`
- `src/Menu/OptionsVideoState.cpp`
- `src/Menu/StartState.cpp`
- `src/Menu/VideoState.cpp`
- `src/Mod/City.cpp`
- `src/Mod/City.h`
- `src/Mod/Mod.cpp`
- `src/Mod/RuleCountry.cpp`
- `src/Mod/RuleCountry.h`
- `src/Mod/RuleRegion.cpp`
- `src/Mod/RuleRegion.h`
- `src/OpenXcom.2010.vcxproj`
- `src/OpenXcom.2010.vcxproj.filters`
- `src/OpenXcom.rc`
- `src/version.h`
