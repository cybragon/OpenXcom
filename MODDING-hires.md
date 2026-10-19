# Hi-res images for modders (8.7.1-hires-2, geoscape/UI stage)

Options > Video, hi-res dropdown: **Hi-res off (original)** / **Hi-res on** (`oxceHiResAssets`, in
user/hires_option.cfg). Upscaling follows the screen filter: none -> nearest, Scale -> smooth, HQx / xBRZ -> that filter;
with the OpenGL output `oxceHiResBattleFilter` in hires_option.cfg decides (0 none, 1 Scale/smooth = default, 2 HQx, 3 xBRZ; GPU shader when OpenGL 3.0+, CPU resampler otherwise - same filter). Images larger than their box are always area-averaged down.
Switching hi-res on/off reloads the mods from the main menu (in a game: after a restart). The Video tooltip is short on purpose; this file has the details.

## width / height = logical size
`extraSprites` keeps its normal rules. `width`/`height` (or the size of the replaced image) is the
**logical** size in 320x200 units. A file larger than that (e.g. 4x) is drawn hi-res in that box;
the game itself sees an 8-bit proxy of exactly the logical size.

## Sheets (subX / subY)
The whole file is compared with `width` x `height`; a larger file is scaled by one factor and cut
into `subX` x `subY` cells on that scaled grid. Example: ranks 192x40 rule, 32x40 cells, a 768x160
file -> six 128x160 hi-res cells, each a 32x40 frame.

## 32-bit PNG / JPG: where allowed
- Geoscape, basescape, Ufopaedia, menus, UI chrome: allowed (drawn true-colour).
- Battlescape UI chrome: TAC00/TAC01.SCR, ICONS, UNIBORD, SCANBORD, DETBORD(2), MEDIBORD, TinyRanks.
- Shared battlescape sets **BIGOBS.PCK, SMOKE.PCK, DETBLOB.DAT**: allowed; the map and shaded draws
  use a battlescape-palette 8-bit proxy (indices 1-239), the UI / native inventory the picture.
  Hand slots stay 8-bit. Map smoke / fire / explosions: see "Battle map smoke" below.
- Other battlescape resources (units, terrain, HANDOB/FLOOROB, cursor, ...): converted to 8-bit in
  the battlescape palette (downscaled to the rule size first) with a log warning.
- Hi-res images option **off**: 32-bit/JPG files are downscaled to the rule size and quantized to
  the right palette (battlescape types: indices 1-239) with a log warning, instead of an error.
  Files that cannot be decoded still disable the mod as before.

## Battle map smoke, fire and explosions (32-bit, translucent)
With **Hi-res: all** and `oxceHiResBattleLayered: true` (default), 32-bit sheets for `SMOKE.PCK` and `X1.PCK`
are drawn with their own alpha on the battle map (soft, translucent smoke over units and terrain, no dither).
- **SMOKE.PCK**: frame 32x40 logical. Vanilla order: 0-3 tile fire (+ animation), 4-7 burning unit,
  8.. smoke (`smokeOffset`, default 8; `underwaterSmokeOffset` for TFTD depth), density step =
  `floor(smoke/6 - 0.1)` (0-2) plus animation 0-3; `extendedSmokeOffset: 1` = 3 densities x 4 frames (8-19),
  `2` = `(smoke-1)/5*4`. Small hits/explosions use item `hitAnimation` frames from the same set; rank badges
  (`getRankSpriteBattlescape`) also come from it. A sheet replaces only the frames it covers: keep the
  rank and hit frames (or replace them with suitable 32-bit art). Smoke is shaded by the tile light; fire and hits are not.
- **X1.PCK** (big explosions, `explosionOffset`, items with radius `hitAnimation`): frame 128x64 logical,
  vanilla 8 frames, drawn centred on the explosion point, unshaded.
- Alpha: straight (non-premultiplied) PNG / WebP alpha; 0 = clear, 255 = opaque, anything between is
  blended over what is drawn before it in map order (the same order as the 8-bit draw: smoke after the unit
  of its tile, so it covers that unit and lies under tiles drawn later).
- Size / scale: `width`/`height` = frames x frame size in logical pixels (e.g. 20 smoke frames:
  640x40, `subX: 32`, `subY: 40`); the file is the same layout at one uniform scale (2x = 1280x80, 4x =
  2560x160 ...), cut at that scale. Any scale works; integer scales (2x, 4x) are recommended. JPG has no alpha (opaque).
- Hi-res off (or Geoscape level): the sheet is downscaled and quantized to the battlescape palette (opaque
  8-bit, no translucency). Non-layered battle hi-res (`oxceHiResBattleLayered: false`): drawn
  hi-res but not translucent (the 8-bit proxy decides coverage).
