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
#include "HiResResample.h"
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
	std::vector<Uint8> mask;      ///< battle unit/terrain sheets: recolour mask (<file>_mask.png), channel 1 face, 2 hair, 3 utile, 4 rank; empty = none
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
std::unordered_map<const void*, const SDL_Surface*> byPixels; ///< proxy pixel buffer -> surface (SurfaceRaw blits)
std::unordered_map<Uint32, std::unique_ptr<Entry>> byId;
Uint32 idCounter = 0;
std::set<std::string> deniedTypes, deniedPrefixes;
std::set<std::string> battleTypes; ///< allowBattle
const SDL_Color *palGeo = nullptr, *palBase = nullptr, *palBattle = nullptr;

/// Palettes 8bpp pictures were resolved with (key -> colours).
std::unordered_map<Uint32, std::vector<SDL_Color>> palettes;

/// Resampled pictures (output resolution), most recently used first.
struct Rendered
{
	Uint32 id, palKey;
	int sx, sy, sw, sh, outW, outH, mode;
	std::vector<Uint32> px;
};
std::list<Rendered> renderCache;
/// index of renderCache by key hash (several entries may share a hash)
std::unordered_multimap<size_t, std::list<Rendered>::iterator> renderIndex;
inline size_t renderKey(Uint32 id, Uint32 palKey, int sx, int sy, int sw, int sh, int outW, int outH, int mode)
{
	size_t h = id * 0x9E3779B97F4A7C15ull;
	for (size_t v : { (size_t)palKey, (size_t)sx, (size_t)sy, (size_t)sw, (size_t)sh, (size_t)outW, (size_t)outH, (size_t)mode })
		h = (h ^ v) * 0x100000001B3ull;
	return h;
}
void unindex(std::list<Rendered>::iterator it)
{
	const size_t k = renderKey(it->id, it->palKey, it->sx, it->sy, it->sw, it->sh, it->outW, it->outH, it->mode);
	auto r = renderIndex.equal_range(k);
	for (auto i = r.first; i != r.second; ++i)
		if (i->second == it) { renderIndex.erase(i); break; }
}
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
			unindex(it);
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

/// Resamples the picture rect into outW x outH with the unified rule (HiResResample: exact = copy,
/// down = area average, up = HiResResample::mode(), from the screen filter). 8bpp pictures: palette first (index 0 = alpha 0),
/// then the filter. PREMULTIPLIED ARGB result.
void resample(const Picture &p, const SDL_Color *pal, double rx, double ry, double rw, double rh, int outW, int outH, int mode, std::vector<Uint32> &out)
{
	std::vector<Uint32> straight;
	const Uint32 *src = nullptr;
	if (p.trueColor || p.ownPalette)
		src = p.argb.data();
	else
	{
		straight.resize((size_t)p.w * p.h);
		for (int y = 0; y < p.h; ++y)
			for (int x = 0; x < p.w; ++x)
				straight[(size_t)y * p.w + x] = pixelOf(p, pal, x, y);
		src = straight.data();
	}
	HiResResample::resample(src, p.w, p.h, rx, ry, rw, rh, outW, outH, mode, out);
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
bool loadedEnabled = false, loadedBattle = false;
std::map<std::string, const SDL_Color*> typePalettes;
std::set<std::string> attachFitTypes, nativeTypes, nativePrefixes;
/// native-only pictures (not drawn by the overlay), per proxy surface
std::unordered_map<const SDL_Surface*, Uint32> nativeBySurface;
}

bool enabled()
{
	return loadedEnabled;
}

bool battleEnabled()
{
	return loadedEnabled && loadedBattle;
}

void beginModLoad()
{
	Options::normalizeHiResLevel();
	loadedEnabled = Options::oxceHiResAssets;
	loadedBattle = Options::oxceHiResLevel >= 2;
	typePalettes.clear();
	attachFitTypes.clear();
	nativeTypes.clear();
	nativePrefixes.clear();
	deniedTypes.clear();
	deniedPrefixes.clear();
	battleTypes.clear();
}


void allowBattle(const std::string &type)
{
	deniedTypes.erase(type);
	battleTypes.insert(type);
}

bool battleType(const std::string &type)
{
	return battleTypes.count(type) != 0;
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

void allowNative(const std::string &type)
{
	nativeTypes.insert(type);
}

void allowNativePrefix(const std::string &prefix)
{
	if (!prefix.empty())
		nativePrefixes.insert(prefix);
}

bool nativeAllowed(const std::string &type)
{
	if (nativeTypes.count(type))
		return true;
	for (const auto &pre : nativePrefixes)
		if (type.compare(0, pre.size(), pre) == 0)
			return true;
	return false;
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

const SDL_Color *battlePalette()
{
	return palBattle;
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

bool loadDeniedTrueColor(Surface *target, const std::string &filename, const std::string &type, int boxW, int boxH, const SDL_Color *origPalette)
{
	try
	{
		target->loadImage(filename);
		return true; // 8bpp (or anything the normal loader accepts): as before
	}
	catch (Exception &)
	{
		std::shared_ptr<Picture> pic = decode(filename);
		if (!pic || !pic->trueColor)
			throw; // truly broken file: error as before (the mod is disabled)
		const bool battle = !trueColorAllowed(type);
		const SDL_Color *pal = battle && palBattle ? palBattle : quantizePalette(type, origPalette);
		if (!pal)
			pal = palBattle ? palBattle : palGeo;
		if (!pal)
			throw;
		int fw, fh;
		double scale;
		fitBox(boxW, boxH, pic->w, pic->h, fw, fh, scale);
		Log(LOG_WARNING) << "HiResAssets: " << type << ": " << filename << " is a " << pic->w << "x" << pic->h << " 32-bit/JPG image"
			<< (enabled() ? " (battlescape resource: 8-bit only)" : " (hi-res images option off)")
			<< ": converted to 8-bit " << fw << "x" << fh << " in the " << (pal == palBattle ? "battlescape" : "type") << " palette";
		std::vector<SDL_Color> spal(pal, pal + 256);
		if (origPalette && !battle)
			spal.assign(origPalette, origPalette + 256);
		forget(target->getSurface());
		*target = Surface(fw, fh, 0, 0);
		target->setPalette(spal.data(), 0, 256);
		if (SDL_Surface *ts = target->getSurface())
			SDL_FillRect(ts, 0, 0);
		makeProxy(target, *pic, 0, 0, pic->w, pic->h, fw, fh, pal);
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
	if (palBattle && battleTypes.count(type))
		return palBattle;
	if (type == "BASEBITS.PCK" && palBase)
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
		// battlescape palette: 240-255 differ between PAL_BATTLESCAPE (corrected grey ramp) and
		// PAL_BATTLEPEDIA (Ufopaedia battle pages) -> only the shared indices 1-239
		const int last = palette == palBattle ? 239 : 255;
		for (int i = 1; i <= last; ++i)
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
	byPixels[s->pixels] = s;
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

namespace
{
/// 32-bit picture of a native-canvas type: battlescape-palette proxy for the game (fitted into the
/// box, or at the box size with the picture top-left when it fits, or at file size without a box),
/// the picture kept for the native inventory canvas only.
bool loadNative(Surface *target, const std::shared_ptr<Picture> &pic, const std::string &filename, int boxW, int boxH, const std::string &type)
{
	int fw, fh;
	double scale;
	const bool larger = fitBox(boxW, boxH, pic->w, pic->h, fw, fh, scale);
	const bool boxKnown = boxW < (1 << 15) && boxH < (1 << 15);
	int pw, ph, lw, lh;
	if (!boxKnown) { pw = lw = pic->w; ph = lh = pic->h; }
	else if (larger) { pw = lw = fw; ph = lh = fh; }
	else { pw = boxW; ph = boxH; lw = pic->w; lh = pic->h; }
	forget(target->getSurface());
	*target = Surface(pw, ph, 0, 0);
	target->setPalette(palBattle, 0, 256);
	SDL_Surface *s = target->getSurface();
	if (!s)
		return false;
	SDL_FillRect(s, 0, 0);
	makeProxy(target, *pic, 0, 0, pic->w, pic->h, lw, lh, palBattle);
	if (pw == lw && ph == lh)
	{
		// the proxy is the picture's box: one picture for every consumer - the overlay (Ufopaedia item
		// and armor pages, soldier avatar, unshaded blits) and the native inventory canvas
		attach(target, pic, 0, 0, pic->w, pic->h, type);
		Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is a 32-bit image of " << pic->w << "x" << pic->h
			<< (boxKnown ? "" : " (no box)") << ": hi-res picture at " << lw << "x" << lh << " (overlay and native canvas), battlescape-palette proxy for the game";
		return true;
	}
	auto e = std::make_unique<Entry>();
	if (++idCounter == 0) ++idCounter;
	e->id = idCounter;
	e->pic = pic;
	e->rx = 0; e->ry = 0; e->rw = pic->w; e->rh = pic->h;
	e->w = lw; e->h = lh;
	e->surface = s;
	e->pixels = s->pixels;
	e->name = type;
	nativeBySurface[s] = e->id;
	byId[e->id] = std::move(e);
	Log(LOG_INFO) << "HiResAssets: " << type << ": " << filename << " is a 32-bit image of " << pic->w << "x" << pic->h
		<< (boxKnown ? "" : " (no box)") << ": native canvas picture at " << lw << "x" << lh << " (logical), " << pw << "x" << ph
		<< " battlescape-palette proxy for the game";
	return true;
}
}

namespace
{
Entry *nativeEntry(const SDL_Surface *surface)
{
	if (!surface || !enabled())
		return nullptr;
	Uint32 id = 0;
	auto it = nativeBySurface.find(surface);
	if (it != nativeBySurface.end())
		id = it->second;
	else
	{
		auto jt = bySurface.find(surface);
		if (jt == bySurface.end())
			return nullptr;
		id = jt->second;
	}
	Entry *e = find(id);
	if (!e || e->pixels != surface->pixels)
		return nullptr;
	return e;
}
}

bool nativeInfo(const SDL_Surface *surface, int &lw, int &lh)
{
	Entry *e = nativeEntry(surface);
	if (!e)
		return false;
	lw = e->w; lh = e->h;
	return true;
}

const Uint32 *nativeRendered(const SDL_Surface *surface, const SDL_Palette *palette, int outW, int outH)
{
	Entry *e = nativeEntry(surface);
	if (!e)
		return nullptr;
	const Uint32 pk = paletteKey(e->id, palette);
	if (!pk && !e->pic->trueColor && !e->pic->ownPalette)
		return nullptr;
	return rendered(e->id, pk, 0, 0, e->w, e->h, outW, outH);
}

bool loadReplacement(Surface *target, const std::string &filename, int boxW, int boxH, const SDL_Color *origPalette, const std::string &type)
{
	if (!enabled() || !target || boxW <= 0 || boxH <= 0)
		return false;
	std::shared_ptr<Picture> pic = decode(filename);
	if (!pic)
		return false; // the normal loader reports the error
	if (pic->trueColor && battleTypes.count(type))
		loadMask(*pic, filename);
	if (pic->trueColor && !trueColorAllowed(type))
	{
		if (palBattle && nativeAllowed(type))
			return loadNative(target, pic, filename, boxW, boxH, type);
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
	if (!surface || (bySurface.empty() && nativeBySurface.empty()))
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
	auto nt = nativeBySurface.find(surface);
	if (nt != nativeBySurface.end())
	{
		dropRendered(nt->second);
		byId.erase(nt->second);
		nativeBySurface.erase(nt);
	}
	auto it = bySurface.find(surface);
	if (it == bySurface.end())
		return;
	dropRendered(it->second);
	{
		auto pe = find(it->second);
		if (pe)
		{
			auto pj = byPixels.find(pe->pixels);
			if (pj != byPixels.end() && pj->second == surface)
				byPixels.erase(pj);
		}
	}
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
			renderCache.clear(); renderIndex.clear();
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
	const int mode = HiResResample::mode();
	{
		auto r = renderIndex.equal_range(renderKey(assetId, palKey, sx, sy, sw, sh, outW, outH, mode));
		for (auto i = r.first; i != r.second; ++i)
		{
			auto it = i->second;
			if (it->id == assetId && it->palKey == palKey && it->sx == sx && it->sy == sy && it->sw == sw && it->sh == sh && it->outW == outW && it->outH == outH && it->mode == mode)
			{
				if (it != renderCache.begin())
					renderCache.splice(renderCache.begin(), renderCache, it); // iterators stay valid
				return renderCache.front().px.data();
			}
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
	Rendered r{ assetId, palKey, sx, sy, sw, sh, outW, outH, mode, {} };
	const double fx = e->rw / e->w, fy = e->rh / e->h;
	resample(*e->pic, pal, e->rx + sx * fx, e->ry + sy * fy, sw * fx, sh * fy, outW, outH, mode, r.px);
	renderBytes += r.px.size() * sizeof(Uint32);
	renderCache.push_front(std::move(r));
	renderIndex.emplace(renderKey(assetId, palKey, sx, sy, sw, sh, outW, outH, mode), renderCache.begin());
	while (renderBytes > RENDER_CACHE_LIMIT && renderCache.size() > 1)
	{
		renderBytes -= renderCache.back().px.size() * sizeof(Uint32);
		unindex(std::prev(renderCache.end()));
		renderCache.pop_back();
	}
	return renderCache.front().px.data();
}


void loadMask(Picture &pic, const std::string &filename)
{
	pic.mask.clear();
	const size_t dot = filename.find_last_of('.');
	const size_t slash = filename.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return;
	const std::string mf = filename.substr(0, dot) + "_mask" + filename.substr(dot);
	if (!FileMap::fileExists(mf))
		return;
	Picture m;
	if (!decodeTrueColor(mf, m) || m.w != pic.w || m.h != pic.h)
	{
		Log(LOG_WARNING) << "HiResAssets: mask " << mf << " ignored (must be a 32-bit image of " << pic.w << "x" << pic.h << ")";
		return;
	}
	pic.mask.assign((size_t)pic.w * pic.h, 0);
	size_t n = 0;
	for (size_t i = 0; i < pic.mask.size(); ++i)
	{
		const Uint32 p = m.argb[i];
		if ((p >> 24) < 128)
			continue;
		const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
		Uint8 ch = 0;
		if (r >= 128 && g >= 128 && b >= 128) ch = 3;      // white: utile
		else if (r >= 128 && g < 128 && b < 128) ch = 1;   // red: face
		else if (g >= 128 && r < 128 && b < 128) ch = 2;   // green: hair
		else if (b >= 128 && r < 128 && g < 128) ch = 4;   // blue: rank
		pic.mask[i] = ch;
		n += ch != 0;
	}
	Log(LOG_INFO) << "HiResAssets: recolour mask " << mf << " (" << n << " pixels)";
}

namespace
{
struct MaskRendered
{
	Uint32 id;
	int sx, sy, sw, sh, outW, outH;
	std::vector<Uint8> px;
};
std::list<MaskRendered> maskCache;
size_t maskBytes = 0;
}

bool nativeScale(Uint32 assetId, double &fx, double &fy)
{
	Entry *e = find(assetId);
	if (!e || e->w <= 0 || e->h <= 0)
		return false;
	fx = e->rw / e->w;
	fy = e->rh / e->h;
	return true;
}

const Uint8 *renderedMask(Uint32 assetId, int sx, int sy, int sw, int sh, int outW, int outH)
{
	Entry *e = find(assetId);
	if (!e || e->pic->mask.empty() || outW <= 0 || outH <= 0 || sw <= 0 || sh <= 0)
		return nullptr;
	for (auto it = maskCache.begin(); it != maskCache.end(); ++it)
	{
		if (it->id == assetId && it->sx == sx && it->sy == sy && it->sw == sw && it->sh == sh && it->outW == outW && it->outH == outH)
		{
			if (it != maskCache.begin())
				maskCache.splice(maskCache.begin(), maskCache, it);
			return maskCache.front().px.data();
		}
	}
	MaskRendered r{ assetId, sx, sy, sw, sh, outW, outH, std::vector<Uint8>((size_t)outW * outH, 0) };
	const double fx = e->rw / e->w, fy = e->rh / e->h;
	const double px0 = e->rx + sx * fx, py0 = e->ry + sy * fy, pw = sw * fx, ph = sh * fy;
	const Picture &p = *e->pic;
	for (int y = 0; y < outH; ++y)
	{
		const int v = std::min(p.h - 1, std::max(0, (int)(py0 + (y + 0.5) * ph / outH)));
		for (int x = 0; x < outW; ++x)
		{
			const int u = std::min(p.w - 1, std::max(0, (int)(px0 + (x + 0.5) * pw / outW)));
			r.px[(size_t)y * outW + x] = p.mask[(size_t)v * p.w + u];
		}
	}
	maskBytes += r.px.size();
	maskCache.push_front(std::move(r));
	while (maskBytes > 32u * 1024u * 1024u && maskCache.size() > 1)
	{
		maskBytes -= maskCache.back().px.size();
		maskCache.pop_back();
	}
	return maskCache.front().px.data();
}

int shadeFactor(int s)
{
	// Options::oxceHiResShadeCurve: brightness % per shade 0..15 (user-tunable), shade 16 = black
	static std::string parsed = "\x01";
	static int table[17];
	if (parsed != Options::oxceHiResShadeCurve)
	{
		parsed = Options::oxceHiResShadeCurve;
		std::vector<int> v;
		std::string cur;
		for (char ch : parsed + ",")
		{
			if (ch == ',' || ch == ' ' || ch == ';')
			{
				if (!cur.empty()) v.push_back(std::max(0, std::min(100, std::atoi(cur.c_str()))));
				cur.clear();
			}
			else
				cur += ch;
		}
		static const int def[16] = { 100, 92, 84, 76, 68, 60, 52, 44, 36, 28, 12, 10, 8, 6, 4, 2 };
		for (int k = 0; k < 16; ++k)
		{
			const int pct = v.empty() ? def[k] : v[std::min((size_t)k, v.size() - 1)];
			table[k] = (pct * 256 + 50) / 100;
		}
		table[16] = 0;
	}
	return table[std::max(0, std::min(16, s))];
}

namespace
{
const SDL_Surface *ownSurf = nullptr;
std::vector<Uint32> owners;
int ownW = 0, ownH = 0;
Uint32 ownCounter = 0;
/// Writes v into the ownership buffer for the opaque pixels of px blitted at (dx, dy), clipped.
void markOwner(const Uint8 *px, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH, Uint32 v)
{
	int x0 = dx + (half ? w / 2 : 0), y0 = dy, x1 = dx + w, y1 = dy + h;
	if (clipW > 0 && clipH > 0)
	{
		x0 = std::max(x0, clipX); y0 = std::max(y0, clipY);
		x1 = std::min(x1, clipX + clipW); y1 = std::min(y1, clipY + clipH);
	}
	x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, ownW); y1 = std::min(y1, ownH);
	for (int y = y0; y < y1; ++y)
	{
		const Uint8 *sp = px + (size_t)(y - dy) * pitch - dx;
		Uint32 *op = &owners[(size_t)y * ownW];
		for (int x = x0; x < x1; ++x)
			if (sp[x])
				op[x] = v;
	}
}
}


void battleBegin(SDL_Surface *map)
{
	ownSurf = map;
	ownW = map ? map->w : 0;
	ownH = map ? map->h : 0;
	owners.assign((size_t)ownW * ownH, 0);
	ownCounter = 0;
}

void battleMark8(const SDL_Surface *dst, const Uint8 *px, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH)
{
	if (!dst || dst != ownSurf || !px || owners.empty())
		return;
	if (HiResLayer::layered())
	{
		// layered battlescape: the 8bpp sprite becomes a magnified sprite command in the draw order
		HiResLayer::layerSprite8(const_cast<SDL_Surface*>(dst), px, w, h, pitch, dx, dy, half, clipX, clipY, clipW, clipH);
		return;
	}
	markOwner(px, w, h, pitch, dx, dy, half, clipX, clipY, clipW, clipH, 0);
}

const Uint32 *battleOwners(const SDL_Surface *s, int &w, int &h)
{
	if (!s || s != ownSurf || owners.empty())
		return nullptr;
	w = ownW; h = ownH;
	return owners.data();
}

const SDL_Surface *proxyByPixels(const void *pixels)
{
	auto it = byPixels.find(pixels);
	return it == byPixels.end() ? nullptr : it->second;
}

void onBattleDraw(const SDL_Surface *src, SDL_Surface *dst, int dx, int dy, int shade, bool half, int newBaseColor,
	const Uint8 rcFrom[4], const Uint8 rcTo[4], int clipX, int clipY, int clipW, int clipH)
{
	if (!src || !dst || !HiResLayer::recording() || !enabled())
		return;
	auto it = bySurface.find(src);
	if (it == bySurface.end())
		return;
	Entry *e = find(it->second);
	if (!e || e->pixels != src->pixels || e->w != src->w || e->h != src->h)
		return;
	if (!battleTypes.count(e->name))
		return;
	HiResCmd c;
	c.kind = HiResCmdKind::Image;
	c.assetId = e->id;
	c.battle = 1;
	c.shade = (Uint8)std::max(0, std::min(16, shade));
	c.nvColor = (Uint8)std::max(0, std::min(255, newBaseColor));
	for (int k = 0; k < 4; ++k)
	{
		c.rcFrom[k] = rcFrom ? rcFrom[k] : 0;
		c.rcTo[k] = rcTo ? rcTo[k] : 0;
	}
	c.x = (Sint16)dx; c.y = (Sint16)dy; c.w = (Sint16)e->w; c.h = (Sint16)e->h;
	c.srcX = 0; c.srcY = 0; c.srcW = (Sint16)e->w; c.srcH = (Sint16)e->h;
	int vx = dx + (half ? e->w / 2 : 0), vy = dy, vw = e->w - (half ? e->w / 2 : 0), vh = e->h;
	if (clipW > 0 && clipH > 0)
	{
		const int x0 = std::max(vx, clipX), y0 = std::max(vy, clipY);
		const int x1 = std::min(vx + vw, clipX + clipW), y1 = std::min(vy + vh, clipY + clipH);
		vx = x0; vy = y0; vw = x1 - x0; vh = y1 - y0;
	}
	if (vw <= 0 || vh <= 0)
		return;
	c.clipX = (Sint16)vx; c.clipY = (Sint16)vy; c.clipW = (Sint16)vw; c.clipH = (Sint16)vh;
	if (dst == ownSurf && HiResLayer::layered())
	{
		c.layered = 1; // draw order decides, no ownership / pixel match
		HiResLayer::layerMark(dst, e->expected.data(), e->w, e->h, e->w, dx, dy, half, vx, vy, vw, vh);
	}
	else if (dst == ownSurf && !owners.empty())
	{
		c.battleId = ++ownCounter;
		markOwner(e->expected.data(), e->w, e->h, e->w, dx, dy, half, vx, vy, vw, vh, c.battleId);
	}
	HiResLayer::recordAsset(dst, c, src);
}

}
}
