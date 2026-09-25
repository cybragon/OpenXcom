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
#include "HiResLayer.h"
#include "HiResFont.h"
#include "OpenGL.h"
#include "Options.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace OpenXcom
{

bool HiResLayer::_recording = false;
bool HiResLayer::_textConfigured = false;

namespace
{

struct LayerState
{
	std::unique_ptr<HiResFont> font;
	std::unordered_map<SDL_Surface*, std::vector<HiResCmd>> lists;   ///< commands held by (non-screen) surfaces
	SDL_Surface *screen = nullptr;
	int baseW = 0, baseH = 0;
	bool outputSupported = false;
	std::vector<Uint32> stamps;        ///< per base pixel: id of last opaque blit
	Uint32 stampCounter = 0;
	std::vector<HiResCmd> frame;       ///< commands that reached the screen this frame
	// composition buffer (premultiplied BGRA, output resolution without black bands)
	std::vector<Uint32> rgba;
	int bufW = 0, bufH = 0;
	SDL_Rect prevDirty = {0, 0, 0, 0};
#ifndef __NO_OPENGL
	GLuint tex = 0;
	int texW = 0, texH = 0;
#endif
	int sizePct = 100, outlinePct = 100, boldPct = 30;
};

LayerState S;


std::vector<std::string> splitList(const std::string &s)
{
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string item;
	while (std::getline(ss, item, ';'))
	{
		size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
		if (a != std::string::npos)
			out.push_back(item.substr(a, b - a + 1));
	}
	return out;
}

inline bool intersect(int &x, int &y, int &w, int &h, int x2, int y2, int w2, int h2)
{
	int nx = std::max(x, x2), ny = std::max(y, y2);
	int nr = std::min(x + w, x2 + w2), nb = std::min(y + h, y2 + h2);
	x = nx; y = ny; w = nr - nx; h = nb - ny;
	return w > 0 && h > 0;
}

/// Glyph pixel size and outline radius (1/64 px) for a cell height at a scale.
/// refW (bitmap advance of U+AC00, base px) additionally caps the size so that a
/// Hangul syllable fits the box the bitmap layout reserved for it.
void glyphSize(int cellH, int refW, double sx, double sy, int &px, int &outline64, int &bold64)
{
	bold64 = (int)std::lround(sy * S.boldPct / 100.0 * 64.0);
	double r = std::max(0.0, sy * S.outlinePct / 100.0 * 0.5);
	outline64 = (int)std::lround(r * 64.0);
	// fill ink should occupy the cell minus the outline on both sides; Hangul ink ~0.85 em
	double em = (cellH * sy - 2.0 * r) / 0.85;
	if (refW > 0 && S.font && S.font->hasGlyph(0xAC00))
	{
		int probe = std::max(1, (int)std::lround(em));
		int adv = S.font->getAdvance(0xAC00, probe);
		double avail = refW * sx - 2.0 * r;
		if (adv > 0 && adv > avail)
			em = em * avail / adv;
	}
	em = em * S.sizePct / 100.0;
	px = std::max(1, (int)std::lround(em));
}

inline void blendPixel(Uint32 &dst, const SDL_Color &c, unsigned cov)
{
	if (!cov)
		return;
	unsigned a = cov * c.unused / 255;
	if (!a)
		return;
	unsigned sr = c.r * a / 255, sg = c.g * a / 255, sb = c.b * a / 255;
	unsigned inv = 255 - a;
	unsigned da = dst >> 24, dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
	da = a + da * inv / 255;
	dr = sr + dr * inv / 255;
	dg = sg + dg * inv / 255;
	db = sb + db * inv / 255;
	dst = (da << 24) | (dr << 16) | (dg << 8) | db;
}

void unionRect(SDL_Rect &r, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	if (r.w == 0 || r.h == 0)
	{
		r.x = x; r.y = y; r.w = w; r.h = h;
		return;
	}
	int x1 = std::min<int>(r.x, x), y1 = std::min<int>(r.y, y);
	int x2 = std::max<int>(r.x + r.w, x + w), y2 = std::max<int>(r.y + r.h, y + h);
	r.x = x1; r.y = y1; r.w = x2 - x1; r.h = y2 - y1;
}

/// Draws one coverage bitmap at (ox,oy) (buffer coords) with occlusion + clip tests.
void drawCoverage(const std::vector<uint8_t> &bm, int bw, int bh, int ox, int oy, const SDL_Color &color,
	int cx0, int cy0, int cx1, int cy1, double sx, double sy, Uint32 stamp, SDL_Rect &dirty)
{
	int x0 = std::max(ox, cx0), y0 = std::max(oy, cy0);
	int x1 = std::min(ox + bw, cx1), y1 = std::min(oy + bh, cy1);
	if (x0 >= x1 || y0 >= y1)
		return;
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	for (int y = y0; y < y1; ++y)
	{
		int by = std::min(S.baseH - 1, (int)(y * isy));
		const Uint32 *stampRow = &S.stamps[by * S.baseW];
		Uint32 *dst = &S.rgba[y * S.bufW];
		const uint8_t *src = &bm[(y - oy) * bw];
		for (int x = x0; x < x1; ++x)
		{
			unsigned cov = src[x - ox];
			if (!cov)
				continue;
			int bx = std::min(S.baseW - 1, (int)(x * isx));
			if (stampRow[bx] > stamp)
				continue; // hidden by something blitted later
			blendPixel(dst[x], color, cov);
		}
	}
	unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
}

}

void HiResLayer::init()
{
	shutdown();
	S.sizePct = std::max(10, Options::oxceHiResFontSize);
	S.outlinePct = std::max(0, Options::oxceHiResTextOutline);
	S.boldPct = std::max(0, Options::oxceHiResTextBold);
	if (!Options::oxceHiResOverlay)
	{
		return;
	}
	if (Options::oxceHiResText)
	{
		std::vector<std::string> specs = splitList(Options::oxceHiResFont);
		std::vector<std::string> fallback = splitList(Options::oxceHiResFontFallback);
		specs.insert(specs.end(), fallback.begin(), fallback.end());
		if (specs.empty())
		{
			// defaults: Windows system fonts first, then common Linux/macOS fonts
			specs = { "malgun.ttf", "gulim.ttc", "batang.ttc", "segoeui.ttf",
				"NanumGothic.ttf", "NotoSansCJK-Regular.ttc", "DejaVuSans.ttf", "AppleSDGothicNeo.ttc" };
		}
		HiResFont::Settings settings;
		settings.hinting = Options::oxceHiResTextHinting;
		settings.antialias = Options::oxceHiResTextAntialias;
		S.font = std::make_unique<HiResFont>();
		if (S.font->load(specs, settings) > 0)
		{
			_textConfigured = true;
		}
		else
		{
			Log(LOG_ERROR) << "HiResLayer: no usable TTF font found, hi-res text disabled";
			S.font.reset();
		}
	}
	_recording = _textConfigured && S.outputSupported;
	Log(LOG_INFO) << "HiResLayer: overlay=" << Options::oxceHiResOverlay << " text=" << _textConfigured;
}

void HiResLayer::shutdown()
{
	_recording = false;
	_textConfigured = false;
	S.font.reset();
	S.lists.clear();
	S.frame.clear();
}

void HiResLayer::setScreen(SDL_Surface *screenBuffer, bool supportedOutput)
{
	S.screen = screenBuffer;
	S.baseW = screenBuffer ? screenBuffer->w : 0;
	S.baseH = screenBuffer ? screenBuffer->h : 0;
	S.outputSupported = supportedOutput;
	bool was = _recording;
	_recording = _textConfigured && S.outputSupported;
	if (_recording)
		S.stamps.assign((size_t)S.baseW * S.baseH, 0);
	else
		std::vector<Uint32>().swap(S.stamps);
	if (was != _recording)
	{
		Log(LOG_INFO) << "HiResLayer: recording " << (_recording ? "enabled" : "disabled (unsupported output path)");
	}
	S.frame.clear();
}

void HiResLayer::onContextLost()
{
#ifndef __NO_OPENGL
	S.tex = 0; // the old context and its textures are gone
	S.texW = S.texH = 0;
#endif
	S.prevDirty = {0, 0, 0, 0};
}

void HiResLayer::record(SDL_Surface *holder, const HiResCmd &cmd)
{
	if (!_recording || !holder)
		return;
	S.lists[holder].push_back(cmd);
}

void HiResLayer::clearSurface(SDL_Surface *holder)
{
	if (!holder)
		return;
	auto it = S.lists.find(holder);
	if (it != S.lists.end())
		it->second.clear();
}

void HiResLayer::forgetSurface(SDL_Surface *holder)
{
	if (holder)
		S.lists.erase(holder);
}

void HiResLayer::onBlit(SDL_Surface *src, SDL_Surface *dst, int x, int y)
{
	if (!_recording || !src || !dst)
		return;
	const bool toScreen = (dst == S.screen);
	Uint32 stamp = 0;
	if (toScreen)
	{
		// occlusion: stamp every base pixel covered by an opaque source pixel
		stamp = ++S.stampCounter;
		int rx = x, ry = y, rw = src->w, rh = src->h;
		if (intersect(rx, ry, rw, rh, 0, 0, S.baseW, S.baseH))
		{
			const bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
			const Uint32 key = src->format->colorkey;
			if (src->format->BitsPerPixel == 8 && keyed)
			{
				SDL_LockSurface(src);
				for (int yy = ry; yy < ry + rh; ++yy)
				{
					const Uint8 *sp = (const Uint8*)src->pixels + (yy - y) * src->pitch + (rx - x);
					Uint32 *st = &S.stamps[yy * S.baseW + rx];
					for (int xx = 0; xx < rw; ++xx)
					{
						if (sp[xx] != key)
							st[xx] = stamp;
					}
				}
				SDL_UnlockSurface(src);
			}
			else
			{
				for (int yy = ry; yy < ry + rh; ++yy)
					std::fill_n(&S.stamps[yy * S.baseW + rx], rw, stamp);
			}
		}
	}

	auto it = S.lists.find(src);
	const bool hasCmds = (it != S.lists.end() && !it->second.empty());
	if (!toScreen)
	{
		auto dit = S.lists.find(dst);
		if (dit != S.lists.end())
		{
			// the same child may be blitted repeatedly without the parent being cleared
			auto &dl = dit->second;
			dl.erase(std::remove_if(dl.begin(), dl.end(), [src](const HiResCmd &c) { return c.origin == src; }), dl.end());
		}
		if (!hasCmds)
			return;
	}
	else if (!hasCmds)
	{
		return;
	}

	const SDL_Palette *pal = src->format->palette;
	std::vector<HiResCmd> *dl = toScreen ? &S.frame : &S.lists[dst];
	// NOTE: S.lists[dst] may rehash; re-find src list after that
	const std::vector<HiResCmd> &sl = S.lists.find(src)->second;
	for (const HiResCmd &c0 : sl)
	{
		HiResCmd c = c0;
		c.x += x; c.y += y; c.clipX += x; c.clipY += y;
		int cx = c.clipX, cy = c.clipY, cw = c.clipW, ch = c.clipH;
		if (!intersect(cx, cy, cw, ch, x, y, src->w, src->h) || !intersect(cx, cy, cw, ch, 0, 0, dst->w, dst->h))
			continue;
		c.clipX = cx; c.clipY = cy; c.clipW = cw; c.clipH = ch;
		if (toScreen)
		{
			c.stamp = stamp;
			if (pal)
			{
				c.fill = pal->colors[c.fillIdx];
				c.line = pal->colors[c.lineIdx];
			}
			else
			{
				c.fill = SDL_Color{255, 255, 255, 255};
				c.line = SDL_Color{0, 0, 0, 255};
			}
			c.fill.unused = 255;
			c.line.unused = 255;
		}
		else
		{
			c.origin = src;
		}
		dl->push_back(c);
	}
}

bool HiResLayer::canRenderGlyph(UCode c)
{
	return _textConfigured && S.font && S.font->hasGlyph(c);
}

int HiResLayer::baseAdvance(UCode c, int cellH)
{
	if (!canRenderGlyph(c))
		return -1;
	int px, outline64, bold64;
	glyphSize(cellH, 0, 1.0, 1.0, px, outline64, bold64);
	int adv = S.font->getAdvance(c, px);
	return adv < 0 ? -1 : std::max(1, adv);
}

void HiResLayer::beginFrame()
{
	if (!_recording)
		return;
	S.frame.clear();
	S.stampCounter = 0;
	std::fill(S.stamps.begin(), S.stamps.end(), 0);
}

const std::vector<HiResCmd> &HiResLayer::frameCommands()
{
	return S.frame;
}

void HiResLayer::renderGL(OpenGL *gl, int outW, int outH, int top, int bottom, int left, int right, const SDL_Color *screenPalette)
{
#ifdef __NO_OPENGL
	(void)gl; (void)outW; (void)outH; (void)top; (void)bottom; (void)left; (void)right; (void)screenPalette;
#else
	(void)screenPalette; // 32bpp buffer: colours were resolved at blit time
	if (!_recording || !gl || S.baseW <= 0 || S.baseH <= 0)
		return;
	const int dstW = outW - left - right, dstH = outH - top - bottom;
	if (dstW <= 0 || dstH <= 0)
		return;
	if (S.frame.empty() && S.prevDirty.w == 0)
		return;
	if (S.bufW != dstW || S.bufH != dstH)
	{
		S.bufW = dstW;
		S.bufH = dstH;
		S.rgba.assign((size_t)dstW * dstH, 0);
		S.prevDirty = {0, 0, (Uint16)dstW, (Uint16)dstH};
	}
	// clear what was drawn last frame
	for (int y = S.prevDirty.y; y < S.prevDirty.y + S.prevDirty.h; ++y)
		std::fill_n(&S.rgba[y * S.bufW + S.prevDirty.x], S.prevDirty.w, 0u);

	const double sx = dstW / (double)S.baseW, sy = dstH / (double)S.baseH;
	if (S.font)
		S.font->trimCache(16384);
	SDL_Rect dirty = {0, 0, 0, 0};
	// Commands delivered by the same blit (one text widget / button) are drawn in two
	// passes - all outlines, then all bodies - like the bitmap font whose outline never
	// covers the neighbouring glyph's body. Groups keep their blit (z) order.
	const HiResGlyph *refGlyph = nullptr;
	int refPx = -1;
	for (size_t groupStart = 0; groupStart < S.frame.size(); )
	{
		size_t groupEnd = groupStart;
		while (groupEnd < S.frame.size() && S.frame[groupEnd].stamp == S.frame[groupStart].stamp)
			++groupEnd;
		for (int pass = 0; pass < 2; ++pass)
		{
			for (size_t i = groupStart; i < groupEnd; ++i)
			{
				const HiResCmd &c = S.frame[i];
				if (c.kind != HiResCmdKind::Glyph || !S.font)
					continue; // HiResCmdKind::Image: reserved for hi-res sprite replacement
				int px, outline64, bold64;
				glyphSize(c.cellH, c.refW, sx, sy, px, outline64, bold64);
				// vertical placement: centre the ink of a reference glyph in the cell
				if (px != refPx)
				{
					refGlyph = &S.font->getGlyph(S.font->hasGlyph(0xAC00) ? 0xAC00 : 'H', px, 0);
					refPx = px;
				}
				const HiResGlyph &g = S.font->getGlyph(c.code, px, outline64, bold64);
				if (!g.ok || (pass == 0 && g.line.empty()))
					continue;
				int refTop = refGlyph->ok ? refGlyph->fillT : px * 4 / 5;
				int refBottom = refGlyph->ok ? refGlyph->fillT - refGlyph->fillH : 0;
				const double bx0 = c.x * sx, by0 = c.y * sy, bw = c.w * sx, bh = c.cellH * sy;
				const int baseline = (int)std::lround(by0 + (bh - (refTop - refBottom)) / 2.0 + refTop);
				const int pen = (int)std::lround(bx0 + (bw - g.advance) / 2.0);
				const int cx0 = std::max(0, (int)std::floor(c.clipX * sx)), cy0 = std::max(0, (int)std::floor(c.clipY * sy));
				const int cx1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
				const int cy1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
				if (pass == 0)
					drawCoverage(g.line, g.lineW, g.lineH, pen + g.lineL, baseline - g.lineT, c.line, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
				else
					drawCoverage(g.fill, g.fillW, g.fillH, pen + g.fillL, baseline - g.fillT, c.fill, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
			}
		}
		groupStart = groupEnd;
	}

	SDL_Rect upload = S.prevDirty;
	unionRect(upload, dirty.x, dirty.y, dirty.w, dirty.h);
	S.prevDirty = dirty;

	if (S.tex == 0)
	{
		glGenTextures(1, &S.tex);
		S.texW = S.texH = 0;
	}
	glBindTexture(GL_TEXTURE_2D, S.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, S.bufW);
	if (S.texW != S.bufW || S.texH != S.bufH)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S.bufW, S.bufH, 0, GL_BGRA, GL_UNSIGNED_BYTE, S.rgba.data());
		S.texW = S.bufW;
		S.texH = S.bufH;
	}
	else if (upload.w > 0 && upload.h > 0)
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, upload.x, upload.y, upload.w, upload.h, GL_BGRA, GL_UNSIGNED_BYTE,
			S.rgba.data() + upload.y * S.bufW + upload.x);
	}
	glErrorCheck();

	if (dirty.w > 0)
	{
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied alpha
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		// OpenGL::refresh() left an ortho projection with (0,0) at the bottom-left of the window
		const int x0 = left, x1 = outW - right;
		const int yTop = outH - top, yBottom = bottom;
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2i(x0, yTop);
		glTexCoord2f(1.0f, 0.0f); glVertex2i(x1, yTop);
		glTexCoord2f(1.0f, 1.0f); glVertex2i(x1, yBottom);
		glTexCoord2f(0.0f, 1.0f); glVertex2i(x0, yBottom);
		glEnd();
		glDisable(GL_BLEND);
		glErrorCheck();
	}
	// restore the state OpenGL::refresh() relies on
	glBindTexture(GL_TEXTURE_2D, gl->gltexture);
#endif
}

}
