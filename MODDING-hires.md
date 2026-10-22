# Hi-res images for modders (8.7.1-hires-3, battle-15)

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
Since battle-13 almost every picture may be 32-bit; only a short deny-list keeps 8-bit images.
- Geoscape, basescape, Ufopaedia, menus, UI chrome: allowed (drawn true-colour).
- Battlescape UI (TAC00/TAC01.SCR, ICONS, UNIBORD, SCANBORD, DETBORD(2), MEDIBORD, TinyRanks, and the
  `backgroundImage` / `altBackgroundImage` a mod sets for the battlescape, inventory, medikit, minimap and stats
  interfaces): allowed, battlescape-palette proxy. OXCE battle-only pictures added by a mod (reactionIndicator,
  noReactionIndicator, oxceLinks, TFTDReserve, Big*Indicator, InvCopy/InvPaste, AlienInventory...) also get a
  battlescape-palette proxy.
- Battle map (**Hi-res: all**): unit sheets, terrain, SMOKE.PCK, X1.PCK, HIT.PCK, Projectiles, and since battle-14
  **CURSOR.PCK, Pathfinding, FLOOROB.PCK, HANDOB.PCK** and the floor indicators (FloorStun/Wound/Burn/ShockIndicator,
  the enviro `mapShockIndicator`): drawn at output resolution, shaded like the 8-bit sprite. See "Map markers" and "Items".
- Shared sets **BIGOBS.PCK, SMOKE.PCK (ranks), DETBLOB.DAT**: the map and shaded draws use a battlescape-palette
  8-bit proxy, the UI / native inventory / battle hand slots the picture.
- Since battle-15 **MEDIBITS.DAT** (medikit body parts) and **SCANG.DAT** (minimap) may be 32-bit too (see
  "Medikit body parts and minimap"). With Hi-res **Geoscape** (battle map off) the battle map sheets above are
  downscaled to the rule size and quantized to the battlescape palette with a log warning.
- Hi-res images option **off**: 32-bit/JPG files are downscaled to the rule size and quantized to
  the right palette (battlescape types: indices 1-239) with a log warning, instead of an error.
  Files that cannot be decoded still disable the mod as before.

## Map markers: 3D cursor, path arrows, waypoints (battle-14)
- **CURSOR.PCK** (32x40 frames): the game picks a frame per state and colour (red / yellow / blue box back and front
  parts, red / yellow crosshairs, throw / psi / waypoint markers - keep the frame order of the original sheet). These
  frames are **not recoloured**: draw each frame in its own colour, like the original.
- **Pathfinding** (12x2 frames of 32x40: path arrows by direction, row 2 the arrows of the "turn" / end markers,
  frames 11 and 23 the tile markers): these are recoloured at draw time by the TU state / marker colour (green = this
  turn, yellow = next turns, red = not enough TU, ...). The 8-bit sprite swaps the colour ramp (high nibble) and keeps the
  brightness (low nibble). A 32-bit picture is tinted the same way: its **brightness** is mapped onto the target
  colour ramp (rampTint), its own hue is dropped. So **draw the arrows grey** (light = bright end of the ramp, dark
  = dark end); coloured art loses its colour and only its brightness remains. Alpha works (soft edges).
- Other map markers: the yellow arrow over the selected unit is a fixed 9x9 pattern in the code (not a picture);
  waypoint and TU numbers are bitmap digits. Floor indicators of unconscious units (FloorStunIndicator etc., 32x40,
  `extraSprites` singleImage) may be 32-bit; they are shaded with the tile.
- Same sheet layout and size rules as other sheets: `width`/`height` logical, file at one uniform scale (4x = 128x160 cells).

## Items on the map: FLOOROB.PCK, HANDOB.PCK (battle-14)
- 32x40 frames, same layout as the original sheets; 32-bit files at a uniform scale (4x recommended).
- Shaded by the tile light. Corpse / carried items recolour the face, hair, utile and rank groups of their unit
  like 32-bit unit sheets: optional `<file>_mask.png` of the same size (red face, green hair, white utile, blue rank),
  the masked pixels get the unit's colour by brightness.
- An item with its own (non-default) `recolorItemSprite` script: the script works on colour indices and cannot be run on
  32-bit art. Its 32-bit map pictures are drawn **shaded only** (log warning once per item). In the inventory and the
  battle hand slots (BIGOBS) a 32-bit picture takes the **brightness** the script gives the 8-bit proxy (a darkening
  script still darkens; hue changes are lost).
- 8-bit FLOOROB frames (vanilla) now go through the battle filter like other map sprites (no more nearest blocks).

## Unit shadows (battle-14)
- Option `oxceHiResUnitShadow` in hires_option.cfg: opacity % (default 60, 0 = off). Only with **Hi-res: all** and
  `oxceHiResBattleLayered: true` (default). Off = the map looks exactly as before.
- One soft shadow per visible unit, positioned with the unit's walking offset: it moves with the sprite pixel by pixel
  (walking, stairs, falling). It is drawn right after each floor it touches, clipped to that floor's diamond, so walls,
  objects and units drawn later cover it, and floors in front do not cut it.
- Stairs and ramps (battle-15): on a raised tile (terrain level from a back object such as a stair) the shadow is drawn
  after that object, at the stair's height, and a shadow is only cast on tiles whose surface height is within 4 px of
  the unit's (no pieces cut onto the flat tiles around a stair, or onto a stair next to a unit on flat ground).
- Strength = option x armor opacity x floor brightness of each tile (lit 100% = full, dark = none): a unit in the dark
  casts no visible shadow.
- Hovering / flying units over empty space: the shadow falls on the first floor below (up to 2 levels), 50% weaker and
  80% smaller per level; nothing below = no shadow.
- Pictures (engine defaults, procedural soft black ellipses): `HiResShadowSmall` (1x1 units, 22x11 logical) and
  `HiResShadowLarge` (2x2 units, 44x22). Replace them with `extraSprites` of the same name (`singleImage: true`,
  `width`/`height` = logical size, any larger 32-bit file with alpha). The shadow is centred on the unit's feet
  (1x1: tile floor centre; 2x2: centre of the four tiles).
- Armor keys:
  ```yaml
  armors:
    - type: STR_MY_HWP_ARMOR
      hiResShadow: MyHwpShadow        # any extraSprites single image; default by size (HiResShadowSmall / HiResShadowLarge)
      hiResShadowOffset: [0, 2]       # logical px (x, y), default [0, 0]
      hiResShadowOpacity: 80          # % of oxceHiResUnitShadow, default 100
      hiResShadowFlying: false        # no shadow while hovering over empty space, default true
  ```
  A humanoid 2x shadow for a 2x2 unit: `hiResShadow: HiResShadowLarge` (the default for size 2 anyway).

## Medikit body parts and minimap (battle-15)
- `MEDIBITS.DAT`: 6 frames of 52x58 (head, torso, right arm, left arm, right leg, left leg), each frame the whole view
  with only its part opaque (as the original). The game recolours a wounded part by colour group (interface `medikit`,
  element `body`: `color` = healthy, `color2` = wounded; 0 = no recolour). A 32-bit part is drawn as is when its colour
  is 0, otherwise the middle colour of that palette group (index group*16+6) is blended over it at 63%: draw the parts
  green (healthy look) with light details; wounded parts then read as the wound colour. Clicks still use the 8-bit proxy
  (opaque pixels), so keep the parts' outlines at the rule size.
- `SCANG.DAT`: 4x4 frames (the original has 454: units 0-8 with 3 animation frames, items 9-11 ..., map tiles from 35 on via
  the terrain `miniMapIndex`). A 32-bit cell is drawn at output resolution; the tile darkness of the minimap (shade) is
  black blended over it, the red recolour of large neutral units / primed grenades the red group colour. Example sheet:
  `width: 1816, height: 4, subX: 4, subY: 4` with a 7264x16 file (4x).
- The inventory primer pulse and the battlescape HUD markers that also use SCANG frame 6 keep their 8-bit proxy.

## Pressed battlescape buttons (battle-15)
- Reserve / kneel buttons are inverted when selected. OXCE draws `TFTDReserve` (59x24) and `KneelButton` over ICONS:
  with the stock 8-bit pictures the selected button shows the inverted 8-bit pixels (as before battle-13); give a 32-bit
  `TFTDReserve` / `KneelButton` (battlescape-palette proxy) to get the hi-res picture inverted. A 32-bit ICONS picture
  hidden under those 8-bit overlays no longer shows through in the pressed state.

## Slideshows (intro / ending, battle-14)
`videos:` slideshow slides (`imagePath`) may be hi-res: a picture larger than 320x200, or a 32-bit picture of exactly
320x200, is drawn at output resolution. A 32-bit slide uses the geoscape palette for its 8-bit proxy and the caption
colours; 8-bit 320x200 slides load as before.

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
