#pragma once
/*
 * Copyright 2010-2026 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <SDL.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "Unicode.h"

namespace OpenXcom
{

class OpenGL;
class HiResFont;

/**
 * Kind of a deferred draw command of the hi-res overlay layer.
 * Glyph: TTF text. Image: a true-colour picture at output resolution (the filtered magnified UI
 * of ScaledPanel), drawn under the glyphs of the same blit
 * replacement that will be drawn by the same layer later.
 */
enum class HiResCmdKind : Uint8
{
	Glyph = 0,
	Image = 1,
};

/**
 * One deferred draw command. Geometry is always kept in BASE (game, low-res)
 * pixels, relative to the surface that currently holds the command; it is
 * translated/clipped every time the holding surface is blitted, and finally
 * converted to output pixels when the layer is rendered.
 */
struct HiResCmd
{
	/// GLYPH_MARKER: zero-width position in a run (no glyph), laid out with the run; GLYPH_CARET: draw a
	/// text caret there; GLYPH_IMEPOS: report its position (caretPosition, IME candidate window);
	/// GLYPH_UNDERLINE: underline this glyph (IME composition), all in the glyph body colour.
	enum : Uint8 { GLYPH_SPACE = 1, GLYPH_ALIGN_CENTER = 2, GLYPH_ALIGN_RIGHT = 4, GLYPH_NBSP = 8,
		GLYPH_MARKER = 16, GLYPH_CARET = 32, GLYPH_UNDERLINE = 64, GLYPH_IMEPOS = 128 };
	HiResCmdKind kind = HiResCmdKind::Glyph;
	Sint16 x = 0, y = 0;            ///< layout box (base px): glyph advance box / image destination
	Sint16 w = 0, h = 0;
	Sint16 clipX = 0, clipY = 0;    ///< clip rect (base px), same space as x/y
	Sint16 clipW = 0, clipH = 0;
	// --- Glyph payload
	UCode code = 0;                  ///< unicode code point
	Uint32 run = 0;                  ///< text run id (one line of one Text draw); glyphs of a run are laid out together
	Uint32 lineId = 0;               ///< text line id (one line of one Text draw, may hold several runs); 0 = n/a
	Sint16 boxW = 0;                 ///< width of the Text box (base px, holder scale): a line wider than this is squeezed
	Sint16 boxDy = 0, boxH = 0;      ///< top of the Text box relative to y, and its height (base px, holder scale)
	Uint8 cellH = 0;                 ///< font cell height in base px (glyphs are centred on the cell)
	Uint8 lineH = 0;                 ///< font line height in base px (Font.dat height + spacing): glyph size of font types without a size rule
	Uint8 refW = 0;                  ///< advance of U+AC00 in the bitmap font (base px), 0 = n/a (informative, not used for sizing)
	Uint8 fillIdx = 0;               ///< palette index of the glyph body   (font pixel value 1)
	Uint8 lineIdx = 0;               ///< palette index of the glyph outline (font pixel value 4)
	Uint8 slot = 0;                  ///< font slot (one per Font.dat font id, see registerFontId)
	Uint8 flags = 0;                 ///< GLYPH_* flags
	Uint8 scale = 1;                 ///< magnification of the widget that recorded it (ScaledPanel): cell metrics
	                                 ///< (cellH, ink rows) are in widget px, i.e. 'scale' base px each
	Sint8 inkTopL = -1, inkBotL = -1; ///< body rows of 'H' in the bitmap font (Latin size/baseline reference)
	Sint8 inkTopK = -1, inkBotK = -1; ///< body rows of U+AC00 in the bitmap font (Hangul/CJK reference)
	// --- Image payload: picture registered with HiResLayer::setImage (x/y/w/h = where it is shown)
	Uint32 imageId = 0;
	// --- hi-res asset (HiResAssets): the proxy rect srcX/Y/W/H was drawn into the box x/y/w/h
	Uint32 assetId = 0;
	Sint16 srcX = 0, srcY = 0, srcW = 0, srcH = 0;
	Uint32 palKey = 0;               ///< resolved at screen delivery: palette of 8bpp pictures
	Uint32 maskIdx = 0;              ///< resolved at screen delivery: 1 + index of the per-base-pixel snapshot mask (clip rect)
	Uint32 underId = 0;              ///< pixels under the transparent proxy pixels at record time (silhouette edge), 0 = none
	// --- battlescape (stage battle-1): the proxy was drawn shaded/recoloured like the 8bpp map
	Uint8 battle = 0;                ///< 1 = battle draw: the expected pixel is the transformed proxy pixel
	Uint8 layered = 0;               ///< battle draw composited by draw order at output resolution (no per-pixel 8bpp match)
	Uint8 mapLayer = 0;              ///< delivered with the layered map blit (drawn by the GL map pass when active)
	Uint8 clearRect = 0;             ///< layered map start: fill the clip rect with 'fill' (opaque) before the map's draws
	Uint8 nearest = 0;               ///< sprite drawn magnified by nearest neighbour (8bpp overlay of the map)
	Uint32 spriteKey = 0;            ///< 8bpp sprite (as drawn, base px ARGB) kept by HiResLayer::storeSprite, drawn magnified
	Uint32 battleId = 0;             ///< draw id in the map ownership buffer (HiResAssets::battleOwners), 0 = untracked
	Uint8 shade = 0;                 ///< tile shade 0..16 (brightness multiply at draw time)
	Uint8 nvColor = 0;               ///< night vision / marker base colour + 1 (ColorReplace), 0 = none
	Uint8 shadowA = 0;               ///< battle-14 unit shadow piece: opacity 1..255 (0 = not a shadow); drawn translucent,
	                                 ///< clipped to the floor diamond centred at (diaX, diaY) base px (32x16), scale class kept
	Sint16 diaX = 0, diaY = 0;
	Uint8 rcFrom[4] = {0, 0, 0, 0};  ///< recolour source group<<4 per channel (face, hair, utile, rank), 0 = off
	Uint8 rcTo[4] = {0, 0, 0, 0};    ///< recolour target base colour per channel
	Uint32 tint = 0;                 ///< ARGB colour blended over the picture (8bpp highlight of a copied background), 0 = none;
	                                 ///< the proxy pixels are recoloured, so they are not verified (every opaque proxy pixel counts)
	Uint8 invert = 0;                ///< 1 = brightness inverted around 'invertMid' (8bpp Surface::invert of a pressed button);
	                                 ///< like tint, the proxy pixels are recoloured and not verified
	Uint8 invertMid = 0;             ///< luminance of the middle colour of the 8bpp inversion
	// --- resolved when the command reaches the screen buffer
	SDL_Color fill = {0, 0, 0, 0};
	SDL_Color line = {0, 0, 0, 0};
	Uint32 stamp = 0;                ///< id of the screen blit that delivered this command (occlusion)
	const void *origin = nullptr;    ///< surface that propagated this command into its current holder
	Sint16 originX = 0, originY = 0; ///< position 'origin' was blitted at
};

/**
 * "Scaled-resolution drawing layer".
 *
 * Game widgets keep rendering into their 8bpp low-res surfaces exactly as before,
 * except that they may *record* hi-res draw commands instead of drawing some
 * pixels. Commands travel with the pixels: whenever a Surface is blitted onto
 * another one, its commands are translated and clipped into the target. Commands
 * that reach the screen buffer are collected per frame and rendered at the final
 * output resolution after the scaler (OpenGL path, or the software path with a 32bpp output).
 *
 * Occlusion: every blit onto the screen buffer gets an increasing id and stamps
 * the base pixels it covers with non-transparent pixels. A command delivered by
 * blit N is visible on base pixel p iff stamp[p] <= N, i.e. nothing opaque was
 * blitted over it later (popups, cursor...).
 *
 * Palette: colours are resolved from the palette of the surface that delivers the
 * command to the screen (exactly what SDL uses to convert the 8bpp pixels), so they
 * follow palette changes; for an 8bpp screen, the screen palette at flip time is used.
 */
class HiResLayer
{
public:
	/// Global master switch; checked inline by the hooks. False = old code path only.
	static bool recording() { return _recording; }
	/// True if hi-res text is configured (options + font loaded). Drives layout of glyphs missing from bitmap fonts.
	static bool textConfigured() { return _textConfigured; }

	/// Reads options, loads fonts. Call once after SDL/options init.
	static void init();
	/// Re-reads the options (fonts, sizes, master switch) at runtime. Recorded commands are kept,
	/// so font/size changes apply on the next frame; if the master switch flips, all command lists
	/// are dropped and consumeModeChanged() returns true once (the caller must rebuild its states).
	static void reconfigure();
	static bool consumeModeChanged();
	/// True if the overlay wants a 32bpp output surface in the software (non-OpenGL) path.
	static bool wantsTrueColor();
	/// Auto GPU from a software screen filter (Screen sets it): the battlefield log names it.
	static bool autoFromSoftwareFilter;
	/// With a fresh OpenGL context: can the GPU battlefield path run the current filter (FBOs, scale-mask shaders, xBRZ / HQx
	/// shader, GL >= 3.0)? false + reason otherwise (the caller falls back to the software output).
	static bool probeGpuFilter(std::string &reason);
	/// Releases everything.
	static void shutdown();
	/// Called by Screen::resetDisplay: the (new) screen buffer and whether the output path supports the layer.
	static void setScreen(SDL_Surface *screenBuffer, bool supportedOutput);
	/// Called when the GL context was re-created (textures are gone).
	static void onContextLost();

	// --- recording API (used by widgets)
	/// Adds a command to a surface (coordinates relative to it); 'front' = under the older ones.
	static void record(SDL_Surface *holder, const HiResCmd &cmd, bool front = false);
	/// Adds a hi-res asset command (HiResAssets) drawn by the proxy 'origin' into dst; replaces the
	/// previous copy of the same proxy at the same place. dst = screen: delivered (checked) at once.
	static void recordAsset(SDL_Surface *dst, const HiResCmd &cmd, const SDL_Surface *origin);
	/// A hi-res asset proxy 'src' is about to be blitted at (x, y) of dst (8bpp): keeps the destination
	/// pixels under it, so the next recordAsset of that draw knows the background of every proxy pixel.
	static void preBlit(const SDL_Surface *src, const SDL_Surface *dst, int x, int y);
	/// A surface is about to be blitted at (x, y) of dst (8bpp): when it carries asset commands with a
	/// pre-blit snapshot, keeps the destination pixels under it (background where it is transparent).
	static void preCarrier(const SDL_Surface *src, const SDL_Surface *dst, int x, int y);
	/// Surface contents were cleared.
	static void clearSurface(SDL_Surface *holder);
	/// Surface is being destroyed.
	static void forgetSurface(SDL_Surface *holder);
	/// A surface was blitted at (x,y) onto dst. Propagates commands, stamps occlusion if dst is the screen.
	static void onBlit(SDL_Surface *src, SDL_Surface *dst, int x, int y);
	/// The rect (x,y,w,h) of src was magnified 'scale' times into dst (at 0,0), replacing its contents.
	/// Commands are moved along with scaled geometry; their glyphs are later rendered at
	/// scale x output resolution (not magnified from bitmaps).
	/// dst->copy(src): dst now holds the part of src at (fromX, fromY) (Surface::copy). The image
	/// commands of src (hi-res pictures, not text) move along, replacing dst's commands.
	static void onCopy(SDL_Surface *src, SDL_Surface *dst, int fromX, int fromY);
	/// The image commands of holder were recoloured by an 8bpp effect (Surface::offset/offsetBlock):
	/// the pictures are drawn with the ARGB colour 'tint' blended over them instead.
	static void tintSurface(SDL_Surface *holder, Uint32 tint);
	/// The surface was inverted by Surface::invert (mid colour luminance 'midLum'): its pictures are drawn with
	/// their brightness inverted around it; a second inversion restores them.
	static void invertSurface(SDL_Surface *holder, Uint8 midLum);
	static void transferScaled(SDL_Surface *src, int x, int y, int w, int h, SDL_Surface *dst, int scale);

	// --- glyph helpers
	/// Can the hi-res font of this slot draw this code point?
	static bool canRenderGlyph(UCode c, int slot);
	/// Layout advance (base px) for a code point missing from a bitmap font, -1 if unknown.
	static int baseAdvance(UCode c, int lineH, int slot);
	/// Next text run id.
	static Uint32 newRun();
	/// Image commands: a picture id, its straight-alpha ARGB pixels (kept by the caller, must stay
	/// valid until replaced or dropped) and their size.
	static Uint32 newImageId();
	static void setImage(Uint32 id, const Uint32 *argb, int w, int h);
	// --- layered battlescape (all map draws at output resolution, in draw order)
	/// Map::draw start: 'map' is repainted; bg = its fill colour. Records the clear command.
	static void layerBegin(SDL_Surface *map, Uint8 bg);
	/// A recorded map draw wrote these pixels (opaque src pixels, after the blit): they are explained by
	/// a command; any other map pixel that changes later comes from an unrecorded 8bpp draw (overlay).
	static void layerMark(SDL_Surface *dst, const Uint8 *src, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH);
	/// An 8bpp sprite was blitted into the map (after the blit): recorded as a magnified sprite command
	/// built from the map pixels it produced (shade/recolour/script included).
	static void layerSprite8(SDL_Surface *dst, const Uint8 *src, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH);
	/// Layered battlescape active (option on, recording)?
	static bool layered();
	/// the map drawn this frame has a walking / flying unit: its region (map-surface px) is recomposed together with the changes found by the planner
	static void noteWalkRegion(int x, int y, int w, int h);
	static void dropImage(Uint32 id);
	/// Output px per screen base px of the last composed frame (false before the first frame).
	static bool outputScale(double &sx, double &sy);
	/// Is this the (8bpp) screen buffer the overlay follows?
	static bool isScreen(const SDL_Surface *surface);

	// --- per game font (Font.dat id) configuration
	/// Registers a Font.dat font id, returns its slot (stable for the process lifetime).
	static int registerFontId(const std::string &id);
	/// Registered font ids, in slot order.
	static const std::vector<std::string> &fontIds();
	/// Font spec ("" = default chain) and size (%) configured for a font id (from oxceHiResFontMap).
	static void getFontSetting(const std::string &id, std::string &spec, int &sizePct);
	/// Changes the font setting of a font id (writes Options::oxceHiResFontMap, does not reconfigure).
	static void setFontSetting(const std::string &id, const std::string &spec, int sizePct);
	/// Faces actually used by a slot (for display), first = primary.
	static std::vector<std::string> slotFaceNames(int slot);

	// --- frame
	/// Start of a frame (screen buffer was cleared).
	static void beginFrame();
	/// Renders the frame's commands on top of the already drawn OpenGL output.
	static void renderGL(OpenGL *gl, int outW, int outH, int top, int bottom, int left, int right, const SDL_Color *screenPalette);
	/// Renders the frame's commands on top of the already scaled 32bpp software output surface.
	static void renderSoftware(SDL_Surface *out, int top, int bottom, int left, int right);
	/// Position (screen base px, fractional) of the last GLYPH_IMEPOS marker laid out by the renderer and
	/// the line height; false if none was drawn in the last rendered frame.
	static bool caretPosition(double &x, double &y, double &h);
	/// Commands collected for the current frame (debug/tests).
	static const std::vector<HiResCmd> &frameCommands();

private:
	static bool _recording, _textConfigured;
};

}
