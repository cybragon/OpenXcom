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
#include "HiResAssets.h"
#include "HiResLayer.h"
#include "Surface.h"
#include "Options.h"
#include "Logger.h"
#include "Exception.h"
#include "FileMap.h"
#include "CrossPlatform.h"
#include "SDL2Helpers.h"
#include "../lodepng.h"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>
#include <list>
#include <map>
#include <climits>

namespace OpenXcom
{
namespace HiResAssets
{

size_t registeredCount = 0;

/// A decoded replacement picture (shared by all frames cut from it).
struct Picture
{
	int w = 0, h = 0;
	bool trueColor = false;
	std::vector<Uint8> idx;       ///< 8bpp pixels (index 0 = transparent)
	std::vector<Uint32> argb;     ///< 32bpp pixels, straight alpha
	std::vector<SDL_Color> pal;   ///< palette of an 8bpp file (as the normal loader sets it)
	bool ownPalette = false;      ///< 8bpp file whose colours match no game palette: displayed from argb (own palette)
	std::string file;
};

namespace
{

/// One registered proxy.
struct Entry
{
	Uint32 id = 0;
	std::shared_ptr<Picture> pic;
	double rx = 0, ry = 0, rw = 0, rh = 0;  ///< picture rect shown by the proxy
	int w = 0, h = 0;                       ///< proxy size
	std::vector<Uint8> expected;            ///< proxy pixels when attached (w * h)
	const SDL_Surface *surface = nullptr;
	const void *pixels = nullptr;           ///< sanity check: the SDL surface was not replaced
	std::string name;
};

std::unordered_map<const SDL_Surface*, Uint32> bySurface;
std::unordered_map<Uint32, std::unique_ptr<Entry>> byId;
Uint32 idCounter = 0;
std::set<std::string> deniedTypes, deniedPrefixes;
const SDL_Color *palGeo = nullptr, *palBase = nullptr, *palBattle = nullptr;

/// Palettes 8bpp pictures were resolved with (key -> colours).
std::unordered_map<Uint32, std::vector<SDL_Color>> palettes;

/// Resampled pictures (output resolution), most recently used first.
struct Rendered
{
	Uint32 id, palKey;
	int sx, sy, sw, sh, outW, outH;
	std::vector<Uint32> px;
};
std::list<Rendered> renderCache;
size_t renderBytes = 0;
const size_t RENDER_CACHE_LIMIT = 128u * 1024u * 1024u;

Entry *find(Uint32 id)
{
	auto it = byId.find(id);
	return it == byId.end() ? nullptr : it->second.get();
}

void dropRendered(Uint32 id)
{
	for (auto it = renderCache.begin(); it != renderCache.end(); )
	{
		if (it->id == id)
		{
			renderBytes -= it->px.size() * sizeof(Uint32);
			it = renderCache.erase(it);
		}
		else
			++it;
	}
}

inline Uint32 pixelOf(const Picture &p, const SDL_Color *pal, int x, int y)
{
	if (p.trueColor || p.ownPalette)
		return p.argb[(size_t)y * p.w + x];
	const Uint8 i = p.idx[(size_t)y * p.w + x];
	if (!i)
		return 0;
	const SDL_Color &c = pal[i];
	return 0xFF000000u | ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b;
}

bool uniform(const SDL_Color *pal)
{
	if (!pal)
		return true;
	for (int i = 2; i < 256; ++i)
		if (pal[i].r != pal[1].r || pal[i].g != pal[1].g || pal[i].b != pal[1].b)
			return false;
	return true;
}

/// Tent filter weights of one axis: output cell o covers source [a + o*step, a + (o+1)*step).
struct Taps
{
	std::vector<int> first, count;
	std::vector<float> w;
	std::vector<size_t> offs;
};
Taps makeTaps(double a, double len, int outN, int srcN)
{
	Taps t;
	const double step = len / outN;
	const double radius = std::max(1.0, step);
	const int lo = std::max(0, (int)std::floor(a)), hi = std::min(srcN, (int)std::ceil(a + len - 1e-9));
	for (int o = 0; o < outN; ++o)
	{
		const double c = a + (o + 0.5) * step - 0.5; // source pixel centre coordinate
		int i0 = std::max(lo, (int)std::ceil(c - radius)), i1 = std::min(hi - 1, (int)std::floor(c + radius));
		if (i1 < i0) { i0 = i1 = std::min(hi - 1, std::max(lo, (int)std::lround(c))); }
		t.first.push_back(i0);
		t.count.push_back(i1 - i0 + 1);
		t.offs.push_back(t.w.size());
		double sum = 0;
		for (int i = i0; i <= i1; ++i)
		{
			const double wt = std::max(0.0, 1.0 - std::fabs(i - c) / radius);
			t.w.push_back((float)wt);
			sum += wt;
		}
		if (sum <= 0)
		{
			for (int i = 0; i <= i1 - i0; ++i) t.w[t.offs.back() + i] = 1.0f / (i1 - i0 + 1);
		}
		else
		{
			for (int i = 0; i <= i1 - i0; ++i) t.w[t.offs.back() + i] = (float)(t.w[t.offs.back() + i] / sum);
		}
	}
	return t;
}

/// Resamples the picture rect into outW x outH (premultiplied filtering, straight ARGB result).
void resample(const Picture &p, const SDL_Color *pal, double rx, double ry, double rw, double rh, int outW, int outH, std::vector<Uint32> &out)
{
	const Taps tx = makeTaps(rx, rw, outW, p.w), ty = makeTaps(ry, rh, outH, p.h);
	const int y0 = std::max(0, (int)std::floor(ry)), y1 = std::min(p.h, (int)std::ceil(ry + rh - 1e-9));
	const int rows = std::max(0, y1 - y0);
	std::vector<float> tmp((size_t)rows * outW * 4, 0.0f);
	for (int y = y0; y < y1; ++y)
	{
		float *row = &tmp[(size_t)(y - y0) * outW * 4];
		for (int o = 0; o < outW; ++o)
		{
			float r = 0, g = 0, b = 0, a = 0;
			const float *w = &tx.w[tx.offs[o]];
			for (int k = 0; k < tx.count[o]; ++k)
			{
				const Uint32 px = pixelOf(p, pal, tx.first[o] + k, y);
				const float pa = (px >> 24) / 255.0f * w[k];
				a += pa;
				r += ((px >> 16) & 0xFF) * pa;
				g += ((px >> 8) & 0xFF) * pa;
				b += (px & 0xFF) * pa;
			}
			row[o * 4 + 0] = r; row[o * 4 + 1] = g; row[o * 4 + 2] = b; row[o * 4 + 3] = a;
		}
	}
	out.assign((size_t)outW * outH, 0);
	for (int o = 0; o < outH; ++o)
	{
		const float *w = &ty.w[ty.offs[o]];
		for (int x = 0; x < outW; ++x)
		{
			float r = 0, g = 0, b = 0, a = 0;
			for (int k = 0; k < ty.count[o]; ++k)
			{
				const int yy = ty.first[o] + k - y0;
				if (yy < 0 || yy >= rows) continue;
				const float *s = &tmp[((size_t)yy * outW + x) * 4];
				r += s[0] * w[k]; g += s[1] * w[k]; b += s[2] * w[k]; a += s[3] * w[k];
			}
			if (a <= 1e-4f)
				continue;
			const unsigned A = (unsigned)std::min(255.0f, std::lround(a * 255.0f) * 1.0f);
			if (!A)
				continue;
			auto ch = [&](float v) { return (Uint32)std::min(255.0f, std::max(0.0f, v / a + 0.5f)); };
			out[(size_t)o * outW + x] = ((Uint32)A << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
		}
	}
}

/// 32bpp decode of a PNG (lodepng) or any SDL_image format, straight-alpha ARGB.
bool decodeTrueColor(const std::string &filename, Picture &pic)
{
	auto rw = FileMap::getRWops(filename);
	if (!rw)
		return false;
	if (CrossPlatform::compareExt(filename, "png"))
	{
		size_t size;
		void *data = SDL_LoadFile_RW(rw, &size, SDL_TRUE);
		if (!data)
			return false;
		std::vector<unsigned char> png((unsigned char*)data, (unsigned char*)data + size);
		SDL_free(data);
		std::vector<unsigned char> image;
		unsigned w, h;
		if (lodepng::decode(image, w, h, png, LCT_RGBA, 8) != 0 || !w || !h)
			return false;
		pic.w = (int)w; pic.h = (int)h;
		pic.argb.resize((size_t)w * h);
		for (size_t i = 0; i < pic.argb.size(); ++i)
		{
			const unsigned char *s = &image[i * 4];
			pic.argb[i] = s[3] ? (((Uint32)s[3] << 24) | ((Uint32)s[0] << 16) | ((Uint32)s[1] << 8) | s[2]) : 0u;
		}
		return true;
	}
	SDL_Surface *img = IMG_Load_RW(rw, SDL_TRUE);
	if (!img)
		return false;
	SDL_PixelFormat fmt;
	std::memset(&fmt, 0, sizeof(fmt));
	fmt.BitsPerPixel = 32; fmt.BytesPerPixel = 4;
	fmt.Rmask = 0x00FF0000; fmt.Gmask = 0x0000FF00; fmt.Bmask = 0x000000FF; fmt.Amask = 0xFF000000;
	fmt.Rshift = 16; fmt.Gshift = 8; fmt.Bshift = 0; fmt.Ashift = 24;
	fmt.alpha = 255;
	const bool hasAlpha = img->format->Amask != 0;
	SDL_Surface *conv = SDL_ConvertSurface(img, &fmt, SDL_SWSURFACE);
	SDL_FreeSurface(img);
	if (!conv)
		return false;
	pic.w = conv->w; pic.h = conv->h;
	pic.argb.resize((size_t)pic.w * pic.h);
	SDL_LockSurface(conv);
	for (int y = 0; y < pic.h; ++y)
	{
		const Uint32 *s = (const Uint32*)((const Uint8*)conv->pixels + y * conv->pitch);
		for (int x = 0; x < pic.w; ++x)
		{
			Uint32 p = s[x];
			if (!hasAlpha) p |= 0xFF000000u;
			pic.argb[(size_t)y * pic.w + x] = (p >> 24) ? p : 0u;
		}
	}
	SDL_UnlockSurface(conv);
	SDL_FreeSurface(conv);
	return true;
}

std::vector<const SDL_Color*> gamePalettes;

/**
 * Own palette test of an 8bpp picture: its used colours are compared to the game palettes
 * (geoscape, basescape, ufopaedia, graphs, battlescape) index by index with a small tolerance.
 * No match: the picture is displayed in true colour from its own palette (index 0 transparent);
 * a game-palette picture keeps following the palette of the screen. The proxy keeps the file indices.
 */
void preparePicture(Picture &p)
{
	if (p.trueColor || p.pal.size() != 256 || gamePalettes.empty())
		return;
	std::vector<size_t> hist(256, 0);
	for (Uint8 i : p.idx) ++hist[i];
	size_t used = 0;
	for (int i = 1; i < 224; ++i) used += hist[i];
	const int last = used ? 224 : 256; // UI colours 224-255 (back palettes) only if nothing else is used
	if (!used)
		for (int i = 224; i < 256; ++i) used += hist[i];
	if (!used)
		return;
	const int TOL = 8;
	for (const SDL_Color *g : gamePalettes)
	{
		if (!g) continue;
		size_t bad = 0;
		for (int i = 1; i < last; ++i)
		{
			if (!hist[i]) continue;
			const SDL_Color &a = p.pal[i], &b = g[i];
			if (std::abs(a.r - b.r) > TOL || std::abs(a.g - b.g) > TOL || std::abs(a.b - b.b) > TOL)
				bad += hist[i];
		}
		if (bad * 100 <= used * 2)
			return; // a game palette (98% of the pixels match)
	}
	p.ownPalette = true;
	p.argb.resize(p.idx.size());
	for (size_t k = 0; k < p.idx.size(); ++k)
	{
		const Uint8 i = p.idx[k];
		const SDL_Color &c = p.pal[i];
		p.argb[k] = i ? (0xFF000000u | ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b) : 0u;
	}
}

} // namespace

namespace
{
bool loadedEnabled = false;
std::map<std::string, const SDL_Color*> typePalettes;
std::set<std::string> attachFitTypes;
}

bool enabled()
{
	return loadedEnabled;
}

void beginModLoad()
{
	loadedEnabled = Options::oxceHiResAssets;
	typePalettes.clear();
	attachFitTypes.clear();
	deniedTypes.clear();
	deniedPrefixes.clear();
}

void denyTrueColor(const std::string &type)
{
	deniedTypes.insert(type);
}

void denyTrueColorPrefix(const std::string &prefix)
{
	if (!prefix.empty())
		deniedPrefixes.insert(prefix);
}

void allowTrueColor(const std::string &type)
{
	deniedTypes.erase(type);
}

void attachWhenFits(const std::string &type)
{
	attachFitTypes.insert(type);
}

bool trueColorAllowed(const std::string &type)
{
	if (deniedTypes.count(type))
		return false;
	for (const auto &pre : deniedPrefixes)
		if (type.compare(0, pre.size(), pre) == 0)
			return false;
	return true;
}

size_t deniedCount()
{
	return deniedTypes.size() + deniedPrefixes.size();
}

void setGamePalettes(const std::vector<const SDL_Color*> &palettes)
{
	gamePalettes = palettes;
}

void setPalettes(const SDL_Color *geoscape, const SDL_Color *basescape, const SDL_Color *battlescape)
{
	palGeo = geoscape;
	palBase = basescape;
	palBattle = battlescape;
}

bool loadDeniedTrueColor(Surface *target, const std::string &filename, const std::string &type)
{
	try
	{
		target->loadImage(filename);
		return true; // 8bpp (or anything the normal loader accepts): as before
	}
	catch (Exception &)
	{
		if (!enabled() || !palBattle)
			throw;
		if (CrossPlatform::compareExt(filename, "jpg") || CrossPlatform::compareExt(filename, "jpeg"))
		{
			Log(LOG_WARNING) << "HiResAssets: " << type << ": " << filename << ": JPEG pictures are not allowed for a battlescape resource: ignored";
			return false;
		}
		std::shared_ptr<Picture> pic = decode(filename);
		if (!pic || !pic->trueColor)
			throw;
		Log(LOG_WARNING) << "HiResAssets: " << type << ": " << filename << " is a 32-bit image, not allowed for a battlescape resource: converted to the battlescape palette (" << pic->w << "x" << pic->h << ", 8-bit)";
		forget(target->getSurface());
		*target = Surface(pic->w, pic->h, 0, 0);
		target->setPalette(palBattle, 0, 256);
		makeProxy(target, *pic, 0, 0, pic->w, pic->h, pic->w, pic->h, palBattle);
		return true;
	}
}

int pictureWidth(const Picture &p) { return p.w; }
int pictureHeight(const Picture &p) { return p.h; }
bool pictureTrueColor(const Picture &p) { return p.trueColor; }
bool pictureOwnPalette(const Picture &p) { return p.ownPalette; }

std::shared_ptr<Picture> decode(const std::string &filename)
{
	auto pic = std::make_shared<Picture>();
	pic->file = filename;
	try
	{
		// 8bpp: exactly what the normal loader produces (transparent index fix-up included)
		Surface tmp(1, 1);
		tmp.loadImage(filename);
		if (tmp.getSurface() && tmp.getSurface()->format->BitsPerPixel == 8)
		{
			pic->w = tmp.getWidth();
			pic->h = tmp.getHeight();
			pic->idx.resize((size_t)pic->w * pic->h);
			for (int y = 0; y < pic->h; ++y)
				for (int x = 0; x < pic->w; ++x)
					pic->idx[(size_t)y * pic->w + x] = tmp.getPixel(x, y);
			pic->pal.assign(tmp.getPalette(), tmp.getPalette() + 256);
			preparePicture(*pic);
			return pic;
		}
	}
	catch (Exception &)
	{
		// not 8bpp (or broken): try 32bpp below
	}
	if (decodeTrueColor(filename, *pic))
	{
		pic->trueColor = true;
		return pic;
	}
	return nullptr;
}

void setTypePalette(const std::string &type, const SDL_Color *palette)
{
	if (palette)
		typePalettes[type] = palette;
	else
		typePalettes.erase(type);
}

const SDL_Color *quantizePalette(const std::string &type, const SDL_Color *origPalette)
{
	auto tp = typePalettes.find(type);
	if (tp != typePalettes.end())
		return tp->second;
	if ((type == "BASEBITS.PCK" || type == "INTICON.PCK") && palBase)
		return palBase;
	if (!uniform(origPalette))
		return origPalette;
	return palGeo ? palGeo : origPalette;
}

void logMismatch(const std::string &type, const std::string &filename, int origW, int origH, int fileW, int fileH, const char *what)
{
	if (origW == fileW && origH == fileH)
		return;
	Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is " << fileW << "x" << fileH << ", replaced image is "
		<< origW << "x" << origH << " (" << what << ")";
}

void makeProxy(Surface *target, const Picture &pic, double rx, double ry, double rw, double rh, int w, int h, const SDL_Color *palette)
{
	SDL_Surface *s = target->getSurface();
	SDL_LockSurface(s);
	std::vector<int> hist(256, 0);
	std::vector<int> touched;
	// nearest palette colour, cached per RGB
	std::unordered_map<Uint32, Uint8> nearest;
	auto quant = [&](Uint32 rgb) -> Uint8
	{
		auto it = nearest.find(rgb);
		if (it != nearest.end()) return it->second;
		const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
		int best = 1, bestD = 1 << 30;
		for (int i = 1; i < 256; ++i)
		{
			const SDL_Color &c = palette[i];
			const int dr = c.r - r, dg = c.g - g, db = c.b - b;
			const int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
			if (d < bestD) { bestD = d; best = i; }
		}
		return nearest[rgb] = (Uint8)best;
	};
	for (int ty = 0; ty < h; ++ty)
	{
		const double a0 = ry + ty * rh / h, a1 = ry + (ty + 1) * rh / h;
		int y0 = std::max(0, (int)std::floor(a0 + 1e-9)), y1 = std::min(pic.h, (int)std::ceil(a1 - 1e-9));
		Uint8 *row0 = (Uint8*)s->pixels + ty * s->pitch;
		if (a0 >= pic.h) { std::memset(row0, 0, w); continue; }
		if (y1 <= y0) { y0 = std::min(pic.h - 1, std::max(0, (int)std::floor((a0 + a1) / 2))); y1 = y0 + 1; }
		Uint8 *row = (Uint8*)s->pixels + ty * s->pitch;
		for (int tx = 0; tx < w; ++tx)
		{
			const double b0 = rx + tx * rw / w, b1 = rx + (tx + 1) * rw / w;
			int x0 = std::max(0, (int)std::floor(b0 + 1e-9)), x1 = std::min(pic.w, (int)std::ceil(b1 - 1e-9));
			if (b0 >= pic.w) { row[tx] = 0; continue; }
			if (x1 <= x0) { x0 = std::min(pic.w - 1, std::max(0, (int)std::floor((b0 + b1) / 2))); x1 = x0 + 1; }
			const int n = (x1 - x0) * (y1 - y0);
			Uint8 v = 0;
			if (!pic.trueColor)
			{
				int opaque = 0, best = 0, bestN = 0;
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x)
					{
						const Uint8 i = pic.idx[(size_t)y * pic.w + x];
						if (!i) continue;
						if (!hist[i]) touched.push_back(i);
						if (++hist[i] > bestN || (hist[i] == bestN && i < best)) { bestN = hist[i]; best = i; }
						++opaque;
					}
				for (int i : touched) hist[i] = 0;
				touched.clear();
				if (opaque * 2 >= n) v = (Uint8)best;
			}
			else
			{
				double sa = 0, sr = 0, sg = 0, sb = 0;
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x)
					{
						const Uint32 p = pic.argb[(size_t)y * pic.w + x];
						const double pa = (p >> 24) / 255.0;
						sa += pa; sr += ((p >> 16) & 0xFF) * pa; sg += ((p >> 8) & 0xFF) * pa; sb += (p & 0xFF) * pa;
					}
				if (sa * 2 >= n && sa > 0)
				{
					const Uint32 rgb = ((Uint32)std::lround(sr / sa) << 16) | ((Uint32)std::lround(sg / sa) << 8) | (Uint32)std::lround(sb / sa);
					v = quant(rgb);
				}
			}
			row[tx] = v;
		}
	}
	SDL_UnlockSurface(s);
}

void attach(Surface *proxy, const std::shared_ptr<Picture> &pic, double rx, double ry, double rw, double rh, const std::string &name)
{
	SDL_Surface *s = proxy->getSurface();
	if (!s || !pic || s->format->BitsPerPixel != 8)
		return;
	forget(s);
	auto e = std::make_unique<Entry>();
	if (++idCounter == 0) ++idCounter;
	e->id = idCounter;
	e->pic = pic;
	e->rx = rx; e->ry = ry; e->rw = rw; e->rh = rh;
	e->w = s->w; e->h = s->h;
	e->surface = s;
	e->pixels = s->pixels;
	e->name = name;
	e->expected.resize((size_t)s->w * s->h);
	SDL_LockSurface(s);
	for (int y = 0; y < s->h; ++y)
		std::memcpy(&e->expected[(size_t)y * s->w], (const Uint8*)s->pixels + y * s->pitch, s->w);
	SDL_UnlockSurface(s);
	bySurface[s] = e->id;
	byId[e->id] = std::move(e);
	registeredCount = bySurface.size();
}

bool fitBox(int boxW, int boxH, int w, int h, int &fw, int &fh, double &scale)
{
	fw = w; fh = h; scale = 1.0;
	if (boxW <= 0 || boxH <= 0 || (w <= boxW && h <= boxH))
		return false;
	scale = std::max(w / (double)boxW, h / (double)boxH);
	fw = std::max(1, std::min(boxW, (int)std::lround(w / scale)));
	fh = std::max(1, std::min(boxH, (int)std::lround(h / scale)));
	return true;
}

bool loadReplacement(Surface *target, const std::string &filename, int boxW, int boxH, const SDL_Color *origPalette, const std::string &type)
{
	if (!enabled() || !target || boxW <= 0 || boxH <= 0)
		return false;
	std::shared_ptr<Picture> pic = decode(filename);
	if (!pic)
		return false; // the normal loader reports the error
	if (pic->trueColor && !trueColorAllowed(type))
	{
		return false; // battlescape resource: converted to the battlescape palette by loadDeniedTrueColor
	}
	int fw, fh;
	double scale;
	const bool larger = fitBox(boxW, boxH, pic->w, pic->h, fw, fh, scale);
	if (!larger && !pic->trueColor)
		return false; // fits the box (same size or smaller) 8bpp: exactly the normal path
	const bool boxKnown = boxW < (1 << 15) && boxH < (1 << 15);
	if (!larger && boxKnown && pic->trueColor && attachFitTypes.count(type) && pic->w == boxW && pic->h == boxH)
	{
		// allow-listed battlescape chrome (ICONS, TAC01...) of exactly the box size: attached like a larger
		// picture (proxy quantized to the type palette, the picture drawn as is over it)
		Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is a 32-bit image of " << pic->w << "x" << pic->h
			<< " (box size): drawn as a 32-bit picture over a quantized 8-bit proxy";
		std::vector<SDL_Color> pal(256, SDL_Color{0, 0, 0, 255});
		if (origPalette)
			std::memcpy(pal.data(), origPalette, 256 * sizeof(SDL_Color));
		forget(target->getSurface());
		*target = Surface(boxW, boxH, 0, 0);
		target->setPalette(pal.data(), 0, 256);
		makeProxy(target, *pic, 0, 0, pic->w, pic->h, boxW, boxH, quantizePalette(type, origPalette));
		attach(target, pic, 0, 0, pic->w, pic->h, type);
		return true;
	}
	if (!larger && boxKnown)
	{
		// 32-bit picture that fits the box: stock-like 8bpp of the BOX size, image at original
		// size in the top-left, the rest transparent (no hi-res attach — a file-sized surface was
		// misplaced on some Windows/OpenGL paths; a box-sized SPK matches vanilla).
		Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is a 32-bit image of " << pic->w << "x" << pic->h << " (box " << boxW << "x" << boxH
			<< "): fits the box, 8-bit at box size with the image at top-left (no hi-res, like stock)";
		std::vector<SDL_Color> pal(256, SDL_Color{0, 0, 0, 255});
		if (origPalette)
			std::memcpy(pal.data(), origPalette, 256 * sizeof(SDL_Color));
		forget(target->getSurface());
		*target = Surface(boxW, boxH, 0, 0);
		target->setPalette(pal.data(), 0, 256);
		if (SDL_Surface *ts = target->getSurface())
			SDL_FillRect(ts, 0, 0);
		makeProxy(target, *pic, 0, 0, pic->w, pic->h, pic->w, pic->h, quantizePalette(type, origPalette));
		return true;
	}
	if (larger)
		Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is " << pic->w << "x" << pic->h << (pic->trueColor ? " (32-bit)" : " (8-bit)")
			<< ", box " << boxW << "x" << boxH << ": drawn hi-res at " << fw << "x" << fh << " (scale 1/" << scale << "), " << fw << "x" << fh << " proxy for the game" << (pic->ownPalette ? "; own palette (matches no game palette): drawn in its own colours" : "");
	else
		Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is a new 32-bit image of " << pic->w << "x" << pic->h
			<< ": kept at its size with a quantized proxy (each consumer fits it into its box)";
	// palette: 8bpp pictures bring their own palette like with the normal loader
	std::vector<SDL_Color> pal(256, SDL_Color{0, 0, 0, 255});
	const SDL_Color *qpal = quantizePalette(type, origPalette);
	if (!pic->trueColor && pic->pal.size() == 256)
	{
		pal = pic->pal;
	}
	else if (origPalette)
	{
		std::memcpy(pal.data(), origPalette, 256 * sizeof(SDL_Color));
	}
	forget(target->getSurface());
	*target = Surface(fw, fh, 0, 0);
	target->setPalette(pal.data(), 0, 256);
	makeProxy(target, *pic, 0, 0, pic->w, pic->h, fw, fh, pic->trueColor ? qpal : pal.data());
	attach(target, pic, 0, 0, pic->w, pic->h, type);
	return true;
}

namespace
{
/// Fitted proxies of images larger than a consumer box (see fit()), per source surface.
struct FitKey
{
	const SDL_Surface *src;
	int w, h;
	bool operator<(const FitKey &o) const { return src != o.src ? src < o.src : (w != o.w ? w < o.w : h < o.h); }
};
std::map<FitKey, std::unique_ptr<Surface>> fitted;
bool inFitForget = false;
}

Surface *fit(Surface *src, int boxW, int boxH)
{
	if (!src || !enabled() || !src->getSurface())
		return src;
	SDL_Surface *s = src->getSurface();
	int fw, fh;
	double scale;
	if (s->format->BitsPerPixel != 8)
		return src;
	const FitKey key{ s, boxW, boxH };
	auto it = fitted.find(key);
	if (it != fitted.end())
		return it->second.get();
	if (!fitBox(boxW, boxH, s->w, s->h, fw, fh, scale))
	{
		// fits the box (surface <= box). Keep hi-res when the picture is larger than the proxy
		// (replacement already fitted at load, e.g. Flag10 160x80 -> 40x20): stripping it was a
		// regression. Only strip when the picture is not larger than the surface (smaller-than-box
		// truecolor kept at file size) so consumers blit 8bpp at original size, top-left.
		auto reg = bySurface.find(s);
		if (reg == bySurface.end())
			return src;
		Entry *e = find(reg->second);
		if (e && e->pic && (e->pic->w > e->w || e->pic->h > e->h))
			return src;
		std::unique_ptr<Surface> plain(new Surface(s->w, s->h, 0, 0));
		plain->setPalette(src->getPalette(), 0, 256);
		SDL_Surface *d = plain->getSurface();
		SDL_LockSurface(s);
		SDL_LockSurface(d);
		for (int y = 0; y < s->h; ++y)
			std::memcpy((Uint8*)d->pixels + y * d->pitch, (const Uint8*)s->pixels + y * s->pitch, s->w);
		SDL_UnlockSurface(d);
		SDL_UnlockSurface(s);
		Log(LOG_INFO) << "HiResAssets: image " << s->w << "x" << s->h << " fits its box " << boxW << "x" << boxH << ": drawn like a stock image (8-bit, original size)";
		Surface *ret = plain.get();
		fitted[key] = std::move(plain);
		return ret;
	}
	std::shared_ptr<Picture> pic;
	double rx = 0, ry = 0, rw = s->w, rh = s->h;
	auto reg = bySurface.find(s);
	Entry *e = reg != bySurface.end() ? find(reg->second) : nullptr;
	if (e && e->pixels == s->pixels && e->w == s->w && e->h == s->h)
	{
		pic = e->pic;
		rx = e->rx; ry = e->ry; rw = e->rw; rh = e->rh;
	}
	else
	{
		// a plain 8bpp image (new mod image loaded the normal way): its own pixels are the picture
		pic = std::make_shared<Picture>();
		pic->w = s->w; pic->h = s->h;
		pic->idx.resize((size_t)s->w * s->h);
		SDL_LockSurface(s);
		for (int y = 0; y < s->h; ++y)
			std::memcpy(&pic->idx[(size_t)y * s->w], (const Uint8*)s->pixels + y * s->pitch, s->w);
		SDL_UnlockSurface(s);
		pic->pal.assign(src->getPalette(), src->getPalette() + 256);
		pic->file = "(image)";
		preparePicture(*pic);
	}
	std::unique_ptr<Surface> proxy(new Surface(fw, fh, 0, 0));
	proxy->setPalette(src->getPalette(), 0, 256);
	makeProxy(proxy.get(), *pic, rx, ry, rw, rh, fw, fh, src->getPalette());
	attach(proxy.get(), pic, rx, ry, rw, rh, "fit");
	Log(LOG_INFO) << "HiResAssets: image " << s->w << "x" << s->h << " larger than its box " << boxW << "x" << boxH << ": drawn hi-res at " << fw << "x" << fh;
	Surface *ret = proxy.get();
	fitted[key] = std::move(proxy);
	return ret;
}

void forget(const SDL_Surface *surface)
{
	if (!surface || bySurface.empty())
		return;
	if (!fitted.empty() && !inFitForget)
	{
		auto lo = fitted.lower_bound(FitKey{ surface, INT_MIN, INT_MIN });
		if (lo != fitted.end() && lo->first.src == surface)
		{
			inFitForget = true;
			while (lo != fitted.end() && lo->first.src == surface)
				lo = fitted.erase(lo);
			inFitForget = false;
		}
	}
	auto it = bySurface.find(surface);
	if (it == bySurface.end())
		return;
	dropRendered(it->second);
	byId.erase(it->second);
	bySurface.erase(it);
	registeredCount = bySurface.size();
}

void onDraw(const SDL_Surface *src, int sx, int sy, int sw, int sh, SDL_Surface *dst, int dx, int dy, int dw, int dh)
{
	if (!src || !dst || !HiResLayer::recording() || !enabled())
		return; // switched off in a game: proxies stay until the restart, drawn 8bpp
	auto it = bySurface.find(src);
	if (it == bySurface.end())
		return;
	Entry *e = find(it->second);
	if (!e || e->pixels != src->pixels || e->w != src->w || e->h != src->h)
	{
		forget(src); // the surface was re-created behind our back
		return;
	}
	const bool magnified = dw > 0 && dh > 0;
	if (magnified)
	{
		if (sx < 0 || sy < 0 || sx + sw > e->w || sy + sh > e->h || sw <= 0 || sh <= 0)
			return;
	}
	else
	{
		// clamp the source rect to the proxy
		if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
		if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
		sw = std::min(sw, e->w - sx);
		sh = std::min(sh, e->h - sy);
		// and to the destination (like the 8bpp blit: nothing outside the target surface)
		if (dx < 0) { sx -= dx; sw += dx; dx = 0; }
		if (dy < 0) { sy -= dy; sh += dy; dy = 0; }
		sw = std::min(sw, dst->w - dx);
		sh = std::min(sh, dst->h - dy);
		if (sw <= 0 || sh <= 0)
			return;
		dw = sw;
		dh = sh;
	}
	HiResCmd c;
	c.kind = HiResCmdKind::Image;
	c.assetId = e->id;
	c.x = (Sint16)dx; c.y = (Sint16)dy; c.w = (Sint16)dw; c.h = (Sint16)dh;
	c.srcX = (Sint16)sx; c.srcY = (Sint16)sy; c.srcW = (Sint16)sw; c.srcH = (Sint16)sh;
	c.clipX = (Sint16)dx; c.clipY = (Sint16)dy; c.clipW = (Sint16)dw; c.clipH = (Sint16)dh;
	HiResLayer::recordAsset(dst, c, src);
}

const Uint8 *expected(Uint32 assetId, int &w, int &h)
{
	Entry *e = find(assetId);
	if (!e)
		return nullptr;
	w = e->w; h = e->h;
	return e->expected.data();
}

Uint32 paletteKey(Uint32 assetId, const SDL_Palette *palette)
{
	Entry *e = find(assetId);
	if (!e || e->pic->trueColor || e->pic->ownPalette || !palette || palette->ncolors <= 0)
		return 0;
	Uint32 hsh = 2166136261u;
	for (int i = 0; i < palette->ncolors; ++i)
	{
		const SDL_Color &c = palette->colors[i];
		hsh = (hsh ^ c.r) * 16777619u; hsh = (hsh ^ c.g) * 16777619u; hsh = (hsh ^ c.b) * 16777619u;
	}
	if (!hsh) hsh = 1;
	auto &p = palettes[hsh];
	if (p.empty())
	{
		p.assign(256, SDL_Color{0, 0, 0, 255});
		std::copy(palette->colors, palette->colors + std::min(256, palette->ncolors), p.begin());
		if (palettes.size() > 64)
		{
			// keep the table small (palette fades): only the current one is needed again
			std::vector<SDL_Color> keep = p;
			palettes.clear();
			palettes[hsh] = keep;
			renderCache.clear();
			renderBytes = 0;
		}
	}
	return hsh;
}

const Uint32 *rendered(Uint32 assetId, Uint32 palKey, int sx, int sy, int sw, int sh, int outW, int outH)
{
	Entry *e = find(assetId);
	if (!e || outW <= 0 || outH <= 0 || sw <= 0 || sh <= 0)
		return nullptr;
	for (auto it = renderCache.begin(); it != renderCache.end(); ++it)
	{
		if (it->id == assetId && it->palKey == palKey && it->sx == sx && it->sy == sy && it->sw == sw && it->sh == sh && it->outW == outW && it->outH == outH)
		{
			if (it != renderCache.begin())
				renderCache.splice(renderCache.begin(), renderCache, it);
			return renderCache.front().px.data();
		}
	}
	const SDL_Color *pal = nullptr;
	if (!e->pic->trueColor && !e->pic->ownPalette)
	{
		auto p = palettes.find(palKey);
		if (p == palettes.end())
			return nullptr;
		pal = p->second.data();
	}
	Rendered r{ assetId, palKey, sx, sy, sw, sh, outW, outH, {} };
	const double fx = e->rw / e->w, fy = e->rh / e->h;
	resample(*e->pic, pal, e->rx + sx * fx, e->ry + sy * fy, sw * fx, sh * fy, outW, outH, r.px);
	renderBytes += r.px.size() * sizeof(Uint32);
	renderCache.push_front(std::move(r));
	while (renderBytes > RENDER_CACHE_LIMIT && renderCache.size() > 1)
	{
		renderBytes -= renderCache.back().px.size() * sizeof(Uint32);
		renderCache.pop_back();
	}
	return renderCache.front().px.data();
}

}
}
