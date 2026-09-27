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
#include "UiImageScaler.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace OpenXcom
{

ScaledPanel::ScaledPanel(int w, int h, int scale, int x, int y) : ScaledPanel(w, h, 0, 0, w, h, scale, x, y)
{
}

ScaledPanel::ScaledPanel(int canvasW, int canvasH, int srcX, int srcY, int srcW, int srcH, int scale, int x, int y) :
	_scale(std::max(1, scale)), _x(x), _y(y), _paletteSet(false), _imageId(0), _imageHash(0), _imageFilter(-1)
{
	_canvas = new Surface(canvasW, canvasH, 0, 0);
	// the source rect must lie inside the canvas
	_srcX = std::max(0, std::min(srcX, canvasW - 1));
	_srcY = std::max(0, std::min(srcY, canvasH - 1));
	_srcW = std::max(1, std::min(srcW, canvasW - _srcX));
	_srcH = std::max(1, std::min(srcH, canvasH - _srcY));
	_scaled = new Surface(_srcW * _scale, _srcH * _scale, x, y);
	std::memset(_palette, 0, sizeof(_palette));
}

bool ScaledPanel::matches(int canvasW, int canvasH, int srcX, int srcY, int srcW, int srcH, int scale, int x, int y) const
{
	return _canvas->getWidth() == canvasW && _canvas->getHeight() == canvasH && _srcX == srcX && _srcY == srcY &&
		_srcW == srcW && _srcH == srcH && _scale == std::max(1, scale) && _x == x && _y == y;
}

ScaledPanel::~ScaledPanel()
{
	if (_imageId)
		HiResLayer::dropImage(_imageId);
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

void ScaledPanel::magnify8(int filter)
{
	SDL_Surface *src = _canvas->getSurface();
	SDL_Surface *dst = _scaled->getSurface();
	const int k = _scale;
	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	const Uint8 *s0 = (const Uint8*)src->pixels + _srcY * src->pitch + _srcX;
	if (!UiImageScaler::scale8((UiImageScaler::Filter)filter, k, s0, src->pitch, _srcW, _srcH, (Uint8*)dst->pixels, dst->pitch))
	{
		for (int y = 0; y < _srcH; ++y)
		{
			const Uint8 *sp = s0 + y * src->pitch;
			Uint8 *row = (Uint8*)dst->pixels + (y * k) * dst->pitch;
			Uint8 *dp = row;
			for (int x = 0; x < _srcW; ++x)
			{
				std::memset(dp, sp[x], k);
				dp += k;
			}
			for (int r = 1; r < k; ++r)
				std::memcpy(row + r * dst->pitch, row, _srcW * k);
		}
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
}

bool ScaledPanel::updateImage(int filter)
{
	const int k = _scale;
	if (!UiImageScaler::supports((UiImageScaler::Filter)filter, k))
		return false;
	SDL_Surface *src = _canvas->getSurface();
	// content hash (FNV-1a over the pixels, palette and parameters)
	uint64_t hsh = 1469598103934665603ull;
	auto mix = [&](const void *p, size_t n) { const Uint8 *b = (const Uint8*)p; for (size_t i = 0; i < n; ++i) { hsh ^= b[i]; hsh *= 1099511628211ull; } };
	SDL_LockSurface(src);
	for (int y = 0; y < _srcH; ++y)
		mix((const Uint8*)src->pixels + (y + _srcY) * src->pitch + _srcX, _srcW);
	mix(_palette, sizeof(_palette));
	int params[3] = { filter, k, _srcW * 65536 + _srcH };
	mix(params, sizeof(params));
	if (_imageId && hsh == _imageHash && filter == _imageFilter && !_image.empty())
	{
		SDL_UnlockSurface(src);
		return true;
	}
	_imageSrc.resize((size_t)_srcW * _srcH);
	for (int y = 0; y < _srcH; ++y)
	{
		const Uint8 *sp = (const Uint8*)src->pixels + (y + _srcY) * src->pitch + _srcX;
		Uint32 *dp = &_imageSrc[(size_t)y * _srcW];
		for (int x = 0; x < _srcW; ++x)
		{
			const SDL_Color &c = _palette[sp[x]];
			dp[x] = sp[x] ? (0xFF000000u | ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b) : 0u; // index 0 = transparent
		}
	}
	SDL_UnlockSurface(src);
	_image.resize((size_t)_srcW * k * _srcH * k);
	if (!UiImageScaler::scaleArgb((UiImageScaler::Filter)filter, k, _imageSrc.data(), _srcW, _srcH, _image.data()))
		return false;
	if (!_imageId)
		_imageId = HiResLayer::newImageId();
	HiResLayer::setImage(_imageId, _image.data(), _srcW * k, _srcH * k);
	_imageHash = hsh;
	_imageFilter = filter;
	return true;
}

void ScaledPanel::present(SDL_Surface *screen)
{
	// the filter does not depend on the hi-res text option: Scale works on the 8bpp pixels, HQX/xBRZ
	// keep the overlay layer active (HiResLayer::uiImagesWanted) even when hi-res text is off
	const UiImageScaler::Filter filter = (_scale > 1 && HiResLayer::isScreen(screen)) ? UiImageScaler::uiFilter() : UiImageScaler::Filter::Nearest;
	const bool trueColor = filter == UiImageScaler::Filter::HQX || filter == UiImageScaler::Filter::XBRZ;
	// the 8bpp magnification stays: it is what the screen buffer holds (occlusion, screenshots of
	// the base buffer, fallback); true-colour filters are drawn over it by the overlay
	magnify8((int)(trueColor ? UiImageScaler::Filter::Nearest : filter));
	SDL_Surface *src = _canvas->getSurface();
	SDL_Surface *dst = _scaled->getSurface();
	if (HiResLayer::recording())
	{
		// the overlay commands follow the magnification (drawn at output resolution, not magnified)
		HiResLayer::transferScaled(src, _srcX, _srcY, _srcW, _srcH, dst, _scale);
		if (trueColor && updateImage((int)filter))
		{
			HiResCmd img;
			img.kind = HiResCmdKind::Image;
			img.imageId = _imageId;
			img.x = 0; img.y = 0;
			img.w = (Sint16)dst->w; img.h = (Sint16)dst->h;
			img.clipX = 0; img.clipY = 0;
			img.clipW = (Sint16)dst->w; img.clipH = (Sint16)dst->h;
			HiResLayer::record(dst, img);
		}
	}
	_scaled->setX(_x);
	_scaled->setY(_y);
	_scaled->blit(screen);
}

Action ScaledPanel::mapAction(const Action *action) const
{
	// logical = (screen - panelPos) / scale + srcPos, with screen = (window - band) / screenScale
	const double sx = action->getXScale(), sy = action->getYScale();
	const int left = action->getLeftBlackBand() + (int)std::lround(_x * sx - _srcX * sx * _scale);
	const int top = action->getTopBlackBand() + (int)std::lround(_y * sy - _srcY * sy * _scale);
	return Action(action->getDetails(), sx * _scale, sy * _scale, top, left);
}

}
