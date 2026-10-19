#pragma once
/*
 * Copyright 2010-2016 OpenXcom Developers.
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
#include <functional>
#include <algorithm>

namespace OpenXcom
{

class Mod;
class Soldier;
class Surface;
class HiResCanvas;
class Screen;

/**
 * The battlescape soldier avatar (modded: "AvatarBackground" defined): background + the paperdoll
 * pieces cropped at avatarOffsetX/Y, 26x23 logical pixels. Shared by the battlescape HUD, the
 * battlescape unit info screen and the base soldier info screen.
 */
namespace AvatarDraw
{
	const int WIDTH = 26, HEIGHT = 23;
	/// Is the avatar mode on (the mod defines AvatarBackground)?
	bool enabled(Mod *mod);
	/// 8bpp: background (fitted) and pieces into dst at (x, y), exactly the stock battlescape drawing.
	void draw8(Mod *mod, Soldier *soldier, Surface *dst, int x = 0, int y = 0);
	/// 8bpp, scaled (nearest) into the w x h rect of dst at (x, y).
	void draw8Scaled(Mod *mod, Soldier *soldier, Surface *dst, int x, int y, int w, int h);
	/// Output resolution: on a begun canvas, the background stretched over the whole logical rect
	/// (lx, ly, 26, 23) first, then the pieces (pictures at output resolution, 8-bit art resampled once).
	void drawNative(Mod *mod, Soldier *soldier, HiResCanvas &canvas, int lx, int ly);
	/// Content key: soldier, armor, look, variant (the caller mixes the output scale in).
	Uint64 contentKey(Mod *mod, Soldier *soldier);
	/// 8bpp for a screen with another palette (base soldier info): drawn with the battlescape palette,
	/// each index mapped to the nearest colour of dstPalette (indices 1-255).
	void draw8ForPalette(Mod *mod, Soldier *soldier, Surface *dst, int x, int y, const SDL_Color *dstPalette);
	/// Nearest copy of an 8bpp surface into the w x h rect of dst at (x, y) (index 0 transparent).
	void blitScaled8(Surface *src, Surface *dst, int x, int y, int w, int h);
	/// Flag surface of the soldier (the SoldierInfoState rule), fitted into boxW x boxH; null = none.
	Surface *flag(Mod *mod, Soldier *soldier, int boxW, int boxH);
	/// Flag surface of the soldier as loaded (not fitted); null = none.
	Surface *flagRaw(Mod *mod, Soldier *soldier);
	/// Width / height of a flag: its real picture (hi-res) or its surface; 0 = none.
	double flagAspect(Surface *flag);
	/**
	 * Flag rect for a slot: height h (the slot height) and the flag's own aspect, at most maxW wide
	 * (then narrower and centred vertically). Writes the size (w x hh) and the y offset in the slot.
	 */
	void flagRect(double aspect, int h, int maxW, int &w, int &hh, int &dy);
	/**
	 * Vertical margins of a flag in a slot of h rows (shared by base soldier info and battle unit info):
	 * h <= 20: none; h = 21: top 1, bottom 0; h >= 22: bottom 1, top 1 (h even) or 2 (h odd).
	 * E.g. 18 -> 18, 20 -> 20, 21 -> 20, 22 -> 20, 23 -> 20 (stock base slot y 4..26 -> 40x20 at y 6), 24 -> 22, 25 -> 22, 26 -> 24.
	 */
	inline void flagSlot(int h, int &top, int &flagH)
	{
		if (h <= 20) { top = 0; flagH = std::max(1, h); }
		else if (h == 21) { top = 1; flagH = 20; }
		else { top = (h % 2) ? 2 : 1; flagH = h - 1 - top; }
	}
	/// Is the output-resolution path on (hi-res layer recording + hi-res assets)?
	bool nativeActive();
	/**
	 * Output scale = the screen's scale (output px per base px; the battle UI is not magnified).
	 * Output-resolution image on 'holder' (whole surface): 'draw' paints a canvas in a logical space of
	 * vw x vh, shown scaled to the holder size. Redrawn only when key (+ output scale) changes; the
	 * image command is (re)recorded on the holder (after clearing its other commands) when redrawn or force.
	 * Returns false when the native path is off (the 8bpp holder is shown as is).
	 */
	bool presentNative(const Screen *screen, HiResCanvas *&canvas, Uint64 &lastKey, Uint64 key, Surface *holder, int vw, int vh, bool force,
		const std::function<void(HiResCanvas&)> &draw, SDL_Palette *artPalette = nullptr);
}

}
