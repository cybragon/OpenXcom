# OpenXcom Extended 8.7.1-hires-1 (unofficial hi-res text fork)

[한국어](README-hires.ko.md) · [Changelog](CHANGELOG-hires.md)

> **This is an unofficial fork.** It is a personal build modified from OpenXcom Extended (OXCE) 8.7.1,
> not an official OXCE/OpenXcom release. **Please do not contact the original teams (OXCE, OpenXcom)
> about this build.**

- Version name: `8.7.1-hires-1` (window title: `OpenXcom Extended 8.7.1-hires-1 (v2026-09-19)`; the date in brackets is the unchanged date of the OXCE 8.7.1 base)
- Planned release tag: `v8.7.1-hires-1`
- Base: OXCE 8.7.1 (commit `441cae1b0`)
- License: GNU GPL v3 or later (`LICENSE.txt`)

## What changed

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
In "x1, UI optimized" with the software output (OpenGL off), if the Scale, HQX or xBRZ filter is selected,
UI graphics such as the sidebar and windows are magnified with that filter. This also works with hi-res
text off. Without a filter nothing changes. OpenGL shader filters are not supported for this.

### 7. Base screens
With `maximizeInfoScreens` on, the base screens are also enlarged to the 320x200 size.

## Installation (Windows x64)
1. Unzip; you get an `Extended-8.7.1-hires-1` folder. Keep it separate from any other OpenXcom install.
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
- To change: pick the primary font in Options > "Fonts", or set `oxceHiResFont` (primary) and `oxceHiResFontFallback` (fallback) in `user\options.cfg` to a file name or full path (several separated by `;`).
  To use a new font, right-click the .ttf/.otf/.ttc file and choose "Install".

## Options
Apart from the "Fonts" tab, these are set in `options.cfg` only.

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

## Known limitations
- Since one size based on Hangul '각' is used, Latin capitals look a bit lower than the original bitmap capitals (depends on the font). Example: at FONT_BIG x3, with a '각' body of 33 px, an H at the same size is 26 px tall in NanumGothic and 27 px in DejaVu Sans (outline excluded, measured on the Linux development box; Segoe UI / Malgun Gothic were not measured).
- In small-font lists the outlines of neighbouring lines touch. Descenders and Hangul taller than '각' may overlap the outline of the next line.
- Font types newly defined by mods get an estimated size (line height × 0.7).
- UI graphics filters work with the software output only, not with OpenGL shaders.
- Graphics other than text (sprites, backgrounds, ...) stay at their original resolution.
- IME input (Hangul etc.) was not touched by this fork.
- Some options (outline, bold, hinting, ...) have no in-game menu and can only be changed in `options.cfg`.
- The performance impact has not been measured.
- Testing consisted only of automated screenshot comparisons on a Linux virtual display (Xvfb) and a manual check of the previous build on one Windows PC.
- The savegame format was not changed. Only the version string in the save header is written as `Extended 8.7.1-hires-1`.

## Goals
- UI graphics filters with OpenGL shader filters too
- Input of Hangul and other languages (IME)
- Hi-res replacements for all resources (graphics, fonts, ...)
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
- Source: branch `hires-1` / tag `v8.7.1-hires-1` at https://github.com/cybragon/OpenXcom
- The modification notice and the list of modified files are in `CHANGELOG-hires.md`.
