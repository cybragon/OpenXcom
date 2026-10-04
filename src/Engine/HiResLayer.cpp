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
#include "UiImageScaler.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <map>
#include <cstdlib>

namespace OpenXcom
{

bool HiResLayer::_recording = false;
bool HiResLayer::_textConfigured = false;

namespace
{

struct SlotState
{
	HiResFont *font = nullptr;   ///< font chain used by the slot (owned by LayerState::fonts)
	int sizePct = 100;           ///< per font-type size (%), multiplied with the global size
	bool resolved = false;
};

/// Cached pixel sizes for one (slot, cell metrics, scale) combination.
struct SizeKey
{
	int slot, target64, cls, refW64;
	bool operator==(const SizeKey &o) const { return slot == o.slot && target64 == o.target64 && cls == o.cls && refW64 == o.refW64; }
};
struct SizeKeyHash
{
	size_t operator()(const SizeKey &k) const { return ((size_t)k.slot * 1000003u) ^ ((size_t)k.target64 * 9176u) ^ ((size_t)k.cls << 28) ^ ((size_t)k.refW64 * 31u); }
};

struct LayerState
{
	std::map<std::string, std::unique_ptr<HiResFont>> fonts;          ///< loaded font chains by key
	HiResFont *defaultFont = nullptr;
	std::vector<std::string> defaultSpecs;
	std::vector<std::string> slotIds;
	std::vector<SlotState> slots;
	std::unordered_map<SizeKey, int, SizeKeyHash> sizeCache;
	std::unordered_map<SDL_Surface*, std::vector<HiResCmd>> lists;   ///< commands held by (non-screen) surfaces
	SDL_Surface *screen = nullptr;
	int baseW = 0, baseH = 0;
	bool outputSupported = false;
	bool modeChanged = false;
	std::vector<Uint32> stamps;        ///< per base pixel: id of last opaque blit
	Uint32 stampCounter = 0;
	Uint32 runCounter = 0;
	std::vector<HiResCmd> frame;       ///< commands that reached the screen this frame
	// composition buffer (premultiplied BGRA, output resolution without black bands)
	std::vector<Uint32> rgba;
	int bufW = 0, bufH = 0;
	SDL_Rect prevDirty = {0, 0, 0, 0};
#ifndef __NO_OPENGL
	GLuint tex = 0;
	int texW = 0, texH = 0;
#endif
	int sizePct = 100, outlinePct = 100, boldPct = 40, layout = 1;
	HiResFont::Settings settings;
};

LayerState S;

/// The layer is also needed without hi-res text: in "x1, UI optimized" (software path) the UI
/// panels magnified with HQX/xBRZ are true-colour pictures drawn by this layer (image commands).
/// Text stays bitmap then (canRenderGlyph/textConfigured are false).
bool uiImagesWanted()
{
	const UiImageScaler::Filter f = UiImageScaler::uiFilter();
	return f == UiImageScaler::Filter::HQX || f == UiImageScaler::Filter::XBRZ;
}

struct ImageRef
{
	const Uint32 *px = nullptr;
	int w = 0, h = 0;
};
std::unordered_map<Uint32, ImageRef> images;
Uint32 imageCounter = 0;

/// Draws an image command (straight-alpha ARGB, 'over') into the composition buffer, pixel by
/// pixel hidden by anything blitted later (stamps), like the glyphs.
void drawImage(const HiResCmd &c, int dstW, int dstH, double sx, double sy, SDL_Rect &dirty);


std::vector<std::string> splitList(const std::string &s, char sep = ';')
{
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string item;
	while (std::getline(ss, item, sep))
	{
		size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
		if (a != std::string::npos)
			out.push_back(item.substr(a, b - a + 1));
		else if (sep != ';')
			out.push_back("");
	}
	return out;
}

/// oxceHiResFontMap: "FONT_BIG=malgun.ttf,110;FONT_SMALL=,90" -> id -> (spec, size%)
std::map<std::string, std::pair<std::string, int>> parseFontMap(const std::string &s)
{
	std::map<std::string, std::pair<std::string, int>> m;
	for (const auto &entry : splitList(s))
	{
		size_t eq = entry.find('=');
		if (eq == std::string::npos)
			continue;
		std::string id = entry.substr(0, eq);
		std::string rest = entry.substr(eq + 1);
		std::string spec = rest;
		int size = 100;
		size_t comma = rest.rfind(',');
		if (comma != std::string::npos)
		{
			spec = rest.substr(0, comma);
			size = std::atoi(rest.c_str() + comma + 1);
			if (size <= 0) size = 100;
		}
		m[id] = std::make_pair(spec, size);
	}
	return m;
}

std::string formatFontMap(const std::map<std::string, std::pair<std::string, int>> &m)
{
	std::string out;
	for (const auto &e : m)
	{
		if (e.second.first.empty() && e.second.second == 100)
			continue;
		if (!out.empty()) out += ";";
		out += e.first + "=" + e.second.first;
		if (e.second.second != 100) out += "," + std::to_string(e.second.second);
	}
	return out;
}

HiResFont *getFontChain(const std::vector<std::string> &specs)
{
	std::string key = std::to_string(S.settings.hinting) + (S.settings.antialias ? "a" : "m");
	for (const auto &sp : specs) key += "|" + sp;
	auto it = S.fonts.find(key);
	if (it != S.fonts.end())
		return it->second.get();
	auto font = std::make_unique<HiResFont>();
	if (font->load(specs, S.settings) <= 0)
	{
		S.fonts[key] = nullptr;
		return nullptr;
	}
	HiResFont *raw = font.get();
	S.fonts[key] = std::move(font);
	return raw;
}

SlotState &slotState(int slot)
{
	static SlotState none;
	if (slot < 0 || slot >= (int)S.slotIds.size())
	{
		none.font = S.defaultFont;
		none.sizePct = 100;
		return none;
	}
	if ((int)S.slots.size() < (int)S.slotIds.size())
		S.slots.resize(S.slotIds.size());
	SlotState &st = S.slots[slot];
	if (!st.resolved)
	{
		st.resolved = true;
		st.font = S.defaultFont;
		st.sizePct = 100;
		auto m = parseFontMap(Options::oxceHiResFontMap);
		auto it = m.find(S.slotIds[slot]);
		if (it != m.end())
		{
			st.sizePct = std::max(10, it->second.second);
			if (!it->second.first.empty() && S.defaultFont)
			{
				std::vector<std::string> specs = splitList(it->second.first);
				specs.insert(specs.end(), S.defaultSpecs.begin(), S.defaultSpecs.end());
				if (HiResFont *f = getFontChain(specs))
					st.font = f;
			}
		}
	}
	return st;
}

inline HiResFont *slotFont(int slot) { return slotState(slot).font; }

inline bool intersect(int &x, int &y, int &w, int &h, int x2, int y2, int w2, int h2)
{
	int nx = std::max(x, x2), ny = std::max(y, y2);
	int nr = std::min(x + w, x2 + w2), nb = std::min(y + h, y2 + h2);
	x = nx; y = ny; w = nr - nx; h = nb - ny;
	return w > 0 && h > 0;
}

/// Wide (CJK/Hangul) glyphs are sized/placed like the bitmap U+AC00, everything else like 'H'.
inline bool isWide(UCode c)
{
	return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) ||
		(c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x3130 && c <= 0x318F);
}

/// Emboldening in 1/64 px, proportional to the glyph size: oxceHiResTextBold is in 1/10 % of the em
/// (40 = 4% em; bitmap fonts have ~0.11-0.125 em strokes, typical TTF regular weights ~0.07-0.08 em).
inline double boldRatio() { return S.boldPct / 1000.0; }
/// Font sizes are kept in 1/4 px ("size4"), see HiResFont.
inline int bold64ForSize(int size4) { return (int)std::lround(size4 / 4.0 * boldRatio() * 64.0); }

/// Glyph body height (Latin capital 'H', Hangul '각') and outline thickness of a font type, in base
/// (1x) pixels; both are multiplied by the output scale and the size options.
struct FontRule
{
	double body, outline;
};
FontRule fontRule(int slot, int lineH)
{
	static const std::pair<const char*, FontRule> rules[] = {
		{ "FONT_BIG", { 11.0, 1.0 } },      // line 16
		{ "FONT_SMALL", { 7.0, 0.5 } },     // line 9
		{ "FONT_GEO_BIG", { 7.0, 1.0 } },   // line 9
		{ "FONT_GEO_SMALL", { 5.0, 0.5 } }, // line 7
	};
	if (slot >= 0 && slot < (int)S.slotIds.size())
		for (const auto &r : rules)
			if (S.slotIds[slot] == r.first)
				return r.second;
	// other font types (mods): about the proportions of the original fonts
	const double h = std::max(1, lineH);
	return FontRule{ std::max(1.0, std::round(h * 0.7)), h >= 12 ? 1.0 : 0.5 };
}

/// Reference glyph of the size: Hangul '각', from whichever face of the font chain draws it (normally
/// the fallback font: the primary default Segoe UI has no Hangul). All glyphs - Latin, digits, Hangul,
/// Chinese, Japanese - use this one size (same px/em in every face). Latin descenders (g p q y j) and
/// Hangul taller than '각' (뷁...) may extend beyond the body. 'H' only if no face has '각'.
const UCode SIZE_REF = 0xAC01, SIZE_REF_NO_HANGUL = 'H';

/// Size of one font type (slot) at one output scale.
struct SlotSize
{
	int size4 = 4;                  ///< font size in 1/4 px (em), for every glyph
	double asc = 0.0, desc = 0.0;   ///< ink of the reference glyph above / below the baseline (emboldening included)
	double r = 0.0;                 ///< outline thickness (stroke radius, output px)
	double target = 0.0;            ///< wanted body height (output px)
};
std::unordered_map<SizeKey, SlotSize, SizeKeyHash> slotSizeCache;

/// Ink extent (above / below the baseline) of a reference glyph.
bool inkExtent(HiResFont *font, UCode ref, int size4, double &asc, double &desc)
{
	asc = 0.0; desc = 0.0;
	if (!font->hasGlyph(ref))
		return false;
	const HiResGlyph &g = font->getGlyph(ref, size4, 0, bold64ForSize(size4));
	if (!g.ok || g.fillH <= 0)
		return false;
	asc = g.fillT;
	desc = g.fillH - g.fillT;
	return true;
}

/// Font size of a font type: the largest size (in 1/4 px steps) whose '각' (emboldening included)
/// is not taller than the body height fontRule().body x output scale x size options (ink heights are
/// whole pixels: a fractional target is rounded down). The outline is fontRule().outline x output
/// scale x size options x oxceHiResTextOutlineScale. Depends only on the font type, the output scale
/// and the options - never on the text or its box.
const SlotSize &slotSize(HiResFont *font, int slot, int lineH, double sy)
{
	const FontRule rule = fontRule(slot, lineH);
	const int pct = S.sizePct * slotState(slot).sizePct / 100;
	SizeKey key{ slot, (int)std::lround(sy * 64), S.outlinePct, pct * 1000 + std::min(999, std::max(0, lineH)) };
	auto it = slotSizeCache.find(key);
	if (it != slotSizeCache.end())
		return it->second;
	SlotSize ss;
	ss.target = rule.body * sy * pct / 100.0;
	ss.r = rule.outline * sy * pct / 100.0 * S.outlinePct / 100.0;
	const double limit = std::max(1.0, std::floor(ss.target + 1e-6));
	const UCode ref = font->hasGlyph(SIZE_REF) ? SIZE_REF : SIZE_REF_NO_HANGUL;
	double a, d;
	const int probe4 = 64 * 4;
	if (font->hasGlyph(ref) && inkExtent(font, ref, probe4, a, d) && a + d > 0)
	{
		// emboldening grows the ink by em * boldRatio: em * ((a + d) / 64 + b) = target
		int s4 = std::max(4, (int)std::floor(4.0 * ss.target / ((a + d) / 64.0 + boldRatio())));
		while (s4 > 4 && inkExtent(font, ref, s4, a, d) && a + d > limit)
			--s4;
		while (s4 < 16000 && inkExtent(font, ref, s4 + 1, a, d) && a + d <= limit)
			++s4;
		ss.size4 = s4;
		inkExtent(font, ref, s4, ss.asc, ss.desc);
	}
	else
	{
		ss.size4 = std::max(4, (int)std::lround(4.0 * ss.target / 0.72));
		ss.asc = ss.target;
		ss.desc = 0.0;
	}
	double ha = 0.0, hd = 0.0;
	inkExtent(font, 'H', ss.size4, ha, hd);
	const int refFace = font->faceFor(ref), hFace = font->faceFor('H');
	const auto &names = font->getFaceNames();
	auto faceName = [&](int f) { return f >= 0 && f < (int)names.size() ? names[f] : std::string("-"); };
	Log(LOG_INFO) << "HiResText: " << (slot >= 0 && slot < (int)S.slotIds.size() ? S.slotIds[slot] : std::string("default"))
		<< " x" << sy << ": body " << rule.body << " -> " << ss.target << " px, outline " << ss.r
		<< " px; size " << ss.size4 / 4.0 << " px from " << (ref == SIZE_REF ? "\xEA\xB0\x81" : "H") << " (" << faceName(refFace)
		<< ") ink " << ss.asc + ss.desc << " (" << ss.asc << "/" << ss.desc << "); H (" << faceName(hFace) << ") ink "
		<< ha + hd << " (" << ha << "/" << hd << ")";
	return slotSizeCache[key] = ss;
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

void drawImage(const HiResCmd &c, int dstW, int dstH, double sx, double sy, SDL_Rect &dirty)
{
	auto it = images.find(c.imageId);
	if (it == images.end() || !it->second.px || c.w <= 0 || c.h <= 0)
		return;
	const ImageRef &img = it->second;
	const int x0 = std::max(0, (int)std::floor(c.clipX * sx)), y0 = std::max(0, (int)std::floor(c.clipY * sy));
	const int x1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
	const int y1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
	if (x0 >= x1 || y0 >= y1)
		return;
	// output pixel -> picture pixel (the picture spans the command box c.x/c.y/c.w/c.h)
	const double fx = img.w / (c.w * sx), fy = img.h / (c.h * sy);
	const double ox = c.x * sx, oy = c.y * sy;
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	for (int y = y0; y < y1; ++y)
	{
		const int iy = (int)((y - oy) * fy);
		if (iy < 0 || iy >= img.h)
			continue;
		const int by = std::min(S.baseH - 1, (int)(y * isy));
		const Uint32 *stampRow = &S.stamps[by * S.baseW];
		const Uint32 *src = img.px + (size_t)iy * img.w;
		Uint32 *dst = &S.rgba[y * S.bufW];
		for (int x = x0; x < x1; ++x)
		{
			const int ix = (int)((x - ox) * fx);
			if (ix < 0 || ix >= img.w)
				continue;
			const Uint32 p = src[ix];
			const unsigned a = p >> 24;
			if (!a)
				continue;
			const int bx = std::min(S.baseW - 1, (int)(x * isx));
			if (stampRow[bx] > c.stamp)
				continue;
			if (a == 255)
			{
				dst[x] = p;
				continue;
			}
			const SDL_Color col = { (Uint8)((p >> 16) & 0xFF), (Uint8)((p >> 8) & 0xFF), (Uint8)(p & 0xFF), (Uint8)255 };
			blendPixel(dst[x], col, a);
		}
	}
	unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
}

/// Placement of one glyph command in output pixels.
struct Placed
{
	const HiResGlyph *g = nullptr;
	int pen = 0, baseline = 0;
};

/// Vertical placement, font size and outline of one glyph command. The body of the font type (ink
/// box of '각' at the font type's size, shared by all glyphs, so Latin and Hangul share the baseline) is centred on the body of the bitmap capital 'H' of the same font (its
/// top line and baseline; bitmap '가' or the cell if the bitmap font has no 'H'). If the body plus
/// outline sticks out of the Text box but fits in it, it is moved inside; descenders and taller
/// glyphs may still stick out.
void placeVertical(const HiResCmd &c, HiResFont *font, double sx, double sy, int &size4, int &baseline, int &outline64)
{
	(void)sx;
	// geometry (c.x/c.y) is in base px; cell metrics are in widget px (magnified widgets: 'scale' base px each)
	const double ms = sy * std::max<int>(1, c.scale);
	const SlotSize &ss = slotSize(font, c.slot, c.lineH > 0 ? c.lineH : c.cellH, ms);
	const SlotSize &k = ss;
	size4 = k.size4;
	outline64 = (int)std::lround(ss.r * 64.0);
	const double bodyH = k.asc + k.desc;
	const double cellTop = c.y * sy;
	double centre;
	if (c.inkTopL >= 0 && c.inkBotL >= c.inkTopL)
		centre = cellTop + (c.inkTopL + c.inkBotL + 1) * ms / 2.0;
	else if (c.inkTopK >= 0 && c.inkBotK >= c.inkTopK)
		centre = cellTop + (c.inkTopK + c.inkBotK + 1) * ms / 2.0;
	else
		centre = cellTop + c.cellH * ms / 2.0;
	double top = centre - bodyH / 2.0;
	// the Text box itself (not the clip rect, which may be cut further by parents or the screen)
	const double boxT = (c.y + c.boxDy) * sy, boxB = (c.y + c.boxDy + c.boxH) * sy;
	if (c.boxH > 0 && bodyH + 2.0 * ss.r <= boxB - boxT)
	{
		if (top - ss.r < boxT)
			top = boxT + ss.r;
		else if (top + bodyH + ss.r > boxB)
			top = boxB - ss.r - bodyH;
	}
	baseline = (int)std::lround(top + k.asc);
}

/// Composes the current frame into S.rgba (dstW x dstH). Returns the dirty rect of this frame;
/// 'clearRect' receives the rect cleared from the previous frame.
SDL_Rect compose(int dstW, int dstH, SDL_Rect &clearRect)
{
	if (S.bufW != dstW || S.bufH != dstH)
	{
		S.bufW = dstW;
		S.bufH = dstH;
		S.rgba.assign((size_t)dstW * dstH, 0);
		S.prevDirty = {0, 0, (Uint16)dstW, (Uint16)dstH};
		S.sizeCache.clear();
	slotSizeCache.clear();
	}
	clearRect = S.prevDirty;
	for (int y = S.prevDirty.y; y < S.prevDirty.y + S.prevDirty.h; ++y)
		std::fill_n(&S.rgba[y * S.bufW + S.prevDirty.x], S.prevDirty.w, 0u);

	const double sx = dstW / (double)S.baseW, sy = dstH / (double)S.baseH;
	for (auto &f : S.fonts)
		if (f.second) f.second->trimCache(16384);
	SDL_Rect dirty = {0, 0, 0, 0};
	std::vector<Placed> placed;
	// Commands delivered by the same blit (one text widget / button) are drawn in two
	// passes - all outlines, then all bodies - like the bitmap font whose outline never
	// covers the neighbouring glyph's body. Groups keep their blit (z) order.
	for (size_t groupStart = 0; groupStart < S.frame.size(); )
	{
		size_t groupEnd = groupStart;
		while (groupEnd < S.frame.size() && S.frame[groupEnd].stamp == S.frame[groupStart].stamp)
			++groupEnd;
		placed.assign(groupEnd - groupStart, Placed());
		// pictures of this blit first (under its glyphs)
		for (size_t i = groupStart; i < groupEnd; ++i)
			if (S.frame[i].kind == HiResCmdKind::Image)
				drawImage(S.frame[i], dstW, dstH, sx, sy, dirty);
		// layout, run by run (natural width first)
		struct RunInfo { size_t start, end; long long width64; int hs; };
		std::vector<RunInfo> runs;
		auto layoutRun = [&](size_t runStart, size_t runEnd, int hs) -> long long
		{
			// vertical metrics + glyphs; then horizontal placement. The pixel size depends only on the
			// font (slot) and the output scale, never on the width of the text.
			long long pen64 = 0;
			const HiResGlyph *prev = nullptr;
			int prevPx = 0;
			for (size_t i = runStart; i < runEnd; ++i)
			{
				const HiResCmd &c = S.frame[i];
				HiResFont *font = slotFont(c.slot);
				Placed &p = placed[i - groupStart];
				p.g = nullptr;
				if (!font)
					continue;
				int px, baseline; // px: font size in 1/4 px
				int outline64;
				placeVertical(c, font, sx, sy, px, baseline, outline64);
				const int bold64 = bold64ForSize(px);
				const bool space = (c.flags & HiResCmd::GLYPH_SPACE) != 0;
				const HiResGlyph &g = font->getGlyph(c.code, px, space ? 0 : outline64, space ? 0 : bold64, hs);
				if (!g.ok)
					continue;
				p.g = &g;
				p.baseline = baseline;
				if (S.layout == 0 || c.run == 0)
				{
					// cell mode: centre each glyph in the advance box of the bitmap glyph
					p.pen = (int)std::lround(c.x * sx + (c.w * sx - g.advance) / 2.0);
				}
				else
				{
					if (prev && prevPx == px)
						pen64 += (long long)font->getKerning64(*prev, g, px) * hs / 256;
					p.pen = (int)(pen64 >> 6); // relative for now
					if (c.flags & HiResCmd::GLYPH_NBSP)
					{
						// thousands separator: half the advance of a digit (TTF spaces are ~0.25-0.3 em,
						// digits ~0.55-0.6 em; the bitmap fonts use width/4)
						const HiResGlyph &digit = font->getGlyph('0', px, 0, bold64, hs);
						pen64 += digit.ok ? (digit.adv64 + bold64) / 2 : g.adv64;
					}
					else
						pen64 += g.adv64 + (space ? 0 : bold64);
					prev = &g;
					prevPx = px;
				}
			}
			return pen64;
		};
		for (size_t runStart = groupStart; runStart < groupEnd; )
		{
			size_t runEnd = runStart + 1;
			const HiResCmd &first = S.frame[runStart];
			if (first.kind == HiResCmdKind::Glyph && first.run != 0)
			{
				while (runEnd < groupEnd && S.frame[runEnd].run == first.run && S.frame[runEnd].kind == HiResCmdKind::Glyph)
					++runEnd;
			}
			if (first.kind != HiResCmdKind::Glyph)
			{
				runStart = runEnd; // Image: drawn above
				continue;
			}
			const long long w64 = layoutRun(runStart, runEnd, 256);
			if (S.layout != 0 && first.run != 0)
				runs.push_back(RunInfo{ runStart, runEnd, w64, 256 });
			runStart = runEnd;
		}
		// squeeze: a line (all pen-mode runs of one Text line) wider than its Text box is narrowed
		// horizontally only, by box / width, down to 80%; beyond that it overflows from its anchor.
		// The line width is estimated from the bitmap layout: the gaps between/around the runs keep
		// their bitmap width, the runs take their TTF width.
		for (size_t r0 = 0; r0 < runs.size(); )
		{
			const Uint32 lineId = S.frame[runs[r0].start].lineId;
			size_t r1 = r0 + 1;
			if (lineId != 0)
				while (r1 < runs.size() && S.frame[runs[r1].start].lineId == lineId)
					++r1;
			const HiResCmd &c0 = S.frame[runs[r0].start];
			const double boxW = c0.boxW * sx;
			if (lineId != 0 && boxW > 0)
			{
				double ttf = 0, spans = 0, lo = 1e9, hi = -1e9;
				for (size_t r = r0; r < r1; ++r)
				{
					const HiResCmd &f = S.frame[runs[r].start], &l = S.frame[runs[r].end - 1];
					ttf += runs[r].width64 / 64.0;
					spans += (l.x + l.w - f.x) * sx;
					lo = std::min(lo, f.x * sx);
					hi = std::max(hi, (l.x + l.w) * sx);
				}
				const double gaps = std::max(0.0, (hi - lo) - spans);
				if (ttf > 0 && gaps + ttf > boxW + 0.5)
				{
					const double f = std::max(0.8, std::min(1.0, (boxW - gaps) / ttf));
					const int hs = (int)std::lround(256 * f);
					if (hs < 256)
						for (size_t r = r0; r < r1; ++r)
						{
							runs[r].width64 = layoutRun(runs[r].start, runs[r].end, hs);
							runs[r].hs = hs;
						}
				}
			}
			r0 = r1;
		}
		// pen mode: place each run in the span the bitmap layout reserved for it, aligned with its
		// (squeezed) width; a run wider than its span overflows from its alignment anchor
		for (const RunInfo &ri : runs)
		{
			const HiResCmd &first = S.frame[ri.start];
			const HiResCmd &last = S.frame[ri.end - 1];
			const double spanL = first.x * sx, spanR = (last.x + last.w) * sx;
			const double width = ri.width64 / 64.0;
			double start = spanL;
			if (first.flags & HiResCmd::GLYPH_ALIGN_CENTER)
				start = (spanL + spanR - width) / 2.0;
			else if (first.flags & HiResCmd::GLYPH_ALIGN_RIGHT)
				start = spanR - width;
			const int ist = (int)std::lround(start);
			for (size_t i = ri.start; i < ri.end; ++i)
				placed[i - groupStart].pen += ist;
		}
		for (int pass = 0; pass < 2; ++pass)
		{
			for (size_t i = groupStart; i < groupEnd; ++i)
			{
				const HiResCmd &c = S.frame[i];
				const Placed &p = placed[i - groupStart];
				if (!p.g || (c.flags & HiResCmd::GLYPH_SPACE) || (pass == 0 && p.g->line.empty()))
					continue;
				const HiResGlyph &g = *p.g;
				const int cx0 = std::max(0, (int)std::floor(c.clipX * sx)), cy0 = std::max(0, (int)std::floor(c.clipY * sy));
				const int cx1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
				const int cy1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
				if (pass == 0)
					drawCoverage(g.line, g.lineW, g.lineH, p.pen + g.lineL, p.baseline - g.lineT, c.line, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
				else
					drawCoverage(g.fill, g.fillW, g.fillH, p.pen + g.fillL, p.baseline - g.fillT, c.fill, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
			}
		}
		groupStart = groupEnd;
	}
	S.prevDirty = dirty;
	return dirty;
}

}

void HiResLayer::init()
{
	shutdown();
	reconfigure();
	S.modeChanged = false;
}

void HiResLayer::reconfigure()
{
	const bool wasRecording = _recording;
	S.sizePct = std::max(10, Options::oxceHiResFontSize);
	S.outlinePct = std::min(400, std::max(0, Options::oxceHiResTextOutlineScale));
	S.boldPct = std::max(0, Options::oxceHiResTextBold);
	S.layout = Options::oxceHiResTextLayout;
	S.settings.hinting = Options::oxceHiResTextHinting;
	S.settings.antialias = Options::oxceHiResTextAntialias;
	S.slots.clear();
	S.sizeCache.clear();
	slotSizeCache.clear();
	S.defaultFont = nullptr;
	_textConfigured = false;
	if (Options::oxceHiResOverlay && Options::oxceHiResText)
	{
		std::vector<std::string> specs = splitList(Options::oxceHiResFont);
		std::vector<std::string> fallback = splitList(Options::oxceHiResFontFallback);
		specs.insert(specs.end(), fallback.begin(), fallback.end());
		// built-in defaults: primary Segoe UI, fallback Malgun Gothic (Hangul), then other Windows
		// fonts, then common Linux/macOS fonts
		std::vector<std::string> builtin = { "segoeui.ttf", "malgun.ttf", "gulim.ttc", "batang.ttc",
			"NanumGothic.ttf", "NotoSansCJK-Regular.ttc", "DejaVuSans.ttf", "AppleSDGothicNeo.ttc" };
		for (const auto &b : builtin)
			if (std::find(specs.begin(), specs.end(), b) == specs.end())
				specs.push_back(b);
		S.defaultSpecs = specs;
		S.defaultFont = getFontChain(specs);
		if (S.defaultFont)
			_textConfigured = true;
		else
			Log(LOG_ERROR) << "HiResLayer: no usable TTF font found, hi-res text disabled";
	}
	_recording = (_textConfigured || uiImagesWanted()) && S.outputSupported;
	if (_recording && S.stamps.size() != (size_t)S.baseW * S.baseH)
		S.stamps.assign((size_t)S.baseW * S.baseH, 0);
	if (wasRecording != _recording)
	{
		S.lists.clear();
		S.frame.clear();
		S.modeChanged = true;
	}
	Log(LOG_INFO) << "HiResLayer: overlay=" << Options::oxceHiResOverlay << " text=" << _textConfigured << " recording=" << _recording;
}

bool HiResLayer::consumeModeChanged()
{
	bool r = S.modeChanged;
	S.modeChanged = false;
	return r;
}

bool HiResLayer::wantsTrueColor()
{
	return (Options::oxceHiResOverlay && Options::oxceHiResText) || uiImagesWanted();
}

void HiResLayer::shutdown()
{
	_recording = false;
	_textConfigured = false;
	S.slots.clear();
	S.defaultFont = nullptr;
	S.fonts.clear();
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
	_recording = (_textConfigured || uiImagesWanted()) && S.outputSupported;
	if (_recording)
		S.stamps.assign((size_t)S.baseW * S.baseH, 0);
	else
		std::vector<Uint32>().swap(S.stamps);
	if (was != _recording)
	{
		Log(LOG_INFO) << "HiResLayer: recording " << (_recording ? "enabled" : "disabled (unsupported output path)");
		S.lists.clear();
		S.modeChanged = true;
	}
	S.frame.clear();
	S.sizeCache.clear();
	slotSizeCache.clear();
}

void HiResLayer::onContextLost()
{
#ifndef __NO_OPENGL
	S.tex = 0; // the old context and its textures are gone
	S.texW = S.texH = 0;
#endif
	// drop the composition buffer too: it is re-allocated (zeroed) on the next frame,
	// otherwise the new texture would be created from stale contents
	std::vector<Uint32>().swap(S.rgba);
	S.bufW = S.bufH = 0;
	S.prevDirty = {0, 0, 0, 0};
}

int HiResLayer::registerFontId(const std::string &id)
{
	auto it = std::find(S.slotIds.begin(), S.slotIds.end(), id);
	if (it != S.slotIds.end())
		return (int)(it - S.slotIds.begin());
	if (S.slotIds.size() >= 255)
		return -1;
	S.slotIds.push_back(id);
	return (int)S.slotIds.size() - 1;
}

const std::vector<std::string> &HiResLayer::fontIds()
{
	return S.slotIds;
}

void HiResLayer::getFontSetting(const std::string &id, std::string &spec, int &sizePct)
{
	auto m = parseFontMap(Options::oxceHiResFontMap);
	auto it = m.find(id);
	spec = it != m.end() ? it->second.first : "";
	sizePct = it != m.end() ? it->second.second : 100;
}

void HiResLayer::setFontSetting(const std::string &id, const std::string &spec, int sizePct)
{
	auto m = parseFontMap(Options::oxceHiResFontMap);
	m[id] = std::make_pair(spec, sizePct);
	Options::oxceHiResFontMap = formatFontMap(m);
}

std::vector<std::string> HiResLayer::slotFaceNames(int slot)
{
	HiResFont *f = _textConfigured ? slotFont(slot) : nullptr;
	return f ? f->getFaceNames() : std::vector<std::string>();
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
		if (dit != S.lists.end() && !dit->second.empty())
		{
			auto &dl = dit->second;
			// Commands of dst that are covered by opaque pixels of this blit are gone, exactly like
			// the pixels they stand for (e.g. a window background blitted over older text).
			// The same child blitted again at the same place replaces its previous copies (no
			// duplicates, updated contents). A child blitted at another place keeps the older copies:
			// e.g. Globe::drawDetail draws all country/city labels with one reused Text object.
			const bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
			const bool pal8 = src->format->BitsPerPixel == 8;
			const Uint32 key = src->format->colorkey;
			bool locked = false;
			auto covered = [&](const HiResCmd &c) -> bool
			{
				// visible part of the command (layout box inside its clip rect), dst coordinates
				int cx = c.clipX, cy = c.clipY, cw = c.clipW, ch = c.clipH;
				if (!intersect(cx, cy, cw, ch, c.x, c.y, c.w, c.h))
					return false;
				// it must lie completely inside the blitted rectangle
				int bx = cx, by = cy, bw = cw, bh = ch;
				if (!intersect(bx, by, bw, bh, x, y, src->w, src->h) || bw != cw || bh != ch)
					return false;
				if (!keyed)
					return true; // opaque blit
				if (!pal8)
					return false;
				if (!locked) { SDL_LockSurface(src); locked = true; }
				for (int yy = cy; yy < cy + ch; ++yy)
				{
					const Uint8 *sp = (const Uint8*)src->pixels + (yy - y) * src->pitch + (cx - x);
					for (int xx = 0; xx < cw; ++xx)
						if (sp[xx] == key)
							return false;
				}
				return true;
			};
			dl.erase(std::remove_if(dl.begin(), dl.end(), [&](const HiResCmd &c)
			{
				if (c.origin == src)
					return c.originX == x && c.originY == y;
				return covered(c);
			}), dl.end());
			if (locked)
				SDL_UnlockSurface(src);
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
			c.originX = (Sint16)x;
			c.originY = (Sint16)y;
		}
		dl->push_back(c);
	}
}

void HiResLayer::transferScaled(SDL_Surface *src, int x, int y, int w, int h, SDL_Surface *dst, int scale)
{
	if (!_recording || !src || !dst)
		return;
	clearSurface(dst);
	auto it = S.lists.find(src);
	if (it == S.lists.end() || it->second.empty())
		return;
	const int k = std::max(1, scale);
	std::vector<HiResCmd> out;
	out.reserve(it->second.size());
	for (const HiResCmd &c0 : it->second)
	{
		int cx = c0.clipX - x, cy = c0.clipY - y, cw = c0.clipW, ch = c0.clipH;
		if (!intersect(cx, cy, cw, ch, 0, 0, w, h))
			continue;
		HiResCmd c = c0;
		c.x = (Sint16)((c0.x - x) * k);
		c.y = (Sint16)((c0.y - y) * k);
		c.w = (Sint16)(c0.w * k);
		c.boxW = (Sint16)(c0.boxW * k);
		c.boxDy = (Sint16)(c0.boxDy * k);
		c.boxH = (Sint16)(c0.boxH * k);
		c.h = (Sint16)(c0.h * k);
		c.clipX = (Sint16)(cx * k);
		c.clipY = (Sint16)(cy * k);
		c.clipW = (Sint16)(cw * k);
		c.clipH = (Sint16)(ch * k);
		c.scale = (Uint8)std::min(255, std::max<int>(1, c0.scale) * k);
		c.origin = src;
		c.originX = (Sint16)-x;
		c.originY = (Sint16)-y;
		out.push_back(c);
	}
	auto &dl = S.lists[dst];
	dl.insert(dl.end(), out.begin(), out.end());
}

bool HiResLayer::canRenderGlyph(UCode c, int slot)
{
	if (!_textConfigured)
		return false;
	HiResFont *f = slotFont(slot);
	return f && f->hasGlyph(c);
}

int HiResLayer::baseAdvance(UCode c, int lineH, int slot)
{
	if (!canRenderGlyph(c, slot))
		return -1;
	HiResFont *f = slotFont(slot);
	int px = slotSize(f, slot, lineH, 1.0).size4;
	int adv = f->getAdvance(c, px);
	return adv < 0 ? -1 : std::max(1, adv);
}

Uint32 HiResLayer::newImageId()
{
	if (++imageCounter == 0)
		++imageCounter;
	return imageCounter;
}

void HiResLayer::setImage(Uint32 id, const Uint32 *argb, int w, int h)
{
	images[id] = ImageRef{ argb, w, h };
}

void HiResLayer::dropImage(Uint32 id)
{
	images.erase(id);
}

bool HiResLayer::isScreen(const SDL_Surface *surface)
{
	return surface && surface == S.screen;
}

Uint32 HiResLayer::newRun()
{
	if (++S.runCounter == 0)
		++S.runCounter;
	return S.runCounter;
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

void HiResLayer::renderSoftware(SDL_Surface *out, int top, int bottom, int left, int right)
{
	if (!_recording || !out || out->format->BytesPerPixel != 4 || S.baseW <= 0 || S.baseH <= 0)
		return;
	const int dstW = out->w - left - right, dstH = out->h - top - bottom;
	if (dstW <= 0 || dstH <= 0)
		return;
	if (S.frame.empty() && S.prevDirty.w == 0)
		return;
	SDL_Rect cleared;
	SDL_Rect dirty = compose(dstW, dstH, cleared);
	if (dirty.w <= 0 || dirty.h <= 0)
		return;
	if (SDL_MUSTLOCK(out))
		SDL_LockSurface(out);
	const SDL_PixelFormat *fmt = out->format;
	for (int y = dirty.y; y < dirty.y + dirty.h; ++y)
	{
		Uint32 *dst = (Uint32*)((Uint8*)out->pixels + (y + top) * out->pitch) + left;
		const Uint32 *src = &S.rgba[y * S.bufW];
		for (int x = dirty.x; x < dirty.x + dirty.w; ++x)
		{
			Uint32 s = src[x];
			unsigned a = s >> 24;
			if (!a)
				continue;
			Uint32 d = dst[x];
			unsigned inv = 255 - a;
			unsigned dr = ((d & fmt->Rmask) >> fmt->Rshift), dg = ((d & fmt->Gmask) >> fmt->Gshift), db = ((d & fmt->Bmask) >> fmt->Bshift);
			unsigned r = ((s >> 16) & 0xFF) + dr * inv / 255;
			unsigned g = ((s >> 8) & 0xFF) + dg * inv / 255;
			unsigned b = (s & 0xFF) + db * inv / 255;
			dst[x] = (d & ~(fmt->Rmask | fmt->Gmask | fmt->Bmask)) | (r << fmt->Rshift) | (g << fmt->Gshift) | (b << fmt->Bshift);
		}
	}
	if (SDL_MUSTLOCK(out))
		SDL_UnlockSurface(out);
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
	SDL_Rect cleared;
	bool resized = (S.bufW != dstW || S.bufH != dstH);
	SDL_Rect dirty = compose(dstW, dstH, cleared);
	SDL_Rect upload = cleared;
	unionRect(upload, dirty.x, dirty.y, dirty.w, dirty.h);
	(void)resized;

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
		// OpenGL::refresh() already unbound the shader program (glUseProgram(0)), so the overlay is
		// drawn with the fixed pipeline on top of whatever the screen shader produced.
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
