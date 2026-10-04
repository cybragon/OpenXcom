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
#include <string>
#include <memory>
#include <vector>

namespace OpenXcom
{

class Surface;
class SurfaceSet;

/**
 * Hi-res replacements of existing images (option oxceHiResAssets, no mod rules).
 *
 * An extraSprites replacement of an existing resource (single image, per-frame file or subX/subY
 * sheet) whose file size differs from the replaced image keeps an 8bpp "proxy" of the ORIGINAL
 * size: all game logic (sizes, hit tests, blits, colour effects) sees exactly that proxy. The
 * replacement picture itself is drawn stretched into the rect of the proxy at output resolution by
 * the HiResLayer, wherever the proxy pixels reach the screen unchanged (snapshot check per base
 * pixel: anything drawn over the proxy, inverted buttons, shaded blits... show the 8bpp pixels).
 *
 * 32bpp pictures (only with the option, only for resources used exclusively by geoscape/base
 * screens) are quantized to the proxy palette for the proxy.
 *
 * Option off: nothing changes (no proxies, every 32bpp image is rejected as before).
 */
namespace HiResAssets
{
	struct Picture;

	/// Number of registered proxies (fast check for the blit hooks).
	extern size_t registeredCount;
	inline bool any() { return registeredCount != 0; }

	/// Option oxceHiResAssets (read at load time: changing it needs a mod reload).
	bool enabled();
	/// 32bpp pictures (option on) are allowed for every resource except battlescape ones: a type
	/// (Mod surface / surface set name) or name prefix marked here (vanilla battlescape resources,
	/// rule-referenced armor sprites / paperdolls / layers, terrain sets, battlescape backgrounds).
	void denyTrueColor(const std::string &type);
	void denyTrueColorPrefix(const std::string &prefix);
	bool trueColorAllowed(const std::string &type);
	size_t deniedCount();
	/// Palettes used to quantize 32bpp pictures when the replaced image has no meaningful palette.
	void setPalettes(const SDL_Color *geoscape, const SDL_Color *basescape, const SDL_Color *battlescape);
	/// Game palettes for the own palette test of 8bpp hi-res pictures (geoscape, basescape, ufopaedia, graphs, battlescape).
	void setGamePalettes(const std::vector<const SDL_Color*> &palettes);
	/// Surface::loadImage, except (option on) a 32bpp picture, which is converted to the battlescape
	/// palette with a warning instead of the fatal error (for the denied battlescape types).
	bool loadDeniedTrueColor(Surface *target, const std::string &filename, const std::string &type);

	/// Decodes an image file: 8bpp exactly like Surface::loadImage, or 32bpp (PNG via lodepng, others via
	/// SDL_image). Returns null if the file cannot be decoded (the normal loader then reports the error).
	std::shared_ptr<Picture> decode(const std::string &filename);
	int pictureWidth(const Picture &p);
	int pictureHeight(const Picture &p);
	bool pictureTrueColor(const Picture &p);
	bool pictureOwnPalette(const Picture &p);

	/**
	 * Size rule of all hi-res cases: an image of w x h for a box of boxW x boxH is "larger" if w > boxW
	 * or h > boxH; it is then drawn at w/scale x h/scale, scale = max(w/boxW, h/boxH) (aspect kept,
	 * the rest of the box empty). Returns false (fw/fh = w/h, scale 1) if the image fits the box.
	 */
	bool fitBox(int boxW, int boxH, int w, int h, int &fw, int &fh, double &scale);
	/**
	 * Consumer box of a (mod-defined) image: an image larger than boxW x boxH is returned as a cached
	 * fitted proxy (fitted size, drawn hi-res); otherwise (or option off) the image itself.
	 */
	Surface *fit(Surface *image, int boxW, int boxH);
	/**
	 * Single image or per-frame file for a box of boxW x boxH (the image it replaces / the set frame size). Returns true if it
	 * was loaded as a hi-res replacement (target becomes the proxy); false = load it the normal way
	 * (option off, same-size 8bpp file, 32bpp not allowed for this type -> rejected as before...).
	 * Logs size mismatches.
	 */
	bool loadReplacement(Surface *target, const std::string &filename, int boxW, int boxH, const SDL_Color *origPalette, const std::string &type);
	/// Logs a size mismatch of a replacement loaded the normal way (option off or not eligible).
	void logMismatch(const std::string &type, const std::string &filename, int origW, int origH, int fileW, int fileH, const char *what);

	/// Builds the proxy of the picture rect (rx, ry, rw, rh) into target (w x h, 8bpp), using
	/// 'palette' to quantize 32bpp pictures.
	void makeProxy(Surface *target, const Picture &pic, double rx, double ry, double rw, double rh, int w, int h, const SDL_Color *palette);
	/// Registers 'proxy' (its current pixels are the expected snapshot) as showing the rect of the picture.
	void attach(Surface *proxy, const std::shared_ptr<Picture> &pic, double rx, double ry, double rw, double rh, const std::string &name);
	/// Quantization palette for a type (see setPalettes).
	const SDL_Color *quantizePalette(const std::string &type, const SDL_Color *origPalette);

	/// A surface is destroyed or its pixel buffer replaced.
	void forget(const SDL_Surface *surface);

	/// Blit hook: the rect (sx, sy, sw, sh) of 'src' was drawn unchanged at (dx, dy) of 'dst'; dw/dh > 0:
	/// it was magnified (pixel-replicated) to dw x dh.
	void onDraw(const SDL_Surface *src, int sx, int sy, int sw, int sh, SDL_Surface *dst, int dx, int dy, int dw = -1, int dh = -1);

	// --- used by HiResLayer
	/// Expected (proxy) pixels of an asset, null if it is gone.
	const Uint8 *expected(Uint32 assetId, int &w, int &h);
	/// Key of the palette an 8bpp picture is resolved with (0 for 32bpp pictures).
	Uint32 paletteKey(Uint32 assetId, const SDL_Palette *palette);
	/// The proxy rect (sx, sy, sw, sh) of an asset resampled to outW x outH output pixels (straight ARGB).
	const Uint32 *rendered(Uint32 assetId, Uint32 palKey, int sx, int sy, int sw, int sh, int outW, int outH);
}

}
