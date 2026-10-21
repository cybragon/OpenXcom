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
#include "HiResCanvas.h"
#include "HiResLayer.h"
#include "HiResAssets.h"
#include "HiResResample.h"
#include <list>
#include <vector>
#include "Surface.h"
#include <algorithm>
#include <cmath>

namespace OpenXcom
{

HiResCanvas::HiResCanvas()
{
}

HiResCanvas::~HiResCanvas()
{
	if (_imageId)
		HiResLayer::dropImage(_imageId);
}

int HiResCanvas::ox(double l) const { return (int)std::lround((l - _lx) * _sx); }
int HiResCanvas::oy(double l) const { return (int)std::lround((l - _ly) * _sy); }

HiResCanvas::Clip HiResCanvas::clip() const
{
	return _clips.empty() ? Clip{ 0, 0, _outW, _outH } : _clips.back();
}

void HiResCanvas::begin(int lx, int ly, int lw, int lh, double sx, double sy)
{
	_lx = lx; _ly = ly; _lw = std::max(1, lw); _lh = std::max(1, lh);
	_sx = sx > 0 ? sx : 1.0; _sy = sy > 0 ? sy : 1.0;
	const int w = std::max(1, (int)std::lround(_lw * _sx)), h = std::max(1, (int)std::lround(_lh * _sy));
	if (w != _outW || h != _outH)
	{
		_outW = w; _outH = h;
		_px.assign((size_t)w * h, 0);
	}
	else
	{
		std::fill(_px.begin(), _px.end(), 0u);
	}
	_clips.clear();
}

void HiResCanvas::pushClip(int lx, int ly, int lw, int lh)
{
	Clip c = clip();
	c.x0 = std::max(c.x0, ox(lx)); c.y0 = std::max(c.y0, oy(ly));
	c.x1 = std::min(c.x1, ox(lx + lw)); c.y1 = std::min(c.y1, oy(ly + lh));
	_clips.push_back(c);
}

void HiResCanvas::popClip()
{
	if (!_clips.empty())
		_clips.pop_back();
}

void HiResCanvas::blend(Uint32 &d, Uint32 s) const
{
	// premultiplied source "over" a straight-alpha canvas pixel
	HiResResample::blendOver(d, s);
}

void HiResCanvas::fillRect(int lx, int ly, int lw, int lh, Uint32 argb)
{
	const Clip c = clip();
	const int x0 = std::max(c.x0, ox(lx)), y0 = std::max(c.y0, oy(ly));
	const int x1 = std::min(c.x1, ox(lx + lw)), y1 = std::min(c.y1, oy(ly + lh));
	const Uint32 pm = HiResResample::premultiply(argb);
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			blend(_px[(size_t)y * _outW + x], pm);
}

namespace
{
/// 8-bit art resampled to output pixels (index domain already recoloured/shaded by the caller, then the
/// palette, then the upscale filter), most recently used first, bounded.
struct Art8
{
	size_t key;
	int outW, outH;
	std::vector<Uint32> px;
};
std::list<Art8> art8Cache;
size_t art8Bytes = 0;
const size_t ART8_LIMIT = 64u * 1024u * 1024u;
}

void HiResCanvas::drawSurface8(const Surface *surface, int lx, int ly)
{
	if (!surface || !const_cast<Surface*>(surface)->getSurface())
		return;
	SDL_Surface *s = const_cast<Surface*>(surface)->getSurface();
	if (s->format->BitsPerPixel != 8 || !s->format->palette)
		return;
	const SDL_Palette *spal = _palette ? _palette : s->format->palette;
	const int dx0 = ox(lx), dy0 = oy(ly), dx1 = ox(lx + s->w), dy1 = oy(ly + s->h);
	const int ow = dx1 - dx0, oh = dy1 - dy0;
	if (ow <= 0 || oh <= 0 || s->w <= 0 || s->h <= 0)
		return;
	const SDL_Color *pal = spal->colors;
	Uint32 lut[256];
	for (int i = 0; i < 256; ++i)
		lut[i] = i ? (0xFF000000u | ((Uint32)pal[i].r << 16) | ((Uint32)pal[i].g << 8) | pal[i].b) : 0u;
	std::vector<Uint32> straight((size_t)s->w * s->h);
	size_t key = 1469598103934665603ull;
	auto mix = [&](size_t v) { key = (key ^ v) * 1099511628211ull; };
	SDL_LockSurface(s);
	for (int y = 0; y < s->h; ++y)
	{
		const Uint8 *row = (const Uint8*)s->pixels + y * s->pitch;
		for (int x = 0; x < s->w; ++x)
		{
			straight[(size_t)y * s->w + x] = lut[row[x]];
			mix(lut[row[x]]);
		}
	}
	SDL_UnlockSurface(s);
	const int mode = HiResResample::mode();
	mix((size_t)s->w); mix((size_t)s->h); mix((size_t)ow); mix((size_t)oh); mix((size_t)mode + 7);
	const std::vector<Uint32> *px = nullptr;
	for (auto it = art8Cache.begin(); it != art8Cache.end(); ++it)
	{
		if (it->key == key && it->outW == ow && it->outH == oh)
		{
			if (it != art8Cache.begin())
				art8Cache.splice(art8Cache.begin(), art8Cache, it);
			px = &art8Cache.front().px;
			break;
		}
	}
	if (!px)
	{
		Art8 a{ key, ow, oh, {} };
		HiResResample::resample(straight.data(), s->w, s->h, 0, 0, s->w, s->h, ow, oh, mode, a.px);
		art8Bytes += a.px.size() * sizeof(Uint32);
		art8Cache.push_front(std::move(a));
		while (art8Bytes > ART8_LIMIT && art8Cache.size() > 1)
		{
			art8Bytes -= art8Cache.back().px.size() * sizeof(Uint32);
			art8Cache.pop_back();
		}
		px = &art8Cache.front().px;
	}
	if (px->size() == (size_t)ow * oh)
		drawPicture(px->data(), ow, oh, lx, ly, s->w, s->h);
}

void HiResCanvas::drawPicture(const Uint32 *argb, int pw, int ph, int lx, int ly, int lw, int lh)
{
	if (!argb || pw <= 0 || ph <= 0 || lw <= 0 || lh <= 0)
		return;
	const int dx0 = ox(lx), dy0 = oy(ly), dx1 = ox(lx + lw), dy1 = oy(ly + lh);
	const int dw = dx1 - dx0, dh = dy1 - dy0;
	if (dw <= 0 || dh <= 0)
		return;
	const Clip c = clip();
	const int x0 = std::max(c.x0, dx0), y0 = std::max(c.y0, dy0), x1 = std::min(c.x1, dx1), y1 = std::min(c.y1, dy1);
	for (int y = y0; y < y1; ++y)
	{
		const int iy = std::min(ph - 1, (int)((long long)(y - dy0) * ph / dh));
		const Uint32 *src = argb + (size_t)iy * pw;
		Uint32 *dst = &_px[(size_t)y * _outW];
		for (int x = x0; x < x1; ++x)
		{
			const int ix = std::min(pw - 1, (int)((long long)(x - dx0) * pw / dw));
			blend(dst[x], src[ix]);
		}
	}
}

bool HiResCanvas::hasPicture(const Surface *surface, int *lw, int *lh)
{
	int w = 0, h = 0;
	if (!surface || !HiResAssets::nativeInfo(const_cast<Surface*>(surface)->getSurface(), w, h))
		return false;
	if (lw) *lw = w;
	if (lh) *lh = h;
	return true;
}

bool HiResCanvas::drawSurface(const Surface *surface, int lx, int ly, int bright)
{
	int lw = 0, lh = 0;
	if (hasPicture(surface, &lw, &lh))
	{
		// resampled once to exactly the output size of its logical rect (cached by HiResAssets)
		const int pw = ox(lx + lw) - ox(lx), ph = oy(ly + lh) - oy(ly);
		const Uint32 *px = pw > 0 && ph > 0 ? HiResAssets::nativeRendered(const_cast<Surface*>(surface)->getSurface(), (_palette ? _palette : const_cast<Surface*>(surface)->getSurface()->format->palette), pw, ph) : nullptr;
		if (px && bright >= 0 && bright < 256)
		{
			// brightness of a custom item recolour script (battle-14), colour channels only
			std::vector<Uint32> dim(px, px + (size_t)pw * ph);
			for (Uint32 &p : dim)
				p = (p & 0xFF000000u) | (((((p >> 16) & 0xFF) * bright) >> 8) << 16) | (((((p >> 8) & 0xFF) * bright) >> 8) << 8) | (((p & 0xFF) * bright) >> 8);
			drawPicture(dim.data(), pw, ph, lx, ly, lw, lh);
			return true;
		}
		if (px)
		{
			drawPicture(px, pw, ph, lx, ly, lw, lh);
			return true;
		}
	}
	drawSurface8(surface, lx, ly);
	return false;
}

void HiResCanvas::present(SDL_Surface *holder)
{
	present(holder, _lx, _ly, _lw, _lh);
}

void HiResCanvas::present(SDL_Surface *holder, int x, int y, int w, int h)
{
	if (!holder || _px.empty() || !HiResLayer::recording())
		return;
	if (!_imageId)
		_imageId = HiResLayer::newImageId();
	HiResLayer::setImage(_imageId, _px.data(), _outW, _outH);
	HiResCmd c;
	c.kind = HiResCmdKind::Image;
	c.imageId = _imageId;
	c.x = (Sint16)x; c.y = (Sint16)y; c.w = (Sint16)w; c.h = (Sint16)h;
	c.clipX = c.x; c.clipY = c.y; c.clipW = c.w; c.clipH = c.h;
	HiResLayer::record(holder, c, true);
}

}
