# OpenXcom Extended 8.7.1-hires-2 (unofficial hi-res fork)

[한국어](README-hires.ko.md) · [Changelog](CHANGELOG-hires.md) · [Modder notes](MODDING-hires.md)

> **This is an unofficial fork.** It is a personal build modified from OpenXcom Extended (OXCE) 8.7.1,
> not an official OXCE/OpenXcom release. **Please do not contact the original teams (OXCE, OpenXcom)
> about this build.**

- Version name: `8.7.1-hires-2` (window title: `OpenXcom Extended 8.7.1-hires-2 (v2026-10-13)`)
- Planned release tag: `v8.7.1-hires-2`
- Base: OXCE 8.7.1 (commit `441cae1b0`)
- License: GNU GPL v3 or later (`LICENSE.txt`)

## What changed
The game still runs in 320x200 logical coordinates, but text and (optionally) mod images are drawn
on a separate layer at your **output resolution**, so they stay sharp when the window is scaled up.

### 1. Hi-res TTF text overlay
The game draws a 320x200 frame and scales it up, so the bigger the scale, the blockier the text.
This fork redraws only the text with TrueType fonts installed on the system (FreeType), **at the output
resolution**, on top of the scaled frame.
- Works with both the OpenGL and the software output.
- Line breaks, alignment and text box positions stay those of the original bitmap fonts (no layout changes).
- Options > "Fonts" tab: on/off, and font face and size per font type (Big, Small, Geo big, Geo small). Changes apply immediately.
- Only useful when the window is bigger than the original 320x200 at scale 1.

### 2. Glyph size rule
All characters (Latin, digits, Hangul, Chinese, Japanese) use **one size per font type**.
That size is chosen so the Hangul syllable **'각'** has the body height and outline below (in original
1x pixels), times the output scale and the size options.

| Font type | Body | Outline (top and bottom each) | Original line height |
|---|---|---|---|
| FONT_BIG | 11 | 1 | 16 |
| FONT_SMALL | 7 | 0.5 | 9 |
| FONT_GEO_BIG | 7 | 1 | 9 |
| FONT_GEO_SMALL | 5 | 0.5 | 7 |

- '각' is measured with the font that actually draws it (by default the fallback font, Malgun Gothic). The primary font (Segoe UI) uses the same size (px/em).
- Font sizes are searched in 1/4 px steps. A fractional target height is rounded down, and the largest size that does not exceed it is used.
- Vertical position: the body of '각' is aligned between the top line and the baseline of the capital letters of the original bitmap font. All characters share that baseline.
- Latin descenders (g, p, q, y, j) and Hangul taller than '각' (e.g. 뷁) may extend slightly beyond the body.

### 3. Horizontal squeeze of over-long strings
A single line wider than its box is narrowed horizontally (height kept) down to 80%. Alignment uses the
narrowed width. If it still does not fit at 80%, it sticks out from the alignment anchor. Applies to text
boxes, list (TextList) cells and buttons (TextButton).

### 4. Thousands separators
The space in `2 100` is laid out together with the digits, and it is half a digit wide.

### 5. Geoscape scale "x1, UI optimized"
A new entry in Options > Video > Geoscape scale.
- The globe is shown at x1 (wide map), while the sidebar and windows are magnified.
- On tall resolutions the sidebar is magnified further.
- The geoscape background is scaled to cover the whole globe area.
- Globe labels and markers are drawn at 2x, and dogfights are drawn to match this mode.
- Mods can choose the globe zoom level from which country and city names appear (`docs/globe-zoom-levels/README.md`).
- The idle scientists/engineers ("slacking") indicator is shown in yellow.

### 6. UI graphics filters
In "x1, UI optimized", UI graphics such as the sidebar and windows are magnified with the same filter as
the battlefield: with the OpenGL output `oxceHiResBattleFilter` (0 none, 1 Scale, 2 HQx, 3 xBRZ; default 1,
so the UI of OpenGL users is now smoothed), with the software output the Scale, HQX or xBRZ screen filter.
This also works with hi-res text off. 1x areas (the globe) are not magnified and stay as they are.

### 7. Base screens
With `maximizeInfoScreens` on, the base screens are also enlarged to the 320x200 size.

### 8. Hi-res mod images
Options > Video, hi-res dropdown: **Hi-res off (original)** / **Hi-res: geoscape** / **Hi-res: all**. Off = stock OXCE look (only TTF fonts, if enabled, remain). Geoscape = menus, geoscape, base, Ufopaedia, inventory (also when opened in battle) and the battle UI are hi-res, the battlefield (tiles, units, map) uses the stock path. All = the battlefield is drawn hi-res too. An old "on" setting becomes All.
- Mods can ship larger 32-bit PNG or JPG images for the geoscape, basescape, Ufopaedia, menus, the
  inventory and the battlescape UI chrome (TAC00/01, ICONS, UNIBORD, scanner/motion/medikit borders,
  rank badges) and the shared sets BIGOBS, SMOKE, DETBLOB.
- The `width`/`height` in `extraSprites` is the logical size; a bigger file is drawn hi-res in that box.
  Multi-frame sheets (`subX`/`subY`) work the same way.
- Images larger than their box are area-averaged down. Smaller ones are enlarged with the interpolation of the
  screen filter: none → sharp (nearest), Scale or any OpenGL output → smooth, HQx / xBRZ → that filter.
  Battlescape: every map picture is placed by nearest neighbour, then each scale group is interpolated once
  over the whole map (no seams between tiles); pictures at exactly 1x stay untouched.
- Switching hi-res on/off reloads the mods (from the main menu) or needs a restart (in a game).
- Other battlescape resources (map, units, terrain, hand objects, cursor) stay 8-bit; 32-bit files
  for them are converted to the battlescape palette with a log warning.
- **Hi-res off still loads hi-res mods:** 32-bit/JPG images are downscaled to the rule size and
  quantized to the palette with a log warning instead of failing. Files that cannot be decoded
  disable the mod as before.
- Details for modders: `MODDING-hires.md`.

### 9. Native 32-bit inventory
With hi-res images on, the inventory screen is drawn in full colour at output resolution: 32-bit
paperdoll layers, item pictures and rank badges as they are.

### 10. Battlescape UI digits
TU, health, ammo and other battlescape numbers are drawn with a bold TTF digit font (`BATTLE_DIGITS`,
`oxceHiResDigitFont`), sized to the original 3x5 digit rows.

### 11. Korean (IME) text input (Windows only)
IME input in text fields, with the caret, composition and candidate window placed on the TTF text.

## Installation (Windows x64)
1. Unzip; you get an `Extended-8.7.1-hires-2` folder. Keep it separate from any other OpenXcom install.
2. **No original game data is included.** Copy the data of your own copy of X-COM: UFO Defense
   (Steam, GOG, ...), i.e. GEODATA, GEOGRAPH, MAPS, ROUTES, SOUND, TERRAIN, UFOGRAPH, UFOINTRO, UNITS ...,
   into the `UFO\` folder. Optionally put TFTD data into `TFTD\`.
3. Start with `OpenXcom-portable.bat`. Settings, saves and logs all stay in this folder's `user\`.
4. The exe is not signed, so SmartScreen may warn you on the first start.

See `INSTALL.txt` (Korean) in the zip for details.

## Default fonts
No font files are bundled; installed system fonts are used.
- Primary font: **Segoe UI** (`segoeui.ttf`)
- Fallback font: **Malgun Gothic** (`malgun.ttf`), for characters the primary font lacks, such as Hangul.
- Search order: `oxceHiResFont` → `oxceHiResFontFallback` → segoeui.ttf → malgun.ttf → gulim.ttc → batang.ttc → NanumGothic.ttf → NotoSansCJK-Regular.ttc → DejaVuSans.ttf → AppleSDGothicNeo.ttc.
  Each character is drawn with the first font in this order that has it. If none is found, the original bitmap fonts are used.
- Folders searched on Windows: `%WINDIR%\Fonts`, `%LOCALAPPDATA%\Microsoft\Windows\Fonts`, and fonts registered in the registry.
- To change: pick the primary font in Options > "Fonts", or set `oxceHiResFont` (primary) and `oxceHiResFontFallback` (fallback) in `user\hires_option.cfg` to a file name or full path (several separated by `;`).
  To use a new font, right-click the .ttf/.otf/.ttc file and choose "Install".

## Options
Apart from the "Fonts" tab and the Video hi-res dropdown, these are set in `user\hires_option.cfg` (next to `options.cfg`; created with the defaults if missing, read at game start and at every battlescape start).

| Name | Default | Meaning |
|---|---|---|
| `oxceHiResOverlay` | `false` (`true` in the bundled `user\options.cfg`) | Master switch of the hi-res overlay |
| `oxceHiResText` | `true` | Hi-res TTF text (on/off in the "Fonts" tab) |
| `oxceHiResFont` | `""` (automatic = Segoe UI) | Primary font: file name or path, several separated by `;` |
| `oxceHiResFontFallback` | `""` (automatic = Malgun Gothic) | Fallback font |
| `oxceHiResFontMap` | `""` | Font and size per font type, e.g. `FONT_BIG=NanumGothic.ttf,110;FONT_SMALL=,90` (written by the "Fonts" tab) |
| `oxceHiResFontSize` | `100` | Overall text size (%) |
| `oxceHiResTextOutlineScale` | `100` (0–400) | Outline thickness (%), multiplied with the per-font-type outline above. Replaces the former `oxceHiResTextOutline` |
| `oxceHiResTextBold` | `45` | Emboldening (1/10 % of the em); 0 = none |
| `oxceHiResTextHinting` | `1` | 0 off, 1 light, 2 normal, 3 mono |
| `oxceHiResTextAntialias` | `true` | Anti-aliased text |
| `oxceHiResTextLayout` | `1` | 1 = lay out strings with TTF spacing; 0 = each glyph centred in its original bitmap glyph cell |
| `oxceGeoscapeUiOptimized` | `false` | Geoscape scale "x1, UI optimized" (saved when chosen in the video options) |
| `oxceHiResLevel` | `-1` | Hi-res level (Video dropdown): 0 off, 1 geoscape, 2 all; -1 = take over the old `oxceHiResAssets` (on = 2). `oxceHiResAssets` follows it |
| `oxceHiResShadeCurve` | `100,92,…,2` | Battlescape brightness % per light level 1–16 |
| `oxceHiResBattleFilter` | `1` | Interpolation filter with the OpenGL output (not in the UI): 0 none (sharp), 1 Scale (smooth), 2 HQx, 3 xBRZ. Applies to hi-res images and the battlescape map. The GPU is used automatically for all of 1, 2 and 3 when OpenGL is 3.0 or newer and the shaders build (HQx and xBRZ are GLSL ports of the CPU code); otherwise the CPU resampler does the same filter. openxcom.log says which path runs and why (`battlefield path: ...`). xBRZ on the CPU is slow at high resolutions: the log then also has a warning (`the xBRZ battlefield filter runs on the CPU`); use OpenGL 3.0+ or filter 2/1. With the software output the screen filter decides and this is ignored |
| `oxceHiResVerifyGPU` | `false` | Debug (not in the UI, slow): compares every OpenGL battlefield frame with the CPU resampler's full recompose of the same frame and logs the number of differing pixels (`HiResLayer verify (GPU vs CPU ...)`) |
| `oxceHiResDebugScaleMask` | `false` | Battlescape debug: tint pixels by scale group |
| `oxceHiResDigitFont` | `""` (automatic = bold system font) | Font of the battlescape UI digits |

## Known limitations
- Since one size based on Hangul '각' is used, Latin capitals look a bit lower than the original bitmap capitals (depends on the font). Example: at FONT_BIG x3, with a '각' body of 33 px, an H at the same size is 26 px tall in NanumGothic and 27 px in DejaVu Sans (outline excluded, measured on the Linux development box; Segoe UI / Malgun Gothic were not measured).
- In small-font lists the outlines of neighbouring lines touch. Descenders and Hangul taller than '각' may overlap the outline of the next line.
- Font types newly defined by mods get an estimated size (line height × 0.7).
- UI graphics filters work with the software output only, not with OpenGL shaders.
- The battle map and units are still 8-bit (only the battlescape UI chrome and the shared sets are hi-res).
- Palette effects on hi-res backgrounds, fades and blinking are not applied to hi-res images yet.
- The OpenGL shader filter is not applied to the TTF text.
- IME input is Windows only.
- Some options (outline, bold, hinting, ...) have no in-game menu and can only be changed in `hires_option.cfg`.
- The performance impact has not been measured.
- Testing was mostly automated screenshot comparisons on a Linux virtual display (Xvfb), plus manual checks of test builds on Windows. Please back up your saves.
- The savegame format was not changed. Only the version string in the save header is written as `Extended 8.7.1-hires-2`. With hi-res images off, rendering is the same as OXCE 8.7.1; existing mods need no changes.

## Goals
- UI graphics filters with OpenGL shader filters too
- Hi-res battle map and units, palette effects on hi-res images
- IME input on other platforms
- Unified date format

**Help is welcome.** Bug reports, testing, code, translations, hi-res resources: any help is appreciated.

## Apology and disclaimer
The code of this fork was not written by me: **it was written by an AI agent (Grok Bot) on my behalf.**
It has been reviewed and tested to some extent, but I cannot guarantee its stability or security.
Please back up your saves, and please bear with me if something goes wrong.

## Thanks
Many thanks to **Meridian**, who created and leads OpenXcom Extended, and to all OXCE contributors,
as well as to the original OpenXcom developers and contributors. This fork is only a small change on top of their work.

## License and source
- OpenXcom and this fork are distributed under the GNU GPL v3 or later (`LICENSE.txt`). Notices of the linked libraries are in `THIRD_PARTY.txt` and `licenses\`.
- Source: https://github.com/cybragon/OpenXcom (planned tag `v8.7.1-hires-2`; hires-1: branch `hires-1` / tag `v8.7.1-hires-1`)
- The modification notice and the list of modified files are in `CHANGELOG-hires.md`.
