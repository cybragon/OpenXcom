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
#include "UiImageScaler.h"
#include "Surface.h"
#include "Options.h"
#include "Screen.h"
#include "Scalers/hqx.h"
#include "Scalers/xbrz.h"
#include "Scalers/scalebit.h"
#include <algorithm>
#include <cmath>

namespace OpenXcom
{

namespace UiImageScaler
{

Filter uiFilter()
{
	if (Options::geoscapeScale != SCALE_SCREEN_UI || Screen::useOpenGL())
		return Filter::Nearest;
	if (Options::useXBRZFilter)
		return Filter::XBRZ;
	if (Options::useHQXFilter)
		return Filter::HQX;
	if (Options::useScaleFilter)
		return Filter::Scale;
	return Filter::Nearest;
}

bool supports(Filter filter, int k)
{
	switch (filter)
	{
	case Filter::XBRZ: return k >= 2 && k <= 6;
	case Filter::HQX: return k >= 2 && k <= 4;
	case Filter::Scale: return k >= 2 && k <= 4;
	default: return true;
	}
}

bool scaleArgb(Filter filter, int k, const uint32_t *src, int w, int h, uint32_t *dst)
{
	if (!supports(filter, k) || w <= 0 || h <= 0)
		return false;
	if (filter == Filter::XBRZ)
	{
		xbrz::scale((size_t)k, src, dst, w, h, xbrz::ARGB);
		return true;
	}
	if (filter == Filter::HQX)
	{
		static bool initDone = false;
		if (!initDone)
		{
			hqxInit();
			initDone = true;
		}
		const uint32_t sp = (uint32_t)(w * 4), dp = (uint32_t)(w * k * 4);
		if (k == 2) hq2x_32_rb(src, sp, dst, dp, w, h);
		else if (k == 3) hq3x_32_rb(src, sp, dst, dp, w, h);
		else hq4x_32_rb(src, sp, dst, dp, w, h);
		return true;
	}
	return false;
}

bool scale8(Filter filter, int k, const uint8_t *src, int srcPitch, int w, int h, uint8_t *dst, int dstPitch)
{
	if (filter != Filter::Scale || !supports(filter, k) || scale_precondition((unsigned)k, 1, (unsigned)w, (unsigned)h) != 0)
		return false;
	scale((unsigned)k, dst, (unsigned)dstPitch, src, (unsigned)srcPitch, 1, (unsigned)w, (unsigned)h);
	return true;
}

double coverFactor(int srcW, int srcH, int dstW, int dstH)
{
	if (srcW <= 0 || srcH <= 0 || (dstW <= srcW && dstH <= srcH))
		return 1.0;
	return std::max(dstW / (double)srcW, dstH / (double)srcH);
}

void blitCover(Surface *src, Surface *dst, Filter filter)
{
	(void)filter; // Filter::Nearest only
	const int sw = src->getWidth(), sh = src->getHeight();
	const int dw = dst->getWidth(), dh = dst->getHeight();
	const double f = coverFactor(sw, sh, dw, dh);
	// top-left of the destination box inside the magnified picture (centred)
	const double ox = (sw * f - dw) / 2.0, oy = (sh * f - dh) / 2.0;
	src->lock();
	dst->lock();
	for (int y = 0; y < dh; ++y)
	{
		const int sy = (int)std::floor((y + oy + 0.5) / f);
		if (sy < 0 || sy >= sh)
			continue;
		for (int x = 0; x < dw; ++x)
		{
			const int sx = (int)std::floor((x + ox + 0.5) / f);
			if (sx < 0 || sx >= sw)
				continue;
			dst->setPixel(x, y, src->getPixel(sx, sy));
		}
	}
	dst->unlock();
	src->unlock();
}

}

}
