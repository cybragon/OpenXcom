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
#pragma once
#include <cstdint>

namespace OpenXcom
{

class Surface;

/**
 * Scaling of UI pictures (8bpp surfaces): the space background cover fit and the software
 * filters used by the magnified UI (ScaledPanel) in the geoscape scale "x1, UI optimized".
 */
namespace UiImageScaler
{
	/// Nearest = plain pixel blocks; Scale = Scale2x/3x/4x (8bpp, palette indices);
	/// HQX = hq2x/3x/4x and XBRZ = 2xBRZ..6xBRZ (32bpp, with alpha).
	enum class Filter { Nearest, Scale, HQX, XBRZ };

	/// Software filter for the magnified UI, from the video options: only in the geoscape scale
	/// "x1, UI optimized" and the software (non-OpenGL) output path, else Nearest.
	Filter uiFilter();
	/// Can 'filter' magnify by 'k' (Nearest: any k)?
	bool supports(Filter filter, int k);
	/// True-colour filters (HQX, XBRZ): magnify a w x h ARGB picture (alpha 0 = transparent) k times
	/// into 'dst' (k*w x k*h). Returns false (dst untouched) if the filter cannot do it.
	bool scaleArgb(Filter filter, int k, const uint32_t *src, int w, int h, uint32_t *dst);
	/// Palette filter (Scale): magnify w x h 8bpp pixels k times. Returns false if it cannot.
	bool scale8(Filter filter, int k, const uint8_t *src, int srcPitch, int w, int h, uint8_t *dst, int dstPitch);

	/// Factor that makes a srcW x srcH picture cover a dstW x dstH box (aspect kept), or 1.0 when
	/// the picture already covers the box in both directions (it is then only centred).
	double coverFactor(int srcW, int srcH, int dstW, int dstH);

	/// Draws 'src' on the whole of 'dst': magnified to cover it (aspect kept, centred, the
	/// overflowing edges cropped) when 'dst' is larger than 'src' in either direction, else centred.
	void blitCover(Surface *src, Surface *dst, Filter filter = Filter::Nearest);
}

}
