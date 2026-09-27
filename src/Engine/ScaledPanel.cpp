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
#include "ScaledPanel.h"
#include "Surface.h"
#include "Action.h"
#include "HiResLayer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace OpenXcom
{

ScaledPanel::ScaledPanel(int w, int h, int scale, int x, int y) : _scale(std::max(1, scale)), _x(x), _y(y), _paletteSet(false)
{
	_canvas = new Surface(w, h, 0, 0);
	_scaled = new Surface(w * _scale, h * _scale, x, y);
	std::memset(_palette, 0, sizeof(_palette));
}

ScaledPanel::~ScaledPanel()
{
	delete _canvas;
	delete _scaled;
}

void ScaledPanel::add(Surface *surface)
{
	if (!contains(surface))
		_members.push_back(surface);
}

bool ScaledPanel::contains(const Surface *surface) const
{
	return std::find(_members.begin(), _members.end(), surface) != _members.end();
}

void ScaledPanel::beginFrame(const SDL_Color *palette)
{
	// 8bpp -> 8bpp blits are identity copies only while all palettes are identical
	if (palette && (!_paletteSet || std::memcmp(palette, _palette, sizeof(_palette)) != 0))
	{
		std::memcpy(_palette, palette, sizeof(_palette));
		_canvas->setPalette(_palette, 0, 256);
		_scaled->setPalette(_palette, 0, 256);
		_paletteSet = true;
	}
	_canvas->clear();
}

void ScaledPanel::blitMember(Surface *surface)
{
	surface->blit(_canvas->getSurface());
}

void ScaledPanel::present(SDL_Surface *screen)
{
	SDL_Surface *src = _canvas->getSurface();
	SDL_Surface *dst = _scaled->getSurface();
	const int k = _scale;
	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	for (int y = 0; y < src->h; ++y)
	{
		const Uint8 *sp = (const Uint8*)src->pixels + y * src->pitch;
		Uint8 *row = (Uint8*)dst->pixels + (y * k) * dst->pitch;
		Uint8 *dp = row;
		for (int x = 0; x < src->w; ++x)
		{
			std::memset(dp, sp[x], k);
			dp += k;
		}
		for (int r = 1; r < k; ++r)
			std::memcpy(row + r * dst->pitch, row, src->w * k);
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	if (HiResLayer::recording())
	{
		// the overlay commands follow the magnification (drawn at output resolution, not magnified)
		HiResLayer::transferScaled(src, 0, 0, src->w, src->h, dst, k);
	}
	_scaled->setX(_x);
	_scaled->setY(_y);
	_scaled->blit(screen);
}

Action ScaledPanel::mapAction(const Action *action) const
{
	// logical = (screen - panelPos) / scale, with screen = (window - band) / screenScale
	const double sx = action->getXScale(), sy = action->getYScale();
	const int left = action->getLeftBlackBand() + (int)std::lround(_x * sx);
	const int top = action->getTopBlackBand() + (int)std::lround(_y * sy);
	return Action(action->getDetails(), sx * _scale, sy * _scale, top, left);
}

}
