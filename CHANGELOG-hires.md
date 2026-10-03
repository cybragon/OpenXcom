# OpenXcom Extended 8.7.1 – hi-res overlay fork (CHANGELOG / modification notice)

This is a **modified version** of OpenXcom Extended (OXCE) 8.7.1
(upstream commit `441cae1b0`). It is distributed under the same license,
the GNU General Public License version 3 or later (see `LICENSE.txt`).

Modified: 2026-09-25 – 2026-10-03 (release `release-hires-1`, 2026-10-03).
The original OpenXcom / OXCE copyright notices in every file are kept; files listed
below as "added" were written for this fork, files listed as "modified" were changed
for this fork on the dates above.

## Features (release-hires-1, 2026-10-03)

- **Hi-res TTF text overlay** (`oxceHiResOverlay`, `oxceHiResText`): game text is drawn
  with system TrueType fonts at the output resolution on top of the scaled 320x200 frame
  (OpenGL and software paths). Layout and line breaks stay those of the bitmap fonts.
  Per font type (Font.dat id) font face and size (Options > Fonts).
- **Glyph size rule**: per font type the body and the outline have a fixed height in
  original 1x pixels, times the output scale and the size options: FONT_BIG 11 + 1,
  FONT_SMALL 7 + 0.5, FONT_GEO_BIG 7 + 1, FONT_GEO_SMALL 5 + 0.5. Latin (reference `H`)
  and Hangul/Chinese/Japanese (reference `각`) are sized separately so both bodies have that
  height (font sizes in 1/4 px steps), and both are aligned to the capital letters of the
  bitmap font. Descenders and glyphs taller than `각` may extend beyond the body.
- **Outline thickness** (`oxceHiResTextOutlineScale`, %, default 100, 0–400): multiplies the
  per-font default outline (replaces the former `oxceHiResTextOutline`).
- **Horizontal squeeze**: a single line wider than its Text box is narrowed (height kept)
  down to 80 %; alignment uses the squeezed width.
- **Thousands separators** are part of the number run, half a digit wide.
- **Geoscape "x1, UI optimized" mode**: globe at x1, UI (sidebar, windows) magnified;
  sidebar magnified on tall resolutions; geoscape background cover-scaled; globe labels
  with per-country/per-city zoom levels; slacking indicator.
- **UI graphic filters**: in "x1, UI optimized" (software path) UI graphics are magnified
  with the selected software filter (Scale / hqx / xBRZ), also with hi-res text off.
- **Basescape**: base screens maximized to 320x200 with `maximizeInfoScreens`.
- Windows x64 cross build (MinGW-w64, static) and Unicode font paths.

## Added files
- `docs/globe-zoom-levels/README.md`
- `docs/globe-zoom-levels/test-mod/globe_zoom_test/globe_zoom_test.rul`
- `docs/globe-zoom-levels/test-mod/globe_zoom_test/metadata.yml`
- `scripts/hires-overlay-build-win64.sh`
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
- `CHANGELOG-hires.md`, `install/hires/*`, `scripts/hires-overlay-package-win64.sh`

## Modified files
- `CMakeLists.txt`
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
- `README.md` (notice at the top)
