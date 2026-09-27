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
#include <algorithm>

namespace OpenXcom
{

/**
 * Placement of the geoscape sidebar (the 64 px wide column of the original 320x200 layout).
 *
 * On tall geoscape resolutions the sidebar keeps its original 320x200 layout but is shown
 * magnified by an integer factor, so that the original 200 px of height take the integer
 * multiple closest to 75% of the screen height. The sidebar widgets then live in a small
 * "logical" panel (64 x height/scale) that is magnified onto the right edge of the screen,
 * while the globe keeps the full geoscape resolution.
 *
 * Scale 1 (heights below 300, e.g. 320x200) is exactly the original layout.
 */
struct GeoSidebarLayout
{
	static const int BASE_WIDTH = 64;    ///< original sidebar width
	static const int BASE_HEIGHT = 200;  ///< original sidebar block height
	static const int MIN_SCREEN_HEIGHT = 300;

	int scale = 1;          ///< magnification factor (1 = original layout)
	int screenW = 0, screenH = 0;
	int logicalW = 0, logicalH = 0; ///< size of the logical panel (scale > 1 only)
	int x = 0, y = 0;       ///< screen position of the magnified panel (scale > 1 only)

	/// Computes the layout for a geoscape resolution (base/game pixels).
	static GeoSidebarLayout compute(int w, int h)
	{
		GeoSidebarLayout l;
		l.screenW = w;
		l.screenH = h;
		int k = 1;
		if (h >= MIN_SCREEN_HEIGHT)
		{
			k = (int)(0.75 * h / BASE_HEIGHT + 0.5);
			k = std::min(k, h / BASE_HEIGHT);          // the original block must fit
			k = std::min(k, w / (BASE_WIDTH * 3));     // keep at least 2/3 of the width for the globe
			k = std::max(k, 1);
		}
		l.scale = k;
		if (k > 1)
		{
			l.logicalW = BASE_WIDTH;
			l.logicalH = h / k;
			l.x = w - BASE_WIDTH * k;
			l.y = (h - l.logicalH * k) / 2;
		}
		return l;
	}
	/// Width of the sidebar on the screen.
	int width() const { return BASE_WIDTH * scale; }
	/// Width left to the globe.
	int globeWidth() const { return screenW - width(); }
	/// True if the sidebar is magnified (drawn through a ScaledPanel).
	bool magnified() const { return scale > 1; }
	/// Size of the space the sidebar widgets are laid out in (screen size, or the logical panel).
	int spaceW() const { return magnified() ? logicalW : screenW; }
	int spaceH() const { return magnified() ? logicalH : screenH; }
	/// Screen rect of a rect given relative to the top-left corner of the original 64x200 block.
	SDL_Rect blockToScreen(int bx, int by, int bw, int bh) const
	{
		SDL_Rect r;
		if (!magnified())
		{
			r.x = (Sint16)(screenW - BASE_WIDTH + bx);
			r.y = (Sint16)(screenH / 2 - BASE_HEIGHT / 2 + by);
			r.w = (Uint16)bw;
			r.h = (Uint16)bh;
		}
		else
		{
			r.x = (Sint16)(x + bx * scale);
			r.y = (Sint16)(y + (logicalH / 2 - BASE_HEIGHT / 2 + by) * scale);
			r.w = (Uint16)(bw * scale);
			r.h = (Uint16)(bh * scale);
		}
		return r;
	}
};

}
