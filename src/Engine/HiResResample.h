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
#pragma once
#include <SDL.h>
#include <vector>

namespace OpenXcom
{

/**
 * One resampling rule for every hi-res asset (overlay pictures, native inventory canvas incl. 8-bit art):
 * exact size = copy, downscale = area average, upscale = the interpolation of the selected screen
 * filter (mode(): none = nearest, Scale or any OpenGL output = smooth, HQx = HQX, xBRZ = PIXEL).
 * Per picture HQX is done as PIXEL (xBRZ is alpha aware, hqx is not); the battlescape scale-mask
 * pass uses the real hqx on opaque map pixels.
 * Input straight ARGB, output PREMULTIPLIED ARGB.
 */
namespace HiResResample
{
	enum Mode { SMOOTH = 0, NEAREST = 1, PIXEL = 2, HQX = 3 };
	/// Current upscale mode, from the screen filter.
	int mode();
	/// Source rect (rx, ry, rw, rh; may be fractional) of the w x h straight ARGB picture -> outW x outH premultiplied.
	/// xbrzContext / xbrzClamp (PIXEL only): the xBRZ crop takes up to xbrzContext source cells around the rect, but
	/// not beyond xbrzClamp = {x0, y0, x1, y1} (src cells; the crop edge replicates there). A partial update with the
	/// clamp of the full-size rect then gives the same pixels as the full one (xBRZ reach < 3 cells).
	void resample(const Uint32 *src, int w, int h, double rx, double ry, double rw, double rh, int outW, int outH, int mode, std::vector<Uint32> &out,
		int xbrzContext = 0, const int *xbrzClamp = nullptr);
	/// Straight ARGB -> premultiplied.
	Uint32 premultiply(Uint32 p);
	/// Premultiplied source over a straight-alpha destination (result straight).
	void blendOver(Uint32 &d, Uint32 s);
}

}
