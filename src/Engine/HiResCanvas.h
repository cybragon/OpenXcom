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

namespace OpenXcom
{

class Surface;

/**
 * Full-colour canvas at output resolution for one logical (base px) rect - the "native" drawing
 * path (pilot: InventoryState, follows the hi-res image option).
 *
 * Callers draw in LOGICAL coordinates (the 320x200 game space); the canvas maps them to output
 * pixels. 8-bit surfaces are resolved through their own palette (index 0 transparent) and
 * upscaled (nearest), so palette, shade and script recolour done on the 8bpp path are kept;
 * pictures (hi-res assets, 32-bit images) are drawn as they are, resampled once to the size of
 * their logical rect, never palette-transformed. The result is shown by the hi-res layer as one
 * Image command (present), under the text commands of the same blit.
 *
 * The API is independent of the pixel buffer (a GPU backend can replace it for the battle map).
 */
class HiResCanvas
{
	std::vector<Uint32> _px;      ///< straight ARGB, _outW x _outH
	int _lx = 0, _ly = 0, _lw = 0, _lh = 0;  ///< logical rect covered (base px)
	double _sx = 1.0, _sy = 1.0;  ///< output px per logical px
	int _outW = 0, _outH = 0;
	SDL_Palette *_palette = nullptr; ///< game (state) palette for 8-bit art; nullptr = the surface's own
	Uint32 _imageId = 0;
	struct Clip { int x0, y0, x1, y1; };
	std::vector<Clip> _clips;
	Clip clip() const;
	int ox(double l) const;       ///< output x of logical x (canvas local)
	int oy(double l) const;
	void blend(Uint32 &d, Uint32 s) const;
public:
	HiResCanvas();
	~HiResCanvas();
	/// Starts a frame: logical rect (lx, ly, lw, lh) at sx x sy output px per logical px. Clears it.
	void begin(int lx, int ly, int lw, int lh, double sx, double sy);
	/// Palette of the 8-bit art (the state palette: raw sprites such as vanilla SPK paperdolls carry no real palette).
	void setPalette(SDL_Palette *palette) { _palette = palette; }
	int getOutputWidth() const { return _outW; }
	int getOutputHeight() const { return _outH; }
	/// Clip rect in logical coordinates (intersected with the current one).
	void pushClip(int lx, int ly, int lw, int lh);
	void popClip();
	/// Solid rect (straight ARGB).
	void fillRect(int lx, int ly, int lw, int lh, Uint32 argb);
	/// 8-bit surface with its own palette at logical (lx, ly); index 0 is transparent.
	void drawSurface8(const Surface *surface, int lx, int ly);
	/// A straight-ARGB picture of pw x ph pixels covering the logical rect (lx, ly, lw, lh).
	void drawPicture(const Uint32 *argb, int pw, int ph, int lx, int ly, int lw, int lh);
	/// A surface: its picture (HiResAssets: hi-res asset or native 32-bit picture) if it has one,
	/// otherwise its 8-bit pixels. Returns true if a picture was drawn.
	bool drawSurface(const Surface *surface, int lx, int ly);
	/// Has the surface a picture for the canvas (and its logical size)?
	static bool hasPicture(const Surface *surface, int *lw = nullptr, int *lh = nullptr);
	/// Shows the canvas: records its Image command at the front of 'holder' (coordinates relative
	/// to it), i.e. under every other command the holder carries (texts on top).
	void present(SDL_Surface *holder);
	/// Raw pixels (debug / tests).
	const std::vector<Uint32> &pixels() const { return _px; }
};

}
