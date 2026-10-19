# OpenXcom Extended 8.7.1-hires-2 – CHANGELOG / modification notice

This is a **modified version** of OpenXcom Extended (OXCE) 8.7.1 (upstream commit `441cae1b0`),
an unofficial fork. It is distributed under the same license, the GNU General Public License
version 3 or later (see `LICENSE.txt`). See `README-hires.en.md` / `README-hires.ko.md`.

- Version name: `8.7.1-hires-2` (planned release tag `v8.7.1-hires-2`)
- Modified: 2026-09-25 – 2026-10-13
- The original OpenXcom / OXCE copyright notices in every file are kept. Files listed below as
  "added" were written for this fork; files listed as "modified" were changed for this fork on the
  dates above.

## battle test builds (unreleased, battle-8: 2026-10-19)
- WEBP pictures (`.webp`, by extension) for hi-res assets, like JPEG; a missing mod file is an error (no other extension is tried).
- UI panel filter follows the battlefield filter rule (OpenGL output: `oxceHiResBattleFilter`; software: the screen filter).
- Avatar mode (mod defines `AvatarBackground`): base soldier info and battle unit info screens show the battle avatar
  (unit info: with the flag, scaled to the name row); the battle HUD avatar is drawn once at output resolution.
- CPU xBRZ: exact partial map updates (no full redraw on every change); a log warning when xBRZ runs on the CPU.
- battle-9: WebP decoded with libwebp directly (lossless / extended / alpha WebP; SDL_image 1.2 only read simple lossy);
  flags keep their own aspect at the slot height (base: stock 40x20 slot rule; battle: bar height, right edge 1 px in);
  battle unit info avatar 20x18 and flag 18 px (bar height), base soldier info avatar 26x23 and flag 20 px (stock slot).

- battle-11 (2026-10-20): 32-bit `SMOKE.PCK` and `X1.PCK` sheets are allowed on the hi-res battle map (Hi-res: all) and drawn
  translucent with their alpha in the layered battle path (smoke, tile/unit fire, small hits, big explosions); spec in MODDING-hires.md.
  Faster hi-res UI delivery (no SDL_GetRGB per pixel; pre-blit snapshots only for pictures with transparency).
  Options > Video: the hi-res dropdown no longer covers the open filter list; with automatic OpenGL (xBRZ) the filter shows xBRZ.

## 8.7.1-hires-2 (2026-10-13)

Changes since 8.7.1-hires-1 (test builds 2test–10test).

### Hi-res images (option `oxceHiResAssets`)
- extraSprites replacements whose size differs from the replaced image are drawn at the output
  resolution. `width`/`height` is the logical size; a larger file is fitted into that box keeping its
  aspect. Sheets (`subX`/`subY`) are cut at one scale. Mod-defined images are fitted per consumer box.
- 32-bit PNG and JPEG pictures allowed where 32-bit is allowed: geoscape, basescape, Ufopaedia, menus.
  8-bit pictures with their own palette are drawn in their own colours.
- 32-bit pictures that fit their box become 8-bit images (drawn like stock images); Ufopaedia images
  are quantized to their article palette.
- Edges follow the picture's alpha at the output resolution (also through child surfaces), with
  premultiplied alpha in the overlay and the native canvas.
- Unified resampling: exact size = copy, larger = area-average downscale, smaller = upscale mode
  Sharp (nearest) / Smooth / Pixel-art (xBRZ) / Auto (follows the screen filter).
- Battlescape: UI chrome allow-listed (TAC00/01, UNIBORD, SCANBORD, DETBORD, MEDIBORD, ICONS,
  TinyRanks); battlescape-palette proxies use indices 1-239. Shared sets SMOKE.PCK, BIGOBS.PCK and
  DETBLOB.DAT: oversized sheets downscaled to the rule grid, battlescape-palette proxy for the map and
  shaded draws, picture for the UI. Other battlescape resources stay 8-bit (converted with a warning).
- Unpressed battlescape buttons show the parent's hi-res picture and their own pictures (rank badge
  in full colour); AvatarBackground fitted to its slot.
- Hi-res pictures move with copied pixels (GEOBORD sidebar, dogfight buttons); list row highlights and
  scrollbar tracks stay hi-res with a tint. Basescape palette for craft previews and commendations;
  INTICON keeps its geoscape palette in the dogfight; SCANG.DAT uses the battlescape palette.
- **Native inventory**: with hi-res images on, the inventory is drawn on a full-colour canvas at the
  output resolution (32-bit paperdoll layers, BIGOBS items); vanilla 8-bit paperdolls use the state
  palette.
- Hi-res images off: 32-bit/JPG mod images are downscaled to the rule size and quantized to the right
  palette with a log warning instead of an error. Files that cannot be decoded disable the offending
  mod with a clear log entry.
- The option value is taken at mod load for loading and drawing alike.
- Options > Video: one dropdown "Hi-res off (original) / Sharp / Smooth / Pixel-art / Auto"
  (`oxceHiResAssets` + `oxceHiResUpscale`, 3 = Auto is the default upscale mode; old values 0-2 kept).
- Modder notes: `MODDING-hires.md`.

### Text
- Battlescape UI digits (all NumberText) drawn with a bold TTF digit font (`BATTLE_DIGITS`,
  `oxceHiResDigitFont`, FreeType emboldening fallback), sized by the digit '0' and centred on the digit
  rows. FONT_SMALL and BATTLE_DIGITS sizes are rounded up.
- Letter wrapping (CJK languages) does not split a Latin word or number; a Latin run after a space
  moves to the next line.
- Game-date displays use `STR_DATE_FORMAT` (en-US "{0} {1} {2}", ko "{2}년 {1} {0}").
- The fonts menu lists Font.dat font types only.

### Korean / IME input (Windows only)
- IME input in text fields (`Engine/ImeWin32`) with inline composition; caret and candidate window
  placed on the TTF glyphs.

### Fixes
- Crew preview / base inventory exit restores 320x200 while the maximized basescape is active
  (was half-size, rest black).
- BriefingState and AliensCrashState magnified like the other geoscape-resolution screens while a battle exists.

### Build
- Win64 package: libjpeg-turbo 3.0.4 (static) for JPEG pictures, license texts; package folder name option.
- Version string `Extended 8.7.1-hires-2 (v2026-10-13)` (version number for mods and rulesets unchanged: 8.7.1.0).

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
- `MODDING-hires.md`
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
- `src/Battlescape/AvatarDraw.cpp`
- `src/Battlescape/AvatarDraw.h`
- `src/Engine/HiResAssets.cpp`
- `src/Engine/HiResAssets.h`
- `src/Engine/HiResCanvas.cpp`
- `src/Engine/HiResCanvas.h`
- `src/Engine/HiResFont.cpp`
- `src/Engine/HiResFont.h`
- `src/Engine/HiResLayer.cpp`
- `src/Engine/HiResLayer.h`
- `src/Engine/HiResResample.cpp`
- `src/Engine/HiResResample.h`
- `src/Engine/ImeWin32.cpp`
- `src/Engine/ImeWin32.h`
- `src/Engine/ScaledPanel.cpp`
- `src/Engine/ScaledPanel.h`
- `src/Engine/UiImageScaler.cpp`
- `src/Engine/UiImageScaler.h`
- `src/Geoscape/GeoSidebarLayout.h`
- `src/Menu/OptionsFontsState.cpp`
- `src/Menu/OptionsFontsState.h`
- `tools/build_webp_win64.sh`

## Modified files
- `CMakeLists.txt`
- `README.md`
- `bin/common/Language/OXCE/en-GB.yml`
- `bin/common/Language/OXCE/en-US.yml`
- `bin/common/Language/OXCE/ko.yml`
- `bin/standard/xcom1/interfaces.rul`
- `src/Basescape/BaseInfoState.cpp`
- `src/Basescape/BasescapeState.cpp`
- `src/Basescape/BasescapeState.h`
- `src/Basescape/PlaceLiftState.cpp`
- `src/Basescape/PlaceLiftState.h`
- `src/Basescape/SoldierDiaryOverviewState.cpp`
- `src/Basescape/SoldierInfoState.cpp`
- `src/Basescape/SoldierInfoState.h`
- `src/Basescape/SoldierMemorialState.cpp`
- `src/Battlescape/AliensCrashState.cpp`
- `src/Battlescape/BattlescapeState.cpp`
- `src/Battlescape/BattlescapeState.h`
- `src/Battlescape/BriefingState.cpp`
- `src/Battlescape/Inventory.cpp`
- `src/Battlescape/Inventory.h`
- `src/Battlescape/InventoryState.cpp`
- `src/Battlescape/InventoryState.h`
- `src/Battlescape/Map.cpp`
- `src/Battlescape/UnitInfoState.cpp`
- `src/Battlescape/UnitInfoState.h`
- `src/CMakeLists.txt`
- `src/Engine/Font.cpp`
- `src/Engine/Font.h`
- `src/Engine/Game.cpp`
- `src/Engine/HiResCanvas.cpp`
- `src/Engine/HiResCanvas.h`
- `src/Engine/HiResLayer.cpp`
- `src/Engine/HiResResample.cpp`
- `src/Engine/HiResResample.h`
- `src/Engine/Options.cpp`
- `src/Engine/Options.h`
- `src/Engine/Options.inc.h`
- `src/Engine/Screen.cpp`
- `src/Engine/State.cpp`
- `src/Engine/State.h`
- `src/Engine/Surface.cpp`
- `src/Engine/Surface.h`
- `src/Engine/UiImageScaler.cpp`
- `src/Engine/Zoom.cpp`
- `src/Geoscape/BaseDefenseState.cpp`
- `src/Geoscape/BuildNewBaseState.cpp`
- `src/Geoscape/DogfightState.cpp`
- `src/Geoscape/GeoscapeEventState.cpp`
- `src/Geoscape/GeoscapeState.cpp`
- `src/Geoscape/GeoscapeState.h`
- `src/Geoscape/Globe.cpp`
- `src/Geoscape/Globe.h`
- `src/Geoscape/GraphsState.cpp`
- `src/Geoscape/MissionDetectedState.cpp`
- `src/Geoscape/SelectDestinationState.cpp`
- `src/Interface/BattlescapeButton.cpp`
- `src/Interface/BattlescapeButton.h`
- `src/Interface/NumberText.cpp`
- `src/Interface/NumberText.h`
- `src/Interface/ScrollBar.cpp`
- `src/Interface/Text.cpp`
- `src/Interface/Text.h`
- `src/Interface/TextEdit.cpp`
- `src/Interface/TextEdit.h`
- `src/Interface/TextList.cpp`
- `src/Menu/CutsceneState.cpp`
- `src/Menu/ErrorMessageState.cpp`
- `src/Menu/OptionsAdvancedState.cpp`
- `src/Menu/OptionsBaseState.cpp`
- `src/Menu/OptionsBaseState.h`
- `src/Menu/OptionsVideoState.cpp`
- `src/Menu/OptionsVideoState.h`
- `src/Menu/StartState.cpp`
- `src/Menu/StatisticsState.cpp`
- `src/Menu/VideoState.cpp`
- `src/Mod/City.cpp`
- `src/Mod/City.h`
- `src/Mod/ExtraSprites.cpp`
- `src/Mod/ExtraSprites.h`
- `src/Mod/Mod.cpp`
- `src/Mod/Mod.h`
- `src/Mod/RuleCountry.cpp`
- `src/Mod/RuleCountry.h`
- `src/Mod/RuleRegion.cpp`
- `src/Mod/RuleRegion.h`
- `src/OpenXcom.2010.vcxproj.filters`
- `src/OpenXcom.2010.vcxproj`
- `src/OpenXcom.rc`
- `src/Savegame/GameTime.cpp`
- `src/Savegame/GameTime.h`
- `src/Savegame/SaveConverter.cpp`
- `src/Savegame/SavedGame.cpp`
- `src/Ufopaedia/ArticleStateArmor.cpp`
- `src/Ufopaedia/ArticleStateCraft.cpp`
- `src/Ufopaedia/ArticleStateCraftWeapon.cpp`
- `src/Ufopaedia/ArticleStateSoldier.cpp`
- `src/Ufopaedia/ArticleStateTFTD.cpp`
- `src/Ufopaedia/ArticleStateTextImage.cpp`
- `src/Ufopaedia/ArticleStateUfo.cpp`
- `src/Ufopaedia/ArticleStateUnit.cpp`
- `src/Ufopaedia/ArticleStateVehicle.cpp`
- `src/version.h`
