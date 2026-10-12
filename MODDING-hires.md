# Hi-res images for modders (8.7.1-hires, geoscape/UI stage)

Option `oxceHiResAssets` (Options > Advanced "hi-res images") draws replacement images at the output
resolution. `oxceHiResUpscale` (Smooth / Nearest / Pixel art) picks how images smaller than the
screen are enlarged; images larger than their box are always area-averaged down.

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
  Map smoke and hand slots stay 8-bit.
- Other battlescape resources (units, terrain, HANDOB/FLOOROB, cursor, ...): converted to 8-bit in
  the battlescape palette (downscaled to the rule size first) with a log warning.
- Hi-res images option **off**: 32-bit/JPG files are downscaled to the rule size and quantized to
  the right palette (battlescape types: indices 1-239) with a log warning, instead of an error.
  Files that cannot be decoded still disable the mod as before.
