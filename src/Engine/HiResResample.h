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
 * exact size = copy, downscale = area average, upscale = option oxceHiResUpscale
 * (0 smooth/bilinear, 1 nearest, 2 pixel-art: xBRZ ARGB integer pass, then area downscale).
 * Input straight ARGB, output PREMULTIPLIED ARGB.
 */
namespace HiResResample
{
	enum Mode { SMOOTH = 0, NEAREST = 1, PIXEL = 2, AUTO = 3 /* option value only */ };
	/// Current upscale mode (Options::oxceHiResUpscale; AUTO follows the video filter).
	int mode();
	/// Source rect (rx, ry, rw, rh; may be fractional) of the w x h straight ARGB picture -> outW x outH premultiplied.
	void resample(const Uint32 *src, int w, int h, double rx, double ry, double rw, double rh, int outW, int outH, int mode, std::vector<Uint32> &out);
	/// Straight ARGB -> premultiplied.
	Uint32 premultiply(Uint32 p);
	/// Premultiplied source over a straight-alpha destination (result straight).
	void blendOver(Uint32 &d, Uint32 s);
}

}
