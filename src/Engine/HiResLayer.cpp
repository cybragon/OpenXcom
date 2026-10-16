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
#include "HiResXbrzGL.h"
#include "HiResFont.h"
#include "HiResAssets.h"
#include "HiResResample.h"
#include "OpenGL.h"
#include "Options.h"
#include "Logger.h"
#include "UiImageScaler.h"
#include "Scalers/hqx.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <map>
#include <cstdlib>
#include <chrono>

namespace
{
/// Optional profiling (env OXCE_HIRES_PROF=1): ms per frame of record / deliver / compose / upload.
struct Prof
{
	bool on = std::getenv("OXCE_HIRES_PROF") != nullptr;
	double rec = 0, del = 0, comp = 0, up = 0, gl = 0, smask = 0;
	int frames = 0;
	size_t cmds = 0;
};
Prof prof;
inline double profNow()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct ProfScope
{
	double &acc; double t0;
	explicit ProfScope(double &a) : acc(a), t0(prof.on ? profNow() : 0) { }
	~ProfScope() { if (prof.on) acc += profNow() - t0; }
};
}

namespace OpenXcom
{

bool HiResLayer::_recording = false;
bool HiResLayer::_textConfigured = false;

namespace
{

/// Extra emboldening (fraction of the em) of font chains without a bold face that must look bold
/// (battle UI digits); keyed by chain, so the size measurement and the rendering agree.
std::unordered_map<const HiResFont*, double> extraBold;

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

enum : Uint8 { MASK_NONE = 0, MASK_PROXY = 1, MASK_EDGE = 2, MASK_BACK = 3 };

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
	/// snapshot masks of the hi-res asset commands of this frame, per base pixel of the clip rect:
	/// 0 = not drawn, MASK_PROXY = the proxy pixel reached the screen unchanged, MASK_EDGE = transparent
	/// proxy pixel next to the silhouette, unchanged since the record (the picture's alpha draws the
	/// edge over what is there), MASK_BACK = proxy pixel on the silhouette whose background is known
	/// (backs: the picture is composited over that colour, so its alpha defines the outline)
	std::vector<std::vector<Uint8>> masks;
	std::vector<std::vector<Uint32>> backs; ///< per mask: background colour of MASK_BACK pixels (or empty)
	/// pixels under the proxy at record time, per proxy pixel of the drawn source rect (underId)
	std::unordered_map<Uint32, std::vector<Uint8>> unders;
	std::unordered_map<size_t, Uint32> underByHash;
	Uint32 underCounter = 0;
	std::unordered_map<Uint32, bool> assetHasEdge; ///< proxy has transparent pixels next to opaque ones
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

/// layered battlescape: 8bpp sprites (base px straight ARGB) and their output-size renders
struct Sprite8 { int w = 0, h = 0; std::vector<Uint32> px; };
std::unordered_map<Uint32, Sprite8> sprites;
std::unordered_map<size_t, Uint32> spriteByHash;
Uint32 spriteCounter = 0;
struct SpriteRender { std::vector<Uint32> px; Uint64 used = 0; };
std::unordered_map<size_t, SpriteRender> spriteRenders;
size_t spriteRenderBytes = 0;
Uint64 spriteUse = 0;
SDL_Surface *layerSurf = nullptr;     ///< the map surface of the layered frame
std::vector<Uint16> layerVal;         ///< per map pixel: 1 + value written by a recorded draw, 0 = none
Uint8 layerBg = 0;

Uint32 storeSprite(std::vector<Uint32> &&px, int w, int h)
{
	size_t hsh = 1469598103934665603ull;
	auto mix = [&](size_t v) { hsh = (hsh ^ v) * 1099511628211ull; };
	mix((size_t)w); mix((size_t)h);
	for (Uint32 v : px) mix(v);
	auto it = spriteByHash.find(hsh);
	if (it != spriteByHash.end())
	{
		auto si = sprites.find(it->second);
		if (si != sprites.end() && si->second.w == w && si->second.h == h && si->second.px == px)
			return it->second;
	}
	if (++spriteCounter == 0)
		++spriteCounter;
	Sprite8 &sp = sprites[spriteCounter];
	sp.w = w; sp.h = h; sp.px = std::move(px);
	spriteByHash[hsh] = spriteCounter;
	return spriteCounter;
}

const Uint32 *spriteRendered(Uint32 key, int outW, int outH, bool nearest)
{
	auto si = sprites.find(key);
	if (si == sprites.end() || outW <= 0 || outH <= 0)
		return nullptr;
	const int mode = nearest ? HiResResample::NEAREST : HiResResample::mode();
	size_t rk = 1469598103934665603ull;
	auto mix = [&](size_t v) { rk = (rk ^ v) * 1099511628211ull; };
	mix(key); mix((size_t)outW); mix((size_t)outH); mix((size_t)mode);
	auto ri = spriteRenders.find(rk);
	if (ri != spriteRenders.end())
	{
		ri->second.used = ++spriteUse;
		return ri->second.px.data();
	}
	if (spriteRenderBytes > ((size_t)192 << 20))
	{
		// bounded: drop the least recently used half
		std::vector<std::pair<Uint64, size_t>> order;
		for (auto &r : spriteRenders) order.emplace_back(r.second.used, r.first);
		std::sort(order.begin(), order.end());
		for (size_t i = 0; i < order.size() / 2; ++i)
		{
			auto e = spriteRenders.find(order[i].second);
			spriteRenderBytes -= e->second.px.size() * 4;
			spriteRenders.erase(e);
		}
	}
	SpriteRender &r = spriteRenders[rk];
	HiResResample::resample(si->second.px.data(), si->second.w, si->second.h, 0, 0, si->second.w, si->second.h, outW, outH, mode, r.px);
	r.used = ++spriteUse;
	spriteRenderBytes += r.px.size() * 4;
	return r.px.data();
}

/// The layer is also needed without hi-res text: in "x1, UI optimized" (software path) the UI
/// panels magnified with HQX/xBRZ are true-colour pictures drawn by this layer (image commands).
/// Text stays bitmap then (canRenderGlyph/textConfigured are false).
bool uiImagesWanted()
{
	const UiImageScaler::Filter f = UiImageScaler::uiFilter();
	return f == UiImageScaler::Filter::HQX || f == UiImageScaler::Filter::XBRZ;
}

/// Hi-res assets (option oxceHiResAssets) are drawn by this layer too, with or without hi-res text.
bool assetsWanted()
{
	return HiResAssets::enabled(); // option value of the last mod load
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
		if (S.slotIds[slot].compare(0, 13, "BATTLE_DIGITS") == 0 && S.defaultFont)
		{
			// battle UI digits: a bold system font (never bundled); no font map entry -> oxceHiResDigitFont
			// or the built-in bold list, then the normal chain
			if (it == m.end() || it->second.first.empty())
			{
				std::vector<std::string> specs = splitList(Options::oxceHiResDigitFont);
				if (specs.empty())
					specs = { "arialbd.ttf", "segoeuib.ttf", "malgunbd.ttf", "verdanab.ttf", "DejaVuSans-Bold.ttf",
						"LiberationSans-Bold.ttf", "NanumGothicBold.ttf" };
				specs.insert(specs.end(), S.defaultSpecs.begin(), S.defaultSpecs.end());
				if (HiResFont *f = getFontChain(specs))
					st.font = f;
			}
			// the face drawing the digits has no bold style: FreeType emboldening instead
			if (st.font && !st.font->isBold(st.font->faceFor('0')) && !extraBold.count(st.font))
				extraBold[st.font] = 0.07;
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
inline int bold64ForSize(int size4, const HiResFont *font = nullptr)
{
	double r = boldRatio();
	if (font && !extraBold.empty())
	{
		auto it = extraBold.find(font);
		if (it != extraBold.end())
			r += it->second;
	}
	return (int)std::lround(size4 / 4.0 * r * 64.0);
}

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
		{ "NUMBER_TEXT", { 5.0, 0.0 } },    // NumberText 3x5 digits (TU, health, ammo...): digit cell height
		{ "NUMBER_TEXT_BORDERED", { 5.0, 1.0 } },
		{ "BATTLE_DIGITS", { 5.0, 0.0 } },  // battlescape UI NumberTexts (own bold font, oxceHiResDigitFont)
		{ "BATTLE_DIGITS_BORDERED", { 5.0, 1.0 } },
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
	const HiResGlyph &g = font->getGlyph(ref, size4, 0, bold64ForSize(size4, font));
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
	// small HUD types (FONT_SMALL, BATTLE_DIGITS): round the target up so the body fills the row like the
	// original bitmap font (rounding down lost up to one output px); larger types round down as before
	const std::string &sid = (slot >= 0 && slot < (int)S.slotIds.size()) ? S.slotIds[slot] : std::string();
	const bool ceilSlot = sid == "FONT_SMALL" || sid.compare(0, 13, "BATTLE_DIGITS") == 0;
	const double limit = std::max(1.0, ceilSlot ? std::ceil(ss.target - 1e-6) : std::floor(ss.target + 1e-6));
	// digit-only fonts (BATTLE_DIGITS): the digit itself fills the 3x5 digit rows like the original
	// (the Hangul reference left Latin digits about one base px short)
	const bool digitSlot = sid.compare(0, 13, "BATTLE_DIGITS") == 0;
	const UCode ref = digitSlot && font->hasGlyph('0') ? (UCode)'0' : font->hasGlyph(SIZE_REF) ? SIZE_REF : SIZE_REF_NO_HANGUL;
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

/// Blends cmd.tint over the output pixels of an image command already drawn in the rect (x0..x1, y0..y1).
void applyTint(const HiResCmd &c, const std::vector<Uint8> *mask, int x0, int y0, int x1, int y1, double sx, double sy)
{
	const unsigned ta = c.tint >> 24;
	if (!ta)
		return;
	const SDL_Color col = { (Uint8)((c.tint >> 16) & 0xFF), (Uint8)((c.tint >> 8) & 0xFF), (Uint8)(c.tint & 0xFF), (Uint8)255 };
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	for (int y = y0; y < y1; ++y)
	{
		const int by = std::min(S.baseH - 1, (int)(y * isy));
		const int my = by - c.clipY;
		if (my < 0 || my >= c.clipH)
			continue;
		const Uint32 *stampRow = &S.stamps[by * S.baseW];
		Uint32 *dst = &S.rgba[y * S.bufW];
		for (int x = x0; x < x1; ++x)
		{
			const int bx = std::min(S.baseW - 1, (int)(x * isx));
			const int mx = bx - c.clipX;
			if (mx < 0 || mx >= c.clipW || stampRow[bx] > c.stamp)
				continue;
			if (mask && !(*mask)[(size_t)my * c.clipW + mx])
				continue;
			blendPixel(dst[x], col, ta);
		}
	}
}

/// Battle draw: the 8bpp pixel the map shows for proxy pixel ev (armor recolour, then shade / colour
/// replace exactly like the default recolorUnitSprite script and Surface::blitRaw).
inline Uint8 battleExpected(const HiResCmd &c, Uint8 ev)
{
	if (!ev)
		return 0;
	for (int k = 0; k < 4; ++k)
	{
		if (c.rcFrom[k] && (ev & 0xF0) == c.rcFrom[k])
		{
			ev = (Uint8)(c.rcTo[k] + (ev & 0x0F));
			break;
		}
	}
	if (c.nvColor)
	{
		const int ns = (ev & 0x0F) + c.shade;
		return (ns & 0xF0) ? 0x0F : (Uint8)((((c.nvColor - 1) << 4) & 0xF0) | ns);
	}
	const Uint8 n = (Uint8)(ev + c.shade);
	return ((n ^ ev) & 0xF0) ? 0x0F : n;
}

/// Hue of the 16-step ramp starting at palette index 'base' at the luminance of the premultiplied pixel p.
inline Uint32 rampTint(Uint32 p, const SDL_Color *pal, int base)
{
	const unsigned a = p >> 24;
	if (!a || !pal)
		return p;
	const double r = ((p >> 16) & 0xFF) * 255.0 / a, g = ((p >> 8) & 0xFF) * 255.0 / a, b = (p & 0xFF) * 255.0 / a;
	const double L = 0.299 * r + 0.587 * g + 0.114 * b;
	int best = base;
	double bd = 1e9;
	for (int i = 0; i < 16 && base + i < 256; ++i)
	{
		const SDL_Color &q = pal[base + i];
		const double d = std::fabs(0.299 * q.r + 0.587 * q.g + 0.114 * q.b - L);
		if (d < bd) { bd = d; best = base + i; }
	}
	const SDL_Color &q = pal[best];
	const double lq = std::max(1.0, 0.299 * q.r + 0.587 * q.g + 0.114 * q.b);
	const double k = L / lq;
	auto ch = [&](double v) { return (Uint32)std::min(255.0, std::max(0.0, v * k * a / 255.0 + 0.5)); };
	return (p & 0xFF000000u) | (ch(q.r) << 16) | (ch(q.g) << 8) | ch(q.b);
}

/// Battle draw at output resolution: mask recolour -> (night vision colour) -> brightness multiply.
inline Uint32 battlePixel(const HiResCmd &c, Uint32 p, Uint8 m, const SDL_Color *pal, int factor)
{
	if (!(p >> 24) || (factor >= 256 && !m && !c.nvColor))
		return p;
	if (m && c.rcFrom[m - 1])
		p = rampTint(p, pal, c.rcTo[m - 1]);
	if (c.nvColor)
		p = rampTint(p, pal, ((c.nvColor - 1) << 4) & 0xF0);
	if (factor >= 256)
		return p;
	const Uint32 r = (((p >> 16) & 0xFF) * factor) >> 8, g = (((p >> 8) & 0xFF) * factor) >> 8, b = ((p & 0xFF) * factor) >> 8;
	return (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

/// Scale-mask interpolation of the layered battlescape (CPU). Every map draw lands at output
/// resolution by nearest neighbour on its own source grid; each output pixel records the scale class
/// of the picture that wrote it last (cls, 0 = exactly 1x or downscaled = final). Afterwards, per
/// class, the pixels are taken back to their source grid (one sample per block), interpolated up once
/// (smooth / xBRZ / hqx, from the screen filter) and written back only where that class is.
struct ScaleClass
{
	double kx, ky, phx, phy; ///< source pixels per base pixel + grid phase
	int bx0 = INT32_MAX, by0 = INT32_MAX, bx1 = -1, by1 = -1; ///< output px bounding box of the class (GL map pass)
};
struct ScaleMaskState
{
	bool inMap = false;               ///< drawing the layered map's commands now
	std::vector<Uint8> cls;           ///< per output pixel
	std::vector<ScaleClass> classes;  ///< class id - 1
} SM;

/// Class of a source grid (1 = the 8-bit grid). 0 = table full (caller falls back).
Uint8 scaleClass(double kx, double ky, double phx, double phy)
{
	for (size_t i = 0; i < SM.classes.size(); ++i)
	{
		const ScaleClass &c = SM.classes[i];
		if (std::fabs(c.kx - kx) < 1e-6 && std::fabs(c.ky - ky) < 1e-6 && std::fabs(c.phx - phx) < 1e-6 && std::fabs(c.phy - phy) < 1e-6)
			return (Uint8)(i + 1);
	}
	if (SM.classes.size() >= 250)
		return 0;
	SM.classes.push_back(ScaleClass{ kx, ky, phx, phy });
	return (Uint8)SM.classes.size();
}

/// buffers of the pass: read the nearest composite, write the interpolated result (map cache)
const Uint32 *smSrc = nullptr;
Uint32 *smDst = nullptr;
int smMargin = 0; ///< output px read around the region (filter reach)

void scaleMaskBegin(int dstW, int dstH)
{
	SM.inMap = true;
	SM.cls.assign((size_t)dstW * dstH, 0);
	SM.classes.clear();
	scaleClass(1.0, 1.0, 0.0, 0.0); // class 1: 8-bit sprites, map background
}

inline void setCls(int x, int y, Uint8 k)
{
	if (SM.inMap)
		SM.cls[(size_t)y * S.bufW + x] = k;
}

inline double fracOf(double v) { return v - std::floor(v); }

/// The pass: region = the map's drawn rect (output px).
void scaleMaskPass(const SDL_Rect &region, double sx, double sy)
{
	SM.inMap = false;
	const int mode = HiResResample::mode();
	const bool debug = Options::oxceHiResDebugScaleMask;
	if ((mode == HiResResample::NEAREST && !debug) || region.w <= 0 || region.h <= 0)
		return;
	ProfScope ps(prof.smask);
	const int nc = (int)SM.classes.size();
	std::vector<int> bx0(nc + 1, INT32_MAX), by0(nc + 1, INT32_MAX), bx1(nc + 1, -1), by1(nc + 1, -1);
	for (int y = region.y; y < region.y + region.h; ++y)
	{
		const Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
		for (int x = region.x; x < region.x + region.w; ++x)
		{
			const int k = cr[x];
			if (!k) continue;
			if (x < bx0[k]) bx0[k] = x;
			if (x > bx1[k]) bx1[k] = x;
			if (y < by0[k]) by0[k] = y;
			if (y > by1[k]) by1[k] = y;
		}
	}
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	static bool hqxReady = false;
	std::vector<Uint32> small, big, out;
	for (int k = 1; k <= nc; ++k)
	{
		if (bx1[k] < 0)
			continue;
		const ScaleClass &sc = SM.classes[k - 1];
		const int X0 = bx0[k], X1 = bx1[k] + 1, Y0 = by0[k], Y1 = by1[k] + 1;
		const int outW = X1 - X0, outH = Y1 - Y0;
		// source grid cell of an output pixel (same rule as the nearest draw: left/top edge of the pixel)
		auto cellX = [&](int x) { return (int)std::floor(x * isx * sc.kx - sc.phx + 1e-9); };
		auto cellY = [&](int y) { return (int)std::floor(y * isy * sc.ky - sc.phy + 1e-9); };
		// read window: the region plus the filter reach (neighbours outside the region come from the cache)
		const int XA = std::max(0, X0 - smMargin), XB = std::min(S.bufW, X1 + smMargin);
		const int YA = std::max(0, Y0 - smMargin), YB = std::min(S.bufH, Y1 + smMargin);
		const int gx0 = cellX(XA), gy0 = cellY(YA), gx1 = cellX(XB - 1) + 1, gy1 = cellY(YB - 1) + 1;
		const int W = gx1 - gx0, H = gy1 - gy0;
		if (W <= 0 || H <= 0)
			continue;
		if (debug)
		{
			static const Uint32 tint[6] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF };
			const Uint32 t = tint[(k - 1) % 6];
			for (int y = Y0; y < Y1; ++y)
			{
				const Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
				Uint32 *dr = &smDst[(size_t)y * S.bufW];
				const Uint32 *sr0 = &smSrc[(size_t)y * S.bufW];
				for (int x = X0; x < X1; ++x)
					if (cr[x] == k)
					{
						const Uint32 p = sr0[x];
						auto mix = [&](int sh) { return (Uint32)(((((p >> sh) & 0xFF) * 5) + (((t >> sh) & 0xFF) * 3)) / 8) << sh; };
						dr[x] = (p & 0xFF000000u) | mix(16) | mix(8) | mix(0);
					}
			}
			continue;
		}
		// shrink: one sample per source block (any pixel of the block still showing this class)
		small.assign((size_t)W * H, 0);
		std::vector<int> colCell(XB - XA);
		for (int x = XA; x < XB; ++x) colCell[x - XA] = cellX(x) - gx0;
		for (int y = YA; y < YB; ++y)
		{
			const int gy = cellY(y) - gy0;
			const Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
			const Uint32 *dr = &smSrc[(size_t)y * S.bufW];
			Uint32 *sr = &small[(size_t)gy * W];
			for (int x = XA; x < XB; ++x)
				if (cr[x] == k && (dr[x] >> 24))
					sr[colCell[x - XA]] = dr[x] | 0xFF000000u;
		}
		const double rx = X0 * isx * sc.kx - sc.phx - gx0, ry = Y0 * isy * sc.ky - sc.phy - gy0;
		const double rw = outW * isx * sc.kx, rh = outH * isy * sc.ky;
		const double scale = std::max(sx / sc.kx, sy / sc.ky);
		if (mode == HiResResample::HQX)
		{
			// hqx has no alpha: blocks without a sample take their nearest sampled neighbour (clamp)
			for (int pass = 0; pass < 2; ++pass)
			{
				for (int y = 0; y < H; ++y)
				{
					Uint32 *r = &small[(size_t)y * W];
					for (int x = 1; x < W; ++x) if (!r[x] && r[x - 1]) r[x] = r[x - 1];
					for (int x = W - 2; x >= 0; --x) if (!r[x] && r[x + 1]) r[x] = r[x + 1];
				}
				for (int x = 0; x < W; ++x)
				{
					for (int y = 1; y < H; ++y) if (!small[(size_t)y * W + x] && small[(size_t)(y - 1) * W + x]) small[(size_t)y * W + x] = small[(size_t)(y - 1) * W + x];
					for (int y = H - 2; y >= 0; --y) if (!small[(size_t)y * W + x] && small[(size_t)(y + 1) * W + x]) small[(size_t)y * W + x] = small[(size_t)(y + 1) * W + x];
				}
			}
			if (!hqxReady) { hqxInit(); hqxReady = true; }
			const int f = std::min(4, std::max(2, (int)std::ceil(scale - 1e-9)));
			big.assign((size_t)W * f * H * f, 0);
			if (f == 2) hq2x_32(small.data(), big.data(), W, H);
			else if (f == 3) hq3x_32(small.data(), big.data(), W, H);
			else hq4x_32(small.data(), big.data(), W, H);
			for (auto &q : big) q |= 0xFF000000u;
			HiResResample::resample(big.data(), W * f, H * f, rx * f, ry * f, rw * f, rh * f, outW, outH, HiResResample::SMOOTH, out);
		}
		else if (mode == HiResResample::SMOOTH || mode == HiResResample::NEAREST)
		{
			// smooth: bilinear in fixed point straight from the source grid, rows interpolated once
			// (same taps as HiResResample: centre-aligned, clamped), samples weighted by alpha, written
			// back only where this class is
			const double stx = rw / outW, sty = rh / outH;
			std::vector<int> c0(outW), c1(outW), cw(outW);
			for (int o = 0; o < outW; ++o)
			{
				const double c = rx + (o + 0.5) * stx - 0.5;
				const int i0 = (int)std::floor(c);
				cw[o] = (int)std::lround((c - i0) * 256);
				c0[o] = std::min(W - 1, std::max(0, i0)); c1[o] = std::min(W - 1, std::max(0, i0 + 1));
			}
			// horizontally interpolated source rows (premultiplied r, g, b, a in 1/256), cached by row
			std::vector<int> rowBuf[2]; int rowId[2] = { -1, -1 };
			auto hrow = [&](int j) -> const int*
			{
				for (int b = 0; b < 2; ++b) if (rowId[b] == j) return rowBuf[b].data();
				const int b = (rowId[0] == -1 || (rowId[1] != -1 && rowId[0] < rowId[1])) ? 0 : 1;
				rowBuf[b].resize((size_t)outW * 4);
				rowId[b] = j;
				const Uint32 *sr = &small[(size_t)j * W];
				int *d = rowBuf[b].data();
				for (int o = 0; o < outW; ++o)
				{
					const Uint32 p0 = sr[c0[o]], p1 = sr[c1[o]];
					const int w1 = cw[o], w0 = 256 - w1;
					const int a0 = (p0 >> 24) ? w0 : 0, a1 = (p1 >> 24) ? w1 : 0;
					d[o * 4 + 0] = (int)((p0 >> 16) & 0xFF) * a0 + (int)((p1 >> 16) & 0xFF) * a1;
					d[o * 4 + 1] = (int)((p0 >> 8) & 0xFF) * a0 + (int)((p1 >> 8) & 0xFF) * a1;
					d[o * 4 + 2] = (int)(p0 & 0xFF) * a0 + (int)(p1 & 0xFF) * a1;
					d[o * 4 + 3] = a0 + a1;
				}
				return d;
			};
			for (int y = Y0; y < Y1; ++y)
			{
				const Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
				bool anyK = false;
				for (int x = X0; x < X1 && !anyK; ++x) anyK = cr[x] == k;
				if (!anyK) continue;
				const double c = ry + (y - Y0 + 0.5) * sty - 0.5;
				const int j0 = (int)std::floor(c);
				const int wy1 = (int)std::lround((c - j0) * 256), wy0 = 256 - wy1;
				const int *r0 = hrow(std::min(H - 1, std::max(0, j0)));
				const int *r1 = hrow(std::min(H - 1, std::max(0, j0 + 1)));
				Uint32 *dr = &smDst[(size_t)y * S.bufW];
				for (int x = X0; x < X1; ++x)
				{
					if (cr[x] != k) continue;
					const int o = (x - X0) * 4;
					const long long A = (long long)r0[o + 3] * wy0 + (long long)r1[o + 3] * wy1;
					if (A <= 0) continue;
					if (A == 65536)
					{
						// all four samples present (the usual case): plain shift
						const Uint32 r = (Uint32)((r0[o] * wy0 + r1[o] * wy1 + 32768) >> 16), g = (Uint32)((r0[o + 1] * wy0 + r1[o + 1] * wy1 + 32768) >> 16), b = (Uint32)((r0[o + 2] * wy0 + r1[o + 2] * wy1 + 32768) >> 16);
						dr[x] = (dr[x] & 0xFF000000u) | (r << 16) | (g << 8) | b;
						continue;
					}
					auto ch = [&](int i) { return (Uint32)std::min<long long>(255, ((long long)r0[o + i] * wy0 + (long long)r1[o + i] * wy1 + A / 2) / A); };
					dr[x] = (dr[x] & 0xFF000000u) | (ch(0) << 16) | (ch(1) << 8) | ch(2);
				}
			}
			continue;
		}
		else
		{
			HiResResample::resample(small.data(), W, H, rx, ry, rw, rh, outW, outH, HiResResample::PIXEL, out);
		}
		if (out.size() != (size_t)outW * outH)
			continue;
		// write back where this class is; premultiplied result over missing neighbours -> normalised
		for (int y = Y0; y < Y1; ++y)
		{
			const Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
			Uint32 *dr = &smDst[(size_t)y * S.bufW];
			const Uint32 *orow = &out[(size_t)(y - Y0) * outW - X0];
			for (int x = X0; x < X1; ++x)
			{
				if (cr[x] != k)
					continue;
				const Uint32 q = orow[x];
				const unsigned a = q >> 24;
				if (!a)
					continue;
				auto ch = [&](int sh) { return std::min(255u, (((q >> sh) & 0xFF) * 255 + a / 2) / a); };
				dr[x] = (dr[x] & 0xFF000000u) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
			}
		}
	}
}

/// Hi-res asset command: the replacement picture resampled to the output size of the box, drawn
/// only on the base pixels whose proxy pixels reached the screen unchanged (mask) and not hidden later.
void drawAsset(const HiResCmd &c, int dstW, int dstH, double sx, double sy, SDL_Rect &dirty)
{
	static const std::vector<Uint8> noMask;
	static const std::vector<Uint32> noBack;
	if (!c.layered && (!c.maskIdx || c.maskIdx > S.masks.size()))
		return;
	if (c.w <= 0 || c.h <= 0)
		return;
	const std::vector<Uint8> &mask = c.layered ? noMask : S.masks[c.maskIdx - 1];
	if (!c.layered && mask.size() != (size_t)c.clipW * c.clipH)
		return;
	const std::vector<Uint32> &back = c.layered ? noBack : S.backs[c.maskIdx - 1];
	const int X0 = (int)std::lround(c.x * sx), Y0 = (int)std::lround(c.y * sy);
	const int X1 = (int)std::lround((c.x + c.w) * sx), Y1 = (int)std::lround((c.y + c.h) * sy);
	const int outW = X1 - X0, outH = Y1 - Y0;
	if (outW <= 0 || outH <= 0)
		return;
	const Uint32 *px = HiResAssets::rendered(c.assetId, c.palKey, c.srcX, c.srcY, c.srcW, c.srcH, outW, outH);
	if (!px)
		return;
	const Uint8 *bm = c.battle ? HiResAssets::renderedMask(c.assetId, c.srcX, c.srcY, c.srcW, c.srcH, outW, outH) : nullptr;
	const SDL_Color *bpal = HiResAssets::battlePalette();
	const int bfac = c.battle ? HiResAssets::shadeFactor(c.shade) : 256;
	const int x0 = std::max(std::max(0, X0), (int)std::floor(c.clipX * sx)), y0 = std::max(std::max(0, Y0), (int)std::floor(c.clipY * sy));
	const int x1 = std::min(std::min(dstW, X1), (int)std::ceil((c.clipX + c.clipW) * sx));
	const int y1 = std::min(std::min(dstH, Y1), (int)std::ceil((c.clipY + c.clipH) * sy));
	if (x0 >= x1 || y0 >= y1)
		return;
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	bool drawn = false;
	double fx = 0, fy = 0;
	if (SM.inMap && c.layered && HiResAssets::nativeScale(c.assetId, fx, fy))
	{
		// scale mask: source pixels per base pixel; upscaled (output > source) = nearest on its grid + class
		const int nW = std::max(1, (int)std::lround(c.srcW * fx)), nH = std::max(1, (int)std::lround(c.srcH * fy));
		const double kx = nW / (double)c.w, ky = nH / (double)c.h;
		const bool up = sx / kx > 1.0 + 1e-6 && sy / ky > 1.0 + 1e-6;
		const Uint8 k = up ? scaleClass(kx, ky, fracOf(c.x * kx), fracOf(c.y * ky)) : 0;
		const Uint32 *npx = k ? HiResAssets::rendered(c.assetId, c.palKey, c.srcX, c.srcY, c.srcW, c.srcH, nW, nH) : nullptr;
		if (npx)
		{
			const Uint8 *nbm = c.battle ? HiResAssets::renderedMask(c.assetId, c.srcX, c.srcY, c.srcW, c.srcH, nW, nH) : nullptr;
			std::vector<int> colU(x1 - x0);
			for (int x = x0; x < x1; ++x)
				colU[x - x0] = (int)std::floor((x * isx - c.x) * kx + 1e-9);
			for (int y = y0; y < y1; ++y)
			{
				const int by = std::min(S.baseH - 1, (int)(y * isy));
				if (by < c.clipY || by >= c.clipY + c.clipH)
					continue;
				const int v = (int)std::floor((y * isy - c.y) * ky + 1e-9);
				if (v < 0 || v >= nH)
					continue;
				const Uint32 *stampRow = &S.stamps[by * S.baseW];
				const Uint32 *src = npx + (size_t)v * nW;
				Uint32 *dst = &S.rgba[y * S.bufW];
				Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
				for (int x = x0; x < x1; ++x)
				{
					const int u = colU[x - x0];
					const int bx = std::min(S.baseW - 1, (int)(x * isx));
					if (u < 0 || u >= nW || bx < c.clipX || bx >= c.clipX + c.clipW || stampRow[bx] > c.stamp)
						continue;
					const Uint32 p = c.battle ? battlePixel(c, src[u], nbm ? nbm[(size_t)v * nW + u] : 0, bpal, bfac) : src[u];
					if (!(p >> 24))
						continue;
					HiResResample::blendOver(dst[x], p);
					cr[x] = k;
					drawn = true;
				}
			}
			if (drawn)
				unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
			return;
		}
	}
	for (int y = y0; y < y1; ++y)
	{
		const int by = std::min(S.baseH - 1, (int)(y * isy));
		const int my = by - c.clipY;
		if (my < 0 || my >= c.clipH)
			continue;
		const Uint32 *stampRow = &S.stamps[by * S.baseW];
		const Uint8 *maskRow = c.layered ? nullptr : &mask[(size_t)my * c.clipW];
		const Uint32 *src = px + (size_t)(y - Y0) * outW - X0;
		Uint32 *dst = &S.rgba[y * S.bufW];
		for (int x = x0; x < x1; ++x)
		{
			const int bx = std::min(S.baseW - 1, (int)(x * isx));
			const int mx = bx - c.clipX;
			if (mx < 0 || mx >= c.clipW || (maskRow && !maskRow[mx]) || stampRow[bx] > c.stamp)
				continue;
			Uint32 p = c.battle ? battlePixel(c, src[x], bm ? bm[(size_t)(y - Y0) * outW + (x - X0)] : 0, bpal, bfac) : src[x];
			const unsigned a = p >> 24;
			if (maskRow && maskRow[mx] == MASK_BACK && a < 255)
			{
				// silhouette pixel with a known background: the picture over that colour, opaque
				// (covers the low-res proxy pixel, the outline follows the picture's alpha)
				const Uint32 b = back[(size_t)my * c.clipW + mx];
				Uint32 out = 0xFF000000u | (b & 0xFFFFFF);
				{
					// a hi-res background already drawn next to it (e.g. a hi-res tile): the same
					// point of the neighbouring base pixel instead of the flat 8bpp colour
					static const int nd[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
					const int dir = (int)((b >> 24) & 3);
					const int nx = x + (int)std::lround(nd[dir][0] * sx), ny = y + (int)std::lround(nd[dir][1] * sy);
					if (nx >= 0 && ny >= 0 && nx < dstW && ny < dstH)
					{
						const Uint32 q = S.rgba[ny * S.bufW + nx];
						if ((q >> 24) == 255)
							out = q;
					}
				}
				HiResResample::blendOver(out, p); // premultiplied picture over the background
				dst[x] = out;
				drawn = true;
				continue;
			}
			if (!a)
				continue;
			drawn = true;
			HiResResample::blendOver(dst[x], p); // premultiplied (HiResResample)
			setCls(x, y, 0); // 1x / downscaled: final
		}
	}
	if (drawn && c.tint && !c.layered)
		applyTint(c, &mask, x0, y0, x1, y1, sx, sy);
	if (drawn)
		unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
}

/// A hi-res asset command reaches the screen through 'src' (8bpp, blitted at x, y): resolves its
/// palette and the snapshot mask (base pixels showing the proxy pixel it expects). False = drop it.
bool deliverAsset(HiResCmd &c, SDL_Surface *src, int x, int y)
{
	int ew = 0, eh = 0;
	const Uint8 *e = HiResAssets::expected(c.assetId, ew, eh);
	if (!e || !src || src->format->BitsPerPixel != 8 || c.w <= 0 || c.h <= 0 || c.srcW <= 0 || c.srcH <= 0)
		return false;
	int vx = c.clipX, vy = c.clipY, vw = c.clipW, vh = c.clipH;
	if (!intersect(vx, vy, vw, vh, c.x, c.y, c.w, c.h) || !intersect(vx, vy, vw, vh, x, y, src->w, src->h))
		return false;
	c.clipX = (Sint16)vx; c.clipY = (Sint16)vy; c.clipW = (Sint16)vw; c.clipH = (Sint16)vh;
	c.palKey = HiResAssets::paletteKey(c.assetId, src->format->palette);
	if (c.layered)
		return true; // draw order decides (layered battlescape): no 8bpp pixel match
	std::vector<Uint8> m((size_t)vw * vh, 0);
	std::vector<Uint32> bk;
	bool any = false;
	// silhouette: pixels under the transparent proxy pixels when the asset was drawn
	const std::vector<Uint8> *under = nullptr;
	if (c.underId)
	{
		auto ui = S.unders.find(c.underId);
		if (ui != S.unders.end() && ui->second.size() == (size_t)c.srcW * c.srcH)
			under = &ui->second;
	}
	auto proxyAt = [&](int u, int v) -> int
	{
		if (u < 0 || v < 0 || u >= ew || v >= eh || u < c.srcX || v < c.srcY || u >= c.srcX + c.srcW || v >= c.srcY + c.srcH)
			return -1;
		return e[(size_t)v * ew + u];
	};
	auto underAt = [&](int u, int v) -> int
	{
		if (!under || u < c.srcX || v < c.srcY || u >= c.srcX + c.srcW || v >= c.srcY + c.srcH)
			return -1;
		return (*under)[(size_t)(v - c.srcY) * c.srcW + (u - c.srcX)];
	};
	const SDL_Color *spal = src->format->palette ? src->format->palette->colors : nullptr;
	int bownW = 0, bownH = 0;
	const Uint32 *bown = (c.battle && c.battleId) ? HiResAssets::battleOwners(src, bownW, bownH) : nullptr;
	if (bown && (bownW != src->w || bownH != src->h))
		bown = nullptr;
	SDL_LockSurface(src);
	for (int yy = vy; yy < vy + vh; ++yy)
	{
		const int v = std::min(eh - 1, std::min(c.srcY + c.srcH - 1, c.srcY + (yy - c.y) * c.srcH / c.h));
		if (v < 0)
			continue;
		const Uint8 *sp = (const Uint8*)src->pixels + (yy - y) * src->pitch - x;
		const Uint8 *ep = e + (size_t)v * ew;
		Uint8 *mp = &m[(size_t)(yy - vy) * vw - vx];
		for (int xx = vx; xx < vx + vw; ++xx)
		{
			const int u = std::min(ew - 1, std::min(c.srcX + c.srcW - 1, c.srcX + (xx - c.x) * c.srcW / c.w));
			if (u < 0)
				continue;
			if (bown && bown[(size_t)(yy - y) * bownW + (xx - x)] != c.battleId)
				continue; // a later 8-bit sprite / other battle draw owns this map pixel
			const Uint8 ev = c.battle ? battleExpected(c, ep[u]) : ep[u];
			if (c.tint && ev && sp[xx])
			{
				mp[xx] = MASK_PROXY; // recoloured copy (highlight): every opaque proxy pixel
				any = true;
			}
			else if (!c.tint && ev && sp[xx] == ev)
			{
				mp[xx] = MASK_PROXY;
				any = true;
				if (under && spal)
				{
					// on the silhouette: background from a transparent neighbour (4-neighbourhood)
					static const int nb[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
					for (const auto &d : nb)
					{
						if (proxyAt(u + d[0], v + d[1]) != 0)
							continue;
						const int uv = underAt(u + d[0], v + d[1]);
						if (uv <= 0)
							continue;
						if (bk.empty())
							bk.assign((size_t)vw * vh, 0);
						const SDL_Color &col = spal[uv];
						// colour of the background pixel + which neighbour it is (bits 24-25: -x, +x, -y, +y)
						bk[(size_t)(yy - vy) * vw + (xx - vx)] = ((Uint32)(&d - nb) << 24) | ((Uint32)col.r << 16) | ((Uint32)col.g << 8) | col.b;
						mp[xx] = MASK_BACK;
						break;
					}
				}
			}
			else if (!ev && under)
			{
				// transparent proxy pixel next to the silhouette, still showing what was under it
				bool edge = false;
				for (int dv = -1; dv <= 1 && !edge; ++dv)
					for (int du = -1; du <= 1 && !edge; ++du)
						if ((du || dv) && proxyAt(u + du, v + dv) > 0)
							edge = true;
				const int uv = edge ? underAt(u, v) : -1;
				if (uv >= 0 && sp[xx] == uv)
				{
					mp[xx] = MASK_EDGE;
					any = true;
				}
			}
		}
	}
	SDL_UnlockSurface(src);
	if (!any)
		return false;
	if (!c.tint && !c.battle && S.screen && (S.screen->format->BytesPerPixel == 4 || (S.screen->format->BitsPerPixel == 8 && S.screen->format->palette)))
	{
		// Output-resolution coverage: inside the proxy box grown by one base pixel around the silhouette,
		// the picture's own alpha decides each output pixel (drawAsset). The 8bpp screen pixel next to the
		// silhouette is the real background (a transparent proxy pixel left it untouched), also for assets
		// that came through an intermediate surface (no 'under' snapshot). The base-pixel stamps still
		// hide everything a later blit covers.
		const std::vector<Uint8> orig = m;
		static const int nb[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
		SDL_LockSurface(S.screen);
		// screen colour (8bpp palette or 32bpp buffer), -1 = outside
		auto screenAt = [&](int sx, int sy) -> long
		{
			if (sx < 0 || sy < 0 || sx >= S.screen->w || sy >= S.screen->h)
				return -1;
			const Uint8 *row = (const Uint8*)S.screen->pixels + sy * S.screen->pitch;
			const Uint32 pv = S.screen->format->BytesPerPixel == 4 ? ((const Uint32*)row)[sx] : row[sx];
			Uint8 r, g, b;
			SDL_GetRGB(pv, S.screen->format, &r, &g, &b);
			return ((long)r << 16) | ((long)g << 8) | b;
		};
		auto origAt = [&](int xx, int yy) -> Uint8
		{
			if (xx < vx || yy < vy || xx >= vx + vw || yy >= vy + vh)
				return MASK_NONE;
			return orig[(size_t)(yy - vy) * vw + (xx - vx)];
		};
		for (int yy = vy; yy < vy + vh; ++yy)
		{
			const int v = std::min(eh - 1, std::min(c.srcY + c.srcH - 1, c.srcY + (yy - c.y) * c.srcH / c.h));
			for (int xx = vx; xx < vx + vw; ++xx)
			{
				const size_t mi = (size_t)(yy - vy) * vw + (xx - vx);
				const int u = std::min(ew - 1, std::min(c.srcX + c.srcW - 1, c.srcX + (xx - c.x) * c.srcW / c.w));
				if (orig[mi] == MASK_NONE)
				{
					if (v < 0 || u < 0 || e[(size_t)v * ew + u])
						continue; // an opaque proxy pixel that did not reach the screen: covered
					bool nearSil = false;
					for (int dy = -1; dy <= 1 && !nearSil; ++dy)
						for (int dx = -1; dx <= 1 && !nearSil; ++dx)
							if ((dx || dy) && origAt(xx + dx, yy + dy) == MASK_PROXY)
								nearSil = true;
					if (nearSil)
					{
						m[mi] = MASK_EDGE;
						any = true;
					}
				}
				else if (orig[mi] == MASK_PROXY)
				{
					for (const auto &d : nb)
					{
						const int nx = xx + d[0], ny = yy + d[1];
						const int nu = std::min(ew - 1, std::max(0, c.srcX + (nx - c.x) * c.srcW / c.w));
						const int nv = std::min(eh - 1, std::max(0, c.srcY + (ny - c.y) * c.srcH / c.h));
						const bool outside = nx < c.x || ny < c.y || nx >= c.x + c.w || ny >= c.y + c.h;
						if (!outside && e[(size_t)nv * ew + nu])
							continue;
						const long sv = screenAt(nx, ny);
						if (sv < 0)
							continue;
						if (bk.empty())
							bk.assign((size_t)vw * vh, 0);
						bk[mi] = ((Uint32)(&d - nb) << 24) | (Uint32)sv;
						m[mi] = MASK_BACK;
						break;
					}
				}
			}
		}
		SDL_UnlockSurface(S.screen);
	}
	S.masks.push_back(std::move(m));
	S.backs.push_back(std::move(bk));
	c.maskIdx = (Uint32)S.masks.size();
	return true;
}

/// Snapshot of the pixels under a hi-res asset's transparent proxy pixels next to its silhouette,
/// taken when the asset is drawn into 'dst' (after the 8bpp blit: transparent proxy pixels left the
/// destination untouched). Indexed per proxy pixel of the drawn source rect; shared by content.
Uint32 snapshotUnder(const HiResCmd &c, const SDL_Surface *dst)
{
	int ew = 0, eh = 0;
	const Uint8 *e = HiResAssets::expected(c.assetId, ew, eh);
	if (!e || !dst || dst->format->BitsPerPixel != 8 || c.srcW <= 0 || c.srcH <= 0 || c.w <= 0 || c.h <= 0)
		return 0;
	auto he = S.assetHasEdge.find(c.assetId);
	if (he == S.assetHasEdge.end())
	{
		bool edge = false;
		for (int v = 0; v < eh && !edge; ++v)
			for (int u = 0; u < ew && !edge; ++u)
			{
				if (e[(size_t)v * ew + u])
					continue;
				for (int dv = -1; dv <= 1 && !edge; ++dv)
					for (int du = -1; du <= 1 && !edge; ++du)
					{
						const int uu = u + du, vv = v + dv;
						if (uu >= 0 && vv >= 0 && uu < ew && vv < eh && e[(size_t)vv * ew + uu])
							edge = true;
					}
			}
		he = S.assetHasEdge.emplace(c.assetId, edge).first;
	}
	if (!he->second)
		return 0;
	std::vector<Uint8> u((size_t)c.srcW * c.srcH, 0);
	int vx = c.clipX, vy = c.clipY, vw = c.clipW, vh = c.clipH;
	if (!intersect(vx, vy, vw, vh, 0, 0, dst->w, dst->h))
		return 0;
	for (int yy = vy; yy < vy + vh; ++yy)
	{
		const int v = c.srcY + (yy - c.y) * c.srcH / c.h;
		if (v < c.srcY || v >= c.srcY + c.srcH)
			continue;
		const Uint8 *dp = (const Uint8*)dst->pixels + yy * dst->pitch;
		for (int xx = vx; xx < vx + vw; ++xx)
		{
			const int uu = c.srcX + (xx - c.x) * c.srcW / c.w;
			if (uu < c.srcX || uu >= c.srcX + c.srcW)
				continue;
			u[(size_t)(v - c.srcY) * c.srcW + (uu - c.srcX)] = dp[xx];
		}
	}
	size_t h = std::hash<std::string>()(std::string((const char*)u.data(), u.size()));
	h ^= (size_t)c.assetId * 0x9E3779B97F4A7C15ull ^ ((size_t)c.srcX << 1) ^ ((size_t)c.srcY << 17) ^ ((size_t)c.srcW << 33) ^ ((size_t)c.srcH << 49);
	auto hit = S.underByHash.find(h);
	if (hit != S.underByHash.end())
	{
		auto ui = S.unders.find(hit->second);
		if (ui != S.unders.end() && ui->second == u)
			return hit->second;
	}
	if (S.unders.size() >= 4096)
	{
		// bounded: older snapshots are dropped (their commands fall back to the proxy silhouette)
		S.unders.clear();
		S.underByHash.clear();
	}
	if (++S.underCounter == 0)
		++S.underCounter;
	S.unders.emplace(S.underCounter, std::move(u));
	S.underByHash[h] = S.underCounter;
	return S.underCounter;
}

void drawImage(const HiResCmd &c, int dstW, int dstH, double sx, double sy, SDL_Rect &dirty)
{
	if (c.assetId)
	{
		drawAsset(c, dstW, dstH, sx, sy, dirty);
		return;
	}
	if (c.clearRect || c.spriteKey)
	{
		const int x0 = std::max(0, (int)std::floor(c.clipX * sx)), y0 = std::max(0, (int)std::floor(c.clipY * sy));
		const int x1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
		const int y1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
		if (x0 >= x1 || y0 >= y1)
			return;
		const double isx = 1.0 / sx, isy = 1.0 / sy;
		if (c.clearRect)
		{
			const Uint32 col = 0xFF000000u | ((Uint32)c.fill.r << 16) | ((Uint32)c.fill.g << 8) | c.fill.b;
			for (int y = y0; y < y1; ++y)
			{
				const int by = std::min(S.baseH - 1, (int)(y * isy));
				const Uint32 *stampRow = &S.stamps[by * S.baseW];
				Uint32 *dst = &S.rgba[y * S.bufW];
				for (int x = x0; x < x1; ++x)
					if (stampRow[std::min(S.baseW - 1, (int)(x * isx))] <= c.stamp)
					{
						dst[x] = col;
						setCls(x, y, 1);
					}
			}
			unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
			return;
		}
		if (SM.inMap)
		{
			// scale mask: the base-pixel sprite by nearest on the 8-bit grid (the stamps' grid), class 1
			auto si = sprites.find(c.spriteKey);
			if (si == sprites.end())
				return;
			const Sprite8 &sp = si->second;
			bool drawn = false;
			for (int y = y0; y < y1; ++y)
			{
				const int by = std::min(S.baseH - 1, (int)(y * isy));
				const int v = by - c.y;
				if (v < 0 || v >= sp.h)
					continue;
				const Uint32 *stampRow = &S.stamps[by * S.baseW];
				const Uint32 *src = &sp.px[(size_t)v * sp.w];
				Uint32 *dst = &S.rgba[y * S.bufW];
				Uint8 *cr = &SM.cls[(size_t)y * S.bufW];
				for (int x = x0; x < x1; ++x)
				{
					const int bx = std::min(S.baseW - 1, (int)(x * isx));
					const int u = bx - c.x;
					if (u < 0 || u >= sp.w)
						continue;
					const Uint32 p = src[u];
					if (!(p >> 24) || stampRow[bx] > c.stamp)
						continue;
					dst[x] = p;
					cr[x] = 1;
					drawn = true;
				}
			}
			if (drawn)
				unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
			return;
		}
		const int X0 = (int)std::lround(c.x * sx), Y0 = (int)std::lround(c.y * sy);
		const int outW = (int)std::lround((c.x + c.w) * sx) - X0, outH = (int)std::lround((c.y + c.h) * sy) - Y0;
		const Uint32 *px = spriteRendered(c.spriteKey, outW, outH, c.nearest != 0);
		if (!px)
			return;
		const int xa = std::max(x0, X0), ya = std::max(y0, Y0), xb = std::min(x1, X0 + outW), yb = std::min(y1, Y0 + outH);
		bool drawn = false;
		for (int y = ya; y < yb; ++y)
		{
			const int by = std::min(S.baseH - 1, (int)(y * isy));
			const Uint32 *stampRow = &S.stamps[by * S.baseW];
			const Uint32 *src = px + (size_t)(y - Y0) * outW - X0;
			Uint32 *dst = &S.rgba[y * S.bufW];
			for (int x = xa; x < xb; ++x)
			{
				const Uint32 p = src[x];
				if (!(p >> 24) || stampRow[std::min(S.baseW - 1, (int)(x * isx))] > c.stamp)
					continue;
				HiResResample::blendOver(dst[x], p);
				drawn = true;
			}
		}
		if (drawn)
			unionRect(dirty, xa, ya, xb - xa, yb - ya);
		return;
	}
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
	if (c.tint)
		applyTint(c, nullptr, x0, y0, x1, y1, sx, sy);
	unionRect(dirty, x0, y0, x1 - x0, y1 - y0);
}

/// Placement of one glyph command in output pixels.
struct Placed
{
	const HiResGlyph *g = nullptr;
	int pen = 0, baseline = 0;
	int adv = 0;          ///< advance (output px) for underlines
	bool marker = false;  ///< GLYPH_MARKER: position only
};

/// Last IME position marker (screen base px) of the frame being composed / the last composed frame.
bool caretFound = false, caretValid = false;
double caretX = 0, caretY = 0, caretH = 0, caretNX = 0, caretNY = 0, caretNH = 0;

/// Solid rect in output px (caret, underline), clipped and hidden like glyphs.
void drawSolid(int x, int y, int w, int h, const SDL_Color &color, int cx0, int cy0, int cx1, int cy1, double sx, double sy, Uint32 stamp, SDL_Rect &dirty)
{
	if (w <= 0 || h <= 0)
		return;
	std::vector<uint8_t> bm((size_t)w * h, 255);
	drawCoverage(bm, w, h, x, y, color, cx0, cy0, cx1, cy1, sx, sy, stamp, dirty);
}

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

bool glMapActive = false; ///< this frame the layered map commands are drawn by the GL map pass (renderGL)

// ---------------------------------------------------------------------------------------------
// Map cache (battle-4): the interpolated map of the last frame is kept; a frame only recomposes what
// changed. The draw list of the map layer is compared with the last one (content signature per
// command, position apart): commands that moved by one common offset are a scroll (the finished frame
// is shifted), added / removed / changed commands mark dirty rects (grown by the filter radius), and
// an identical list draws nothing. Everything else (size, filter, order, class grid phase, ...) is a
// full recompose.
Uint64 cacheEpoch = 1; ///< bumped when cached results may be stale (mode, fonts, context, sprite store...)

struct MapSig
{
	Uint64 sig = 0;
	int x = 0, y = 0;                 ///< command position (base px)
	int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0; ///< visible rect: box x clip (base px)
	bool clear = false, overlay = false;
	Uint32 spriteKey = 0;
	bool hasClass = false;
	double kx = 0, ky = 0, phx = 0, phy = 0;
};

struct MapCacheState
{
	bool valid = false;
	int backend = 0;                  ///< 1 = CPU buffers, 2 = GL textures
	Uint64 epoch = 0;
	int dstW = 0, dstH = 0, baseW = 0, baseH = 0;
	double sx = 0, sy = 0;
	int mode = -1;
	bool debug = false;
	std::vector<MapSig> sigs;
	int mx0 = 0, my0 = 0, mx1 = 0, my1 = 0; ///< map rect (base px)
} MC;

struct MapPlan
{
	bool walking = false;             ///< a unit walks in view this frame
	bool active = false;              ///< a map is drawn this frame
	bool full = true;                 ///< recompose everything
	bool unchanged = false;           ///< nothing to draw: the cached result is current
	int dx = 0, dy = 0, DX = 0, DY = 0; ///< scroll (base px / output px)
	std::vector<SDL_Rect> rects;      ///< base px, grown by the filter margin; empty when full
	int mx0 = 0, my0 = 0, mx1 = 0, my1 = 0;
	const char *reason = "";
} curPlan;

/// per-run counters for the log summary
struct FrameStats
{
	long long frames = 0, full = 0, scroll = 0, dirty = 0, unchanged = 0, fallback = 0;
	double mapMs = 0, totalMs = 0, maxMs = 0, wallMs = 0;
	long long wallFrames = 0;
	long long rectCount = 0;
	std::map<std::string, long long> reasons; ///< why the map was (re)composed: reason -> frames
} FS;

std::vector<SDL_Rect> walkRegions; ///< walking units of the map being recorded (map-surface px)

inline Uint64 mix64(Uint64 h, Uint64 v) { return (h ^ v) * 1099511628211ull; }

long long skippedFrames = 0;
std::string lastMapPath;
/// Logs (once per change) which path composes the battlefield; cpuReason = null: GPU
void logMapPath(bool gl, const char *cpuReason)
{
	if (!gl && !cpuReason)
		return;
	const int fm = HiResResample::mode();
	const std::string now = gl ? std::string("GPU (OpenGL framebuffers + GLSL") + (fm == HiResResample::PIXEL ? ", xBRZ shader)" : fm == HiResResample::HQX ? ", HQx shader)" : ")") : std::string("CPU - ") + cpuReason;
	if (now == lastMapPath)
		return;
	lastMapPath = now;
	Log(LOG_INFO) << "HiResLayer: battlefield path: " << now;
}

void logFrameSummary()
{
	if (FS.frames <= 0)
		return;
	Log(LOG_INFO) << "HiResLayer: " << FS.frames << " battlefield frames, " << (FS.wallFrames ? 1000.0 * FS.wallFrames / FS.wallMs : 0.0) << " fps, hi-res layer " << (FS.totalMs / FS.frames) << " ms avg, " << FS.maxMs
		<< " ms max; map: " << FS.full << " full, " << FS.scroll << " scroll-reuse, " << FS.dirty << " dirty-rect (" << FS.rectCount << " rects), "
		<< FS.unchanged << " unchanged, " << FS.fallback << " GL fallbacks, " << skippedFrames << " compose skips";
	if (!FS.reasons.empty())
	{
		std::ostringstream rs;
		for (const auto &r : FS.reasons) rs << " [" << r.first << ": " << r.second << "]";
		Log(LOG_INFO) << "HiResLayer: map plans:" << rs.str();
	}
	FS = FrameStats();
	skippedFrames = 0;
}

/// one battlefield frame of the layer took ms (plan + map + compose + upload)
void frameDone(double ms)
{
	if (!curPlan.active)
		return;
	static double lastFrame = 0;
	const double now = profNow();
	if (lastFrame > 0 && now - lastFrame < 250.0)
	{
		FS.wallMs += now - lastFrame;
		++FS.wallFrames;
	}
	lastFrame = now;
	++FS.frames;
	FS.totalMs += ms;
	FS.maxMs = std::max(FS.maxMs, ms);
	static const long long period = std::getenv("OXCE_HIRES_SUMMARY") ? std::max(10, std::atoi(std::getenv("OXCE_HIRES_SUMMARY"))) : 600;
	if (FS.frames >= period)
		logFrameSummary();
}

/// source grid of an interpolated hi-res picture (same rule as the draw): false = 8-bit grid / final
bool cmdScaleGrid(const HiResCmd &c, double sx, double sy, double &kx, double &ky, double &phx, double &phy)
{
	if (c.spriteKey)
	{
		kx = ky = 1.0; phx = phy = 0.0;
		return true;
	}
	double fx = 0, fy = 0;
	if (!c.assetId || !HiResAssets::nativeScale(c.assetId, fx, fy) || c.w <= 0 || c.h <= 0)
		return false;
	const int nW = std::max(1, (int)std::lround(c.srcW * fx)), nH = std::max(1, (int)std::lround(c.srcH * fy));
	kx = nW / (double)c.w; ky = nH / (double)c.h;
	if (!(sx / kx > 1.0 + 1e-6 && sy / ky > 1.0 + 1e-6))
		return false;
	phx = c.x * kx - std::floor(c.x * kx);
	phy = c.y * ky - std::floor(c.y * ky);
	return true;
}

MapSig makeSig(const HiResCmd &c, double sx, double sy)
{
	MapSig m;
	m.x = c.x; m.y = c.y;
	m.clear = c.clearRect != 0;
	m.overlay = !c.clearRect && c.nearest && c.spriteKey && !c.battle;
	m.spriteKey = c.spriteKey;
	int x0 = c.clipX, y0 = c.clipY, x1 = c.clipX + c.clipW, y1 = c.clipY + c.clipH;
	if (!m.clear)
	{
		x0 = std::max(x0, (int)c.x); y0 = std::max(y0, (int)c.y);
		x1 = std::min(x1, c.x + c.w); y1 = std::min(y1, c.y + c.h);
	}
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	m.vx0 = x0; m.vy0 = y0; m.vx1 = x1; m.vy1 = y1;
	Uint64 h = 1469598103934665603ull;
	h = mix64(h, (Uint64)c.kind); h = mix64(h, (Uint64)(Uint16)c.w); h = mix64(h, (Uint64)(Uint16)c.h);
	h = mix64(h, (Uint64)(Sint64)(x0 - c.x)); h = mix64(h, (Uint64)(Sint64)(y0 - c.y));
	h = mix64(h, (Uint64)(x1 - x0)); h = mix64(h, (Uint64)(y1 - y0));
	if (m.clear)
	{
		h = mix64(h, 0xC1EA4ull); h = mix64(h, c.fill.r); h = mix64(h, c.fill.g); h = mix64(h, c.fill.b);
		h = mix64(h, (Uint64)(Sint64)c.x); h = mix64(h, (Uint64)(Sint64)c.y);
	}
	h = mix64(h, c.assetId); h = mix64(h, c.palKey);
	h = mix64(h, (Uint64)(Uint16)c.srcX); h = mix64(h, (Uint64)(Uint16)c.srcY); h = mix64(h, (Uint64)(Uint16)c.srcW); h = mix64(h, (Uint64)(Uint16)c.srcH);
	h = mix64(h, c.battle); h = mix64(h, c.layered); h = mix64(h, c.nearest); h = mix64(h, c.spriteKey);
	h = mix64(h, c.shade); h = mix64(h, c.nvColor); h = mix64(h, c.tint);
	for (int i = 0; i < 4; ++i) { h = mix64(h, c.rcFrom[i]); h = mix64(h, c.rcTo[i]); }
	m.sig = h;
	if (!m.clear && !m.overlay)
		m.hasClass = cmdScaleGrid(c, sx, sy, m.kx, m.ky, m.phx, m.phy);
	return m;
}

inline bool rectOverlap(const SDL_Rect &a, const SDL_Rect &b, int pad = 0)
{
	return a.x < b.x + b.w + pad && b.x < a.x + a.w + pad && a.y < b.y + b.h + pad && b.y < a.y + a.h + pad;
}

void mergeRects(std::vector<SDL_Rect> &r)
{
	bool again = true;
	while (again)
	{
		again = false;
		for (size_t i = 0; i < r.size() && !again; ++i)
			for (size_t j = i + 1; j < r.size() && !again; ++j)
				if (rectOverlap(r[i], r[j]))
				{
					const int x0 = std::min(r[i].x, r[j].x), y0 = std::min(r[i].y, r[j].y);
					const int x1 = std::max(r[i].x + r[i].w, r[j].x + r[j].w), y1 = std::max(r[i].y + r[i].h, r[j].y + r[j].h);
					r[i] = SDL_Rect{ (Sint16)x0, (Sint16)y0, (Uint16)(x1 - x0), (Uint16)(y1 - y0) };
					r.erase(r.begin() + j);
					again = true;
				}
	}
}

/// Filter reach (base px) of the active interpolation: source taps + grid block + safety.
int filterMargin(int mode, int backend = 1)
{
	if (mode == HiResResample::NEAREST) return 2;
	if (mode == HiResResample::SMOOTH) return 4;
	if (backend == 2) return 8; // GPU xBRZ / HQx: exact, reach = 2 source cells + the output pixel footprint
	return 14; // HQX / xBRZ (pixel art) on the CPU
}

/// Compares this frame's map draw list with the cached one and decides what to redraw.
/// backend: 1 = CPU buffers, 2 = GL textures (a cache built by the other backend is not reused).
void planMapFrame(int dstW, int dstH, int backend)
{
	MapPlan P;
	P.walking = !walkRegions.empty();
	const double sx = dstW / (double)S.baseW, sy = dstH / (double)S.baseH;
	const int mode = HiResResample::mode();
	const bool debug = Options::oxceHiResDebugScaleMask;
	std::vector<MapSig> cur;
	std::vector<size_t> curIdx;
	bool haveClear = false;
	for (size_t i = 0; i < S.frame.size(); ++i)
	{
		const HiResCmd &c = S.frame[i];
		if (!c.mapLayer || c.kind != HiResCmdKind::Image)
			continue;
		cur.push_back(makeSig(c, sx, sy));
		curIdx.push_back(i);
		if (c.clearRect && !haveClear)
		{
			haveClear = true;
			P.mx0 = cur.back().vx0; P.my0 = cur.back().vy0; P.mx1 = cur.back().vx1; P.my1 = cur.back().vy1;
		}
	}
	P.active = !cur.empty();
	if (!P.active)
	{
		MC.valid = false;
		MC.sigs.clear();
		curPlan = P;
		return;
	}
	auto commit = [&]()
	{
		MC.valid = true; MC.backend = backend; MC.epoch = cacheEpoch;
		MC.dstW = dstW; MC.dstH = dstH; MC.baseW = S.baseW; MC.baseH = S.baseH; MC.sx = sx; MC.sy = sy;
		MC.mode = mode; MC.debug = debug;
		MC.sigs = cur;
		++FS.reasons[std::string(P.reason[0] ? P.reason : "?") + (P.walking ? " +walking" : "")];
		MC.mx0 = P.mx0; MC.my0 = P.my0; MC.mx1 = P.mx1; MC.my1 = P.my1;
		curPlan = P;
	};
	P.full = true;
	if (!haveClear) { P.reason = "no clear command"; commit(); return; }
	if (Options::oxceHiResForceFullRedraw) { P.reason = "forced (oxceHiResForceFullRedraw)"; commit(); return; }
	if (!MC.valid) { P.reason = "no cache"; commit(); return; }
	if (MC.backend != backend || MC.epoch != cacheEpoch || MC.dstW != dstW || MC.dstH != dstH || MC.baseW != S.baseW || MC.baseH != S.baseH
		|| MC.mode != mode || MC.debug != debug || std::fabs(MC.sx - sx) > 1e-9 || std::fabs(MC.sy - sy) > 1e-9)
	{ P.reason = "mode / size changed"; commit(); return; }
	if (MC.mx0 != P.mx0 || MC.my0 != P.my0 || MC.mx1 != P.mx1 || MC.my1 != P.my1) { P.reason = "map rect changed"; commit(); return; }

	const std::vector<MapSig> &prev = MC.sigs;
	// clear command and overlay (whole-map sprite of unrecorded 8-bit draws) are handled apart
	const MapSig *prevClear = nullptr, *prevOv = nullptr, *curClear = nullptr, *curOv = nullptr;
	for (const MapSig &m : prev) { if (m.clear && !prevClear) prevClear = &m; if (m.overlay) prevOv = &m; }
	for (const MapSig &m : cur) { if (m.clear && !curClear) curClear = &m; if (m.overlay) curOv = &m; }
	if (!prevClear || !curClear || prevClear->sig != curClear->sig) { P.reason = "map background changed"; commit(); return; }

	// same list as last frame (content and positions): nothing to do
	if (cur.size() == prev.size())
	{
		bool same = true;
		for (size_t i = 0; i < cur.size() && same; ++i)
			same = cur[i].sig == prev[i].sig && cur[i].x == prev[i].x && cur[i].y == prev[i].y;
		if (same)
		{
			P.full = false; P.unchanged = true; P.reason = "unchanged";
			commit(); return;
		}
	}
	// scroll: the offset most commands moved by
	std::unordered_multimap<Uint64, int> bySig;
	size_t nShift = 0, voters = 0;
	for (size_t i = 0; i < prev.size(); ++i)
		if (!prev[i].clear && !prev[i].overlay) bySig.emplace(prev[i].sig, (int)i);
	std::map<std::pair<int, int>, int> votes;
	const int mapW = P.mx1 - P.mx0, mapH = P.my1 - P.my0;
	// votes of the commands whose content is rare in the last frame (unique sprites: units, objects, walls);
	// the very common ones (plain floor tiles) only if the rare ones decide nothing
	for (int pass = 0; pass < 2; ++pass)
	{
		const size_t maxGroup = pass == 0 ? 4 : 24;
		if (pass == 1 && !votes.empty())
		{
			voters = 0; // second pass decides alone
			int top = 0;
			for (auto &v : votes) top = std::max(top, v.second);
			if (top >= 8) break;
		}
		for (const MapSig &m : cur)
		{
			if (m.clear || m.overlay) continue;
			if (pass == 0) ++nShift;
			if (bySig.count(m.sig) > maxGroup || (pass == 1 && bySig.count(m.sig) <= 4)) continue;
			auto range = bySig.equal_range(m.sig);
			++voters;
			for (auto it = range.first; it != range.second; ++it)
			{
				const MapSig &o = prev[it->second];
				const int ddx = m.x - o.x, ddy = m.y - o.y;
				if (std::abs(ddx) <= mapW && std::abs(ddy) <= mapH)
					++votes[{ddx, ddy}];
			}
		}
	}
	int dx = 0, dy = 0, best = votes.count({0, 0}) ? votes[{0, 0}] : 0;
	for (auto &v : votes)
		if (v.second > best) { best = v.second; dx = v.first.first; dy = v.first.second; }
	if ((dx || dy) && best < (int)std::max<size_t>(8, voters / 3)) { dx = dy = 0; }
	int DX = 0, DY = 0;
	if (dx || dy)
	{
		DX = (int)std::lround(dx * sx); DY = (int)std::lround(dy * sy);
		if (std::fabs(DX - dx * sx) > 1e-3 || std::fabs(DY - dy * sy) > 1e-3)
		{
			P.reason = "scroll not on the output pixel grid";
			commit(); return;
		}
	}

	// match: same content signature at the position + (dx, dy); same class grid (phase)
	std::unordered_map<Uint64, std::vector<int>> byKey;
	auto keyOf = [](Uint64 sig, int x, int y) { return mix64(mix64(sig, (Uint64)(Sint64)x), (Uint64)(Sint64)y); };
	for (size_t i = 0; i < prev.size(); ++i)
		if (!prev[i].clear && !prev[i].overlay) byKey[keyOf(prev[i].sig, prev[i].x + dx, prev[i].y + dy)].push_back((int)i);
	std::vector<char> prevUsed(prev.size(), 0);
	std::vector<SDL_Rect> rects;
	const int M = filterMargin(mode, backend);
	auto addRect = [&](int x0, int y0, int x1, int y1)
	{
		x0 = std::max(x0 - M, P.mx0); y0 = std::max(y0 - M, P.my0);
		x1 = std::min(x1 + M, P.mx1); y1 = std::min(y1 + M, P.my1);
		if (x1 > x0 && y1 > y0)
			rects.push_back(SDL_Rect{ (Sint16)x0, (Sint16)y0, (Uint16)(x1 - x0), (Uint16)(y1 - y0) });
	};
	int lastPrev = -1;
	for (const MapSig &m : cur)
	{
		if (m.clear || m.overlay) continue;
		bool matched = false;
		auto it = byKey.find(keyOf(m.sig, m.x, m.y));
		if (it != byKey.end())
		{
			for (int pi : it->second)
			{
				if (prevUsed[pi]) continue;
				const MapSig &o = prev[pi];
				const bool sameGrid = (o.hasClass == m.hasClass)
					&& (!m.hasClass || (std::fabs(o.kx - m.kx) < 1e-6 && std::fabs(o.ky - m.ky) < 1e-6 && std::fabs(o.phx - m.phx) < 1e-6 && std::fabs(o.phy - m.phy) < 1e-6));
				if (!sameGrid) continue;
				if (pi < lastPrev) { P.reason = "draw order changed"; commit(); return; }
				lastPrev = pi;
				prevUsed[pi] = 1;
				matched = true;
				break;
			}
		}
		if (!matched)
		{
			addRect(m.vx0, m.vy0, m.vx1, m.vy1);
		}
	}
	for (size_t i = 0; i < prev.size(); ++i)
		if (!prev[i].clear && !prev[i].overlay && !prevUsed[i])
			addRect(prev[i].vx0 + dx, prev[i].vy0 + dy, prev[i].vx1 + dx, prev[i].vy1 + dy);

	// exposed strips of a scroll
	if (dx > 0) addRect(P.mx0, P.my0, P.mx0 + dx, P.my1);
	if (dx < 0) addRect(P.mx1 + dx, P.my0, P.mx1, P.my1);
	if (dy > 0) addRect(P.mx0, P.my0, P.mx1, P.my0 + dy);
	if (dy < 0) addRect(P.mx0, P.my1 + dy, P.mx1, P.my1);

	// overlay (unrecorded 8-bit draws as one map-sized sprite): compare the pixels, 8x8 base tiles
	if (prevOv || curOv)
	{
		const bool same = prevOv && curOv && prevOv->spriteKey == curOv->spriteKey && prevOv->x == curOv->x && prevOv->y == curOv->y;
		if (!(same && !dx && !dy))
		{
			const Sprite8 *ps = nullptr, *cs = nullptr;
			if (prevOv) { auto i = sprites.find(prevOv->spriteKey); if (i == sprites.end()) { P.reason = "overlay sprite gone"; commit(); return; } ps = &i->second; }
			if (curOv) { auto i = sprites.find(curOv->spriteKey); if (i == sprites.end()) { P.reason = "overlay sprite gone"; commit(); return; } cs = &i->second; }
			// compare over the map rect, in map-surface coordinates of the current frame
			const int ox = curOv ? curOv->x : prevOv->x + dx, oy = curOv ? curOv->y : prevOv->y + dy;
			const int pox = prevOv ? prevOv->x + dx : 0, poy = prevOv ? prevOv->y + dy : 0;
			for (int ty = P.my0; ty < P.my1; ty += 8)
			{
				for (int tx = P.mx0; tx < P.mx1; tx += 8)
				{
					bool diff = false;
					const int ex = std::min(tx + 8, P.mx1), ey = std::min(ty + 8, P.my1);
					for (int yy = ty; yy < ey && !diff; ++yy)
						for (int xx = tx; xx < ex; ++xx)
						{
							Uint32 a = 0, b = 0;
							if (cs) { const int u = xx - ox, v = yy - oy; if (u >= 0 && v >= 0 && u < cs->w && v < cs->h) a = cs->px[(size_t)v * cs->w + u]; }
							if (ps) { const int u = xx - pox, v = yy - poy; if (u >= 0 && v >= 0 && u < ps->w && v < ps->h) b = ps->px[(size_t)v * ps->w + u]; }
							if (a != b) { diff = true; break; }
						}
					if (diff) addRect(tx, ty, ex, ey);
				}
			}
		}
	}
	P.dx = dx; P.dy = dy; P.DX = DX; P.DY = DY;
	// a walking / flying unit: its region is recomposed too (the planner already catches what changed; the region adds
	// the unit's own surroundings as a safety net and switches to the higher threshold below)
	for (const SDL_Rect &w : walkRegions)
		addRect(w.x, w.y, w.x + w.w, w.y + w.h);
	// rasterise the (margin-grown) rects on an 8x8 base-px grid: the union area is exact and the rects stay disjoint
	// (merging overlapping rects into bounding boxes chained into one huge box for scattered tile changes)
	{
		const int cell = 8;
		const int gw = (mapW + cell - 1) / cell, gh = (mapH + cell - 1) / cell;
		std::vector<unsigned char> grid((size_t)gw * gh, 0);
		for (const SDL_Rect &r : rects)
		{
			const int cx0 = std::max(0, (r.x - P.mx0) / cell), cy0 = std::max(0, (r.y - P.my0) / cell);
			const int cx1 = std::min(gw, (r.x + r.w - P.mx0 + cell - 1) / cell), cy1 = std::min(gh, (r.y + r.h - P.my0 + cell - 1) / cell);
			for (int cy = cy0; cy < cy1; ++cy)
				for (int cx = cx0; cx < cx1; ++cx)
					grid[(size_t)cy * gw + cx] = 1;
		}
		rects.clear();
		// runs per row, merged downwards while the run repeats
		struct Run { int x0, x1, y0; };
		std::vector<Run> open;
		auto emit = [&](const Run &r, int y1)
		{
			const int x0 = P.mx0 + r.x0 * cell, y0 = P.my0 + r.y0 * cell;
			const int x1 = std::min(P.mx1, P.mx0 + r.x1 * cell), yy1 = std::min(P.my1, P.my0 + y1 * cell);
			rects.push_back(SDL_Rect{ (Sint16)x0, (Sint16)y0, (Uint16)(x1 - x0), (Uint16)(yy1 - y0) });
		};
		for (int cy = 0; cy <= gh; ++cy)
		{
			std::vector<Run> runs;
			if (cy < gh)
				for (int cx = 0; cx < gw;)
				{
					if (!grid[(size_t)cy * gw + cx]) { ++cx; continue; }
					int e = cx;
					while (e < gw && grid[(size_t)cy * gw + e]) ++e;
					runs.push_back(Run{ cx, e, cy });
					cx = e;
				}
			std::vector<Run> next;
			for (Run &r : runs)
			{
				bool cont = false;
				for (size_t i = 0; i < open.size(); ++i)
					if (open[i].x0 == r.x0 && open[i].x1 == r.x1 && open[i].y0 >= 0)
					{
						r.y0 = open[i].y0; open[i].y0 = -1 - open[i].y0; cont = true; break; // continued
					}
				(void)cont;
				next.push_back(r);
			}
			for (const Run &o : open)
				if (o.y0 < 0) continue; else emit(o, cy);
			open = next;
		}
	}
	// xBRZ: a partial update is not pixel-identical to a full recompose (verified with oxceHiResVerifyReuse: the result near
	// rect edges depends on the window), so any change recomposes the whole map; only "unchanged" is skipped
	if (mode == HiResResample::PIXEL && backend != 2 && (!rects.empty() || dx || dy))
	{
		P.reason = "xBRZ: partial update is not exact";
		P.dx = P.dy = P.DX = P.DY = 0;
		commit(); return;
	}
	long long area = 0;
	for (auto &r : rects) area += (long long)r.w * r.h;
	// cost rule: a partial update costs about its (exact) area plus a fixed share; a full one costs the map.
	// Walking frames (lighting / visibility change as the unit crosses tiles) tolerate more.
	const int limit = P.walking ? 92 : 75;
	if (rects.size() > 400 || area * 100 > (long long)mapW * mapH * limit)
	{
		P.reason = "most of the map changed";
		P.dx = P.dy = P.DX = P.DY = 0;
		commit(); return;
	}
	P.full = false;
	P.rects = rects;
	P.unchanged = rects.empty() && !dx && !dy;
	P.reason = P.unchanged ? "unchanged" : (dx || dy) ? "scroll" : "dirty";
	commit();
}


/// output-pixel rect of a base-px rect (same floor/ceil rule as the draw's clip rect)
inline void outRect(const SDL_Rect &r, double sx, double sy, int dstW, int dstH, int &x0, int &y0, int &x1, int &y1)
{
	x0 = std::max(0, (int)std::floor(r.x * sx)); y0 = std::max(0, (int)std::floor(r.y * sy));
	x1 = std::min(dstW, (int)std::ceil((r.x + r.w) * sx)); y1 = std::min(dstH, (int)std::ceil((r.y + r.h) * sy));
}

/// CPU map cache: A = nearest composite (no visibility cut), cls = scale class per pixel (SM.cls),
/// res = A interpolated; the visible part of res is copied into the frame buffer each frame.
struct MapBuf
{
	std::vector<Uint32> a, res;
	int w = 0, h = 0;
	std::vector<Uint32> zeroStamps;
} MB;

void shiftBuf(Uint32 *p, int w, int x0, int y0, int x1, int y1, int DX, int DY)
{
	// move the rect (x0..x1, y0..y1) of a w-wide buffer by (DX, DY) inside itself
	const int cw = (x1 - x0) - std::abs(DX), ch = (y1 - y0) - std::abs(DY);
	if (cw <= 0 || ch <= 0) return;
	const int sx0 = DX >= 0 ? x0 : x0 - DX, sy0 = DY >= 0 ? y0 : y0 - DY;
	const int dx0 = sx0 + DX, dy0 = sy0 + DY;
	if (DY > 0)
		for (int r = ch - 1; r >= 0; --r) std::memmove(&p[(size_t)(dy0 + r) * w + dx0], &p[(size_t)(sy0 + r) * w + sx0], (size_t)cw * 4);
	else
		for (int r = 0; r < ch; ++r) std::memmove(&p[(size_t)(dy0 + r) * w + dx0], &p[(size_t)(sy0 + r) * w + sx0], (size_t)cw * 4);
}
void shiftBuf8(Uint8 *p, int w, int x0, int y0, int x1, int y1, int DX, int DY)
{
	const int cw = (x1 - x0) - std::abs(DX), ch = (y1 - y0) - std::abs(DY);
	if (cw <= 0 || ch <= 0) return;
	const int sx0 = DX >= 0 ? x0 : x0 - DX, sy0 = DY >= 0 ? y0 : y0 - DY;
	const int dx0 = sx0 + DX, dy0 = sy0 + DY;
	if (DY > 0)
		for (int r = ch - 1; r >= 0; --r) std::memmove(&p[(size_t)(dy0 + r) * w + dx0], &p[(size_t)(sy0 + r) * w + sx0], (size_t)cw);
	else
		for (int r = 0; r < ch; ++r) std::memmove(&p[(size_t)(dy0 + r) * w + dx0], &p[(size_t)(sy0 + r) * w + sx0], (size_t)cw);
}

/// Composes the layered map (commands S.frame[groupStart..groupEnd)) per the plan into the cache and
/// copies its visible part into S.rgba. Returns the output rect of the map.
SDL_Rect composeMapCPU(size_t groupStart, size_t groupEnd, int dstW, int dstH, double sx, double sy, bool forceFull = false)
{
	const MapPlan &P = curPlan;
	const size_t n = (size_t)dstW * dstH;
	bool full = P.full || forceFull || MB.w != dstW || MB.h != dstH || MB.a.size() != n || SM.cls.size() != n;
	SDL_Rect mapBase = { (Sint16)P.mx0, (Sint16)P.my0, (Uint16)(P.mx1 - P.mx0), (Uint16)(P.my1 - P.my0) };
	int mx0, my0, mx1, my1;
	outRect(mapBase, sx, sy, dstW, dstH, mx0, my0, mx1, my1);
	std::vector<SDL_Rect> rects;
	if (full)
	{
		MB.w = dstW; MB.h = dstH;
		MB.a.assign(n, 0); MB.res.assign(n, 0);
		SM.cls.assign(n, 0);
		SM.classes.clear();
		scaleClass(1.0, 1.0, 0.0, 0.0);
		rects.push_back(mapBase);
		if (!forceFull) ++FS.full;
	}
	else
	{
		if (P.unchanged) ++FS.unchanged;
		else if (P.DX || P.DY) ++FS.scroll; else ++FS.dirty;
		if (P.DX || P.DY)
		{
			shiftBuf(MB.a.data(), dstW, mx0, my0, mx1, my1, P.DX, P.DY);
			shiftBuf(MB.res.data(), dstW, mx0, my0, mx1, my1, P.DX, P.DY);
			shiftBuf8(SM.cls.data(), dstW, mx0, my0, mx1, my1, P.DX, P.DY);
		}
		rects = P.rects;
	}
	const Uint32 mapStamp = S.frame[groupStart].stamp;
	const int mode = HiResResample::mode();
	if (MB.zeroStamps.size() != S.stamps.size())
		MB.zeroStamps.assign(S.stamps.size(), 0);
	for (const SDL_Rect &R : rects)
	{
		++FS.rectCount;
		int x0, y0, x1, y1;
		outRect(R, sx, sy, dstW, dstH, x0, y0, x1, y1);
		if (x0 >= x1 || y0 >= y1) continue;
		for (int y = y0; y < y1; ++y)
		{
			std::fill_n(&MB.a[(size_t)y * dstW + x0], x1 - x0, 0u);
			std::fill_n(&SM.cls[(size_t)y * dstW + x0], x1 - x0, (Uint8)0);
		}
		// the map's commands, nearest on their grids, clipped to the rect, no visibility cut (applied at the copy)
		S.rgba.swap(MB.a);
		S.stamps.swap(MB.zeroStamps);
		SM.inMap = true;
		SDL_Rect unused = {0, 0, 0, 0};
		for (size_t i = groupStart; i < groupEnd; ++i)
		{
			const HiResCmd &c = S.frame[i];
			if (c.kind != HiResCmdKind::Image || !c.mapLayer)
				continue;
			HiResCmd cc = c;
			int cx = cc.clipX, cy = cc.clipY, cw = cc.clipW, ch = cc.clipH;
			const int rx0 = std::max(cx, (int)R.x), ry0 = std::max(cy, (int)R.y);
			const int rx1 = std::min(cx + cw, R.x + R.w), ry1 = std::min(cy + ch, R.y + R.h);
			if (rx1 <= rx0 || ry1 <= ry0) continue;
			cc.clipX = (Sint16)rx0; cc.clipY = (Sint16)ry0; cc.clipW = (Sint16)(rx1 - rx0); cc.clipH = (Sint16)(ry1 - ry0);
			drawImage(cc, dstW, dstH, sx, sy, unused);
		}
		SM.inMap = false;
		S.stamps.swap(MB.zeroStamps);
		S.rgba.swap(MB.a);
	}
	// result = nearest composite, then the interpolation, per rect - after ALL rects are composed (neighbouring rects
	// are sources of each other's filter taps)
	for (const SDL_Rect &R : rects)
	{
		int x0, y0, x1, y1;
		outRect(R, sx, sy, dstW, dstH, x0, y0, x1, y1);
		if (x0 >= x1 || y0 >= y1) continue;
		for (int y = y0; y < y1; ++y)
			std::memcpy(&MB.res[(size_t)y * dstW + x0], &MB.a[(size_t)y * dstW + x0], (size_t)(x1 - x0) * 4);
		if (mode != HiResResample::NEAREST || Options::oxceHiResDebugScaleMask)
		{
			smSrc = MB.a.data(); smDst = MB.res.data();
			smMargin = (int)std::ceil(filterMargin(mode) * std::max(sx, sy));
			scaleMaskPass(SDL_Rect{ (Sint16)x0, (Sint16)y0, (Uint16)(x1 - x0), (Uint16)(y1 - y0) }, sx, sy);
			smSrc = smDst = nullptr;
		}
	}
	if (SM.classes.size() > 200)
		++cacheEpoch; // class table nearly full: next frame starts over
	if (!full && !forceFull && Options::oxceHiResVerifyReuse)
	{
		// debug: the same frame recomposed in full must give the same pixels
		const std::vector<Uint32> reused = MB.res;
		const SDL_Rect r = composeMapCPU(groupStart, groupEnd, dstW, dstH, sx, sy, true);
		long long bad = 0; int bx0 = dstW, by0 = dstH, bx1 = -1, by1 = -1;
		for (int y = my0; y < my1; ++y)
			for (int x = mx0; x < mx1; ++x)
				if (reused[(size_t)y * dstW + x] != MB.res[(size_t)y * dstW + x])
				{
					++bad; bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y);
				}
		Log(LOG_INFO) << "HiResLayer verify (CPU): " << P.reason << " dx " << P.dx << " dy " << P.dy << " rects " << P.rects.size()
			<< ": " << bad << " pixels differ" << (bad ? " in (" + std::to_string(bx0) + "," + std::to_string(by0) + ")-(" + std::to_string(bx1) + "," + std::to_string(by1) + ")" : std::string());
		return r;
	}
	// visible part of the result into the frame buffer
	const double isx = 1.0 / sx, isy = 1.0 / sy;
	std::vector<int> colBase(mx1 - mx0);
	for (int x = mx0; x < mx1; ++x) colBase[x - mx0] = std::min(S.baseW - 1, (int)(x * isx));
	for (int y = my0; y < my1; ++y)
	{
		const Uint32 *stampRow = &S.stamps[std::min(S.baseH - 1, (int)(y * isy)) * S.baseW];
		const Uint32 *src = &MB.res[(size_t)y * dstW];
		Uint32 *dst = &S.rgba[(size_t)y * S.bufW];
		for (int x = mx0; x < mx1; ++x)
		{
			if (stampRow[colBase[x - mx0]] > mapStamp) continue;
			const Uint32 p = src[x];
			const unsigned a = p >> 24;
			if (a == 255) dst[x] = p;
			else if (a) HiResResample::blendOver(dst[x], p);
		}
	}
	return SDL_Rect{ (Sint16)mx0, (Sint16)my0, (Uint16)(mx1 - mx0), (Uint16)(my1 - my0) };
}



/// Compose cache: the frame buffer of the last composition and the hash of everything it was made from
/// (all non-map commands with their stamps, the screen stamps, snapshot masks, pictures, mode). An identical
/// hash means the buffer is current: no layout, no drawing, no upload.
struct ComposeCache
{
	bool valid = false;
	Uint64 hash = 0;
	int w = 0, h = 0;
	bool skipped = false; ///< set by compose(): the last call reused the buffer
} CC;
Uint64 imageEpoch = 1;

inline Uint64 hashBytes(Uint64 h, const void *p, size_t n)
{
	const Uint8 *b = (const Uint8*)p;
	size_t i = 0;
	for (; i + 8 <= n; i += 8) { Uint64 v; std::memcpy(&v, b + i, 8); h = (h ^ v) * 1099511628211ull; h ^= h >> 29; }
	for (; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
	return h;
}

Uint64 frameHash(int dstW, int dstH, bool mapInBuffer)
{
	Uint64 h = 1469598103934665603ull;
	h = mix64(h, cacheEpoch); h = mix64(h, imageEpoch); h = mix64(h, (Uint64)dstW); h = mix64(h, (Uint64)dstH);
	h = mix64(h, mapInBuffer ? 1 : 2); h = mix64(h, (Uint64)HiResResample::mode());
	h = mix64(h, (Uint64)S.layout);
	for (const HiResCmd &c : S.frame)
	{
		if (c.mapLayer) continue;
		h = mix64(h, (Uint64)c.kind);
		h = mix64(h, (Uint64)(Uint16)c.x | ((Uint64)(Uint16)c.y << 16) | ((Uint64)(Uint16)c.w << 32) | ((Uint64)(Uint16)c.h << 48));
		h = mix64(h, (Uint64)(Uint16)c.clipX | ((Uint64)(Uint16)c.clipY << 16) | ((Uint64)(Uint16)c.clipW << 32) | ((Uint64)(Uint16)c.clipH << 48));
		h = mix64(h, c.code); h = mix64(h, c.run); h = mix64(h, c.lineId);
		h = mix64(h, (Uint64)(Uint16)c.boxW | ((Uint64)(Uint16)c.boxDy << 16) | ((Uint64)(Uint16)c.boxH << 32));
		h = mix64(h, (Uint64)c.cellH | ((Uint64)c.lineH << 8) | ((Uint64)c.refW << 16) | ((Uint64)c.fillIdx << 24) | ((Uint64)c.lineIdx << 32) | ((Uint64)c.slot << 40) | ((Uint64)c.flags << 48) | ((Uint64)c.scale << 56));
		h = mix64(h, (Uint64)(Uint8)c.inkTopL | ((Uint64)(Uint8)c.inkBotL << 8) | ((Uint64)(Uint8)c.inkTopK << 16) | ((Uint64)(Uint8)c.inkBotK << 24));
		h = mix64(h, c.imageId); h = mix64(h, c.assetId);
		h = mix64(h, (Uint64)(Uint16)c.srcX | ((Uint64)(Uint16)c.srcY << 16) | ((Uint64)(Uint16)c.srcW << 32) | ((Uint64)(Uint16)c.srcH << 48));
		h = mix64(h, c.palKey); h = mix64(h, c.maskIdx); h = mix64(h, c.underId);
		h = mix64(h, (Uint64)c.battle | ((Uint64)c.layered << 8) | ((Uint64)c.clearRect << 16) | ((Uint64)c.nearest << 24) | ((Uint64)c.shade << 32) | ((Uint64)c.nvColor << 40));
		h = mix64(h, c.spriteKey); h = mix64(h, c.battleId); h = mix64(h, c.tint); h = mix64(h, c.stamp);
		h = hashBytes(h, c.rcFrom, 4); h = hashBytes(h, c.rcTo, 4);
		h = mix64(h, (Uint64)c.fill.r | ((Uint64)c.fill.g << 8) | ((Uint64)c.fill.b << 16) | ((Uint64)c.line.r << 24) | ((Uint64)c.line.g << 32) | ((Uint64)c.line.b << 40));
	}
	h = mix64(h, S.frame.size());
	if (!S.stamps.empty()) h = hashBytes(h, S.stamps.data(), S.stamps.size() * 4);
	for (const auto &m : S.masks) { h = mix64(h, m.size()); if (!m.empty()) h = hashBytes(h, m.data(), m.size()); }
	for (const auto &b : S.backs) { h = mix64(h, b.size()); if (!b.empty()) h = hashBytes(h, b.data(), b.size() * 4); }
	return h;
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
		CC.valid = false;
	}
	// nothing it is made from changed: the buffer is current
	const bool mapCPU = !glMapActive && curPlan.active;
	const Uint64 fh = frameHash(dstW, dstH, mapCPU);
	if (CC.valid && CC.hash == fh && CC.w == dstW && CC.h == dstH && (!mapCPU || curPlan.unchanged) && !Options::oxceHiResForceFullRedraw)
	{
		CC.skipped = true;
		++skippedFrames;
		clearRect = SDL_Rect{0, 0, 0, 0};
		return S.prevDirty;
	}
	CC.skipped = false;
	CC.valid = true; CC.hash = fh; CC.w = dstW; CC.h = dstH;
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
		bool mapGroup = false;
		if (!glMapActive)
			for (size_t i = groupStart; i < groupEnd && !mapGroup; ++i)
				mapGroup = S.frame[i].mapLayer && S.frame[i].kind == HiResCmdKind::Image;
		if (mapGroup && curPlan.active)
		{
			const SDL_Rect md = composeMapCPU(groupStart, groupEnd, dstW, dstH, sx, sy);
			unionRect(dirty, md.x, md.y, md.w, md.h);
		}
		for (size_t i = groupStart; i < groupEnd; ++i)
			if (S.frame[i].kind == HiResCmdKind::Image && !S.frame[i].mapLayer)
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
				p.marker = false;
				if (c.flags & HiResCmd::GLYPH_MARKER)
				{
					// zero-width position (caret / IME): the pen at this point of the run
					p.marker = true;
					p.pen = (S.layout == 0 || c.run == 0) ? (int)std::lround(c.x * sx) : (int)(pen64 >> 6);
					continue;
				}
				if (!font)
					continue;
				int px, baseline; // px: font size in 1/4 px
				int outline64;
				placeVertical(c, font, sx, sy, px, baseline, outline64);
				const int bold64 = bold64ForSize(px, font);
				const bool space = (c.flags & HiResCmd::GLYPH_SPACE) != 0;
				const HiResGlyph &g = font->getGlyph(c.code, px, space ? 0 : outline64, space ? 0 : bold64, hs);
				if (!g.ok)
					continue;
				p.g = &g;
				p.baseline = baseline;
				p.adv = g.advance;
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
		// carets, composition underlines and the IME position (after the bodies, in the body colour)
		for (size_t i = groupStart; i < groupEnd; ++i)
		{
			const HiResCmd &c = S.frame[i];
			if (c.kind != HiResCmdKind::Glyph || !(c.flags & (HiResCmd::GLYPH_MARKER | HiResCmd::GLYPH_UNDERLINE)))
				continue;
			const Placed &p = placed[i - groupStart];
			const int cx0 = std::max(0, (int)std::floor(c.clipX * sx)), cy0 = std::max(0, (int)std::floor(c.clipY * sy));
			const int cx1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
			const int cy1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
			const double ms = sy * std::max<int>(1, c.scale);
			const int top = (int)std::lround(c.y * sy), bottom = (int)std::lround(c.y * sy + c.cellH * ms);
			if (p.marker && (c.flags & HiResCmd::GLYPH_CARET))
			{
				const int w = std::max(1, (int)std::lround(sx * 0.75));
				drawSolid(p.pen - w / 2, top, w, bottom - top, c.fill, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
			}
			if (p.marker && (c.flags & HiResCmd::GLYPH_IMEPOS))
			{
				caretFound = true;
				caretNX = p.pen / sx; caretNY = top / sy; caretNH = (bottom - top) / sy;
			}
			if (!p.marker && p.g && (c.flags & HiResCmd::GLYPH_UNDERLINE))
			{
				const int th = std::max(1, (int)std::lround(sy * 0.5));
				const int uy = std::min(bottom - th, p.baseline + std::max(1, (int)std::lround(sy * 0.75)));
				drawSolid(p.pen, uy, std::max(1, p.adv), th, c.fill, cx0, cy0, cx1, cy1, sx, sy, c.stamp, dirty);
			}
		}
		groupStart = groupEnd;
	}
	caretValid = caretFound;
	if (caretFound) { caretX = caretNX; caretY = caretNY; caretH = caretNH; }
	caretFound = false;
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
	++cacheEpoch;
	const bool wasRecording = _recording;
	S.sizePct = std::max(10, Options::oxceHiResFontSize);
	S.outlinePct = std::min(400, std::max(0, Options::oxceHiResTextOutlineScale));
	S.boldPct = std::max(0, Options::oxceHiResTextBold);
	S.layout = Options::oxceHiResTextLayout;
	S.settings.hinting = Options::oxceHiResTextHinting;
	S.settings.antialias = Options::oxceHiResTextAntialias;
	S.slots.clear();
	extraBold.clear();
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
	_recording = (_textConfigured || uiImagesWanted() || assetsWanted()) && S.outputSupported;
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
	return (Options::oxceHiResOverlay && Options::oxceHiResText) || uiImagesWanted() || assetsWanted();
}

void HiResLayer::shutdown()
{
	++cacheEpoch;
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
	++cacheEpoch;
	S.screen = screenBuffer;
	S.baseW = screenBuffer ? screenBuffer->w : 0;
	S.baseH = screenBuffer ? screenBuffer->h : 0;
	S.outputSupported = supportedOutput;
	bool was = _recording;
	_recording = (_textConfigured || uiImagesWanted() || assetsWanted()) && S.outputSupported;
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
	S.masks.clear();
	S.backs.clear();
	S.sizeCache.clear();
	slotSizeCache.clear();
}

#ifndef __NO_OPENGL
namespace { void glMapReset(); }
#endif
void HiResLayer::onContextLost()
{
	++cacheEpoch;
#ifndef __NO_OPENGL
	glMapReset();
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

void HiResLayer::record(SDL_Surface *holder, const HiResCmd &cmd, bool front)
{
	if (!_recording || !holder)
		return;
	auto &l = S.lists[holder];
	if (front)
		l.insert(l.begin(), cmd);
	else
		l.push_back(cmd);
}

void HiResLayer::recordAsset(SDL_Surface *dst, const HiResCmd &cmd, const SDL_Surface *origin)
{
	ProfScope ps(prof.rec);
	if (!_recording || !dst)
		return;
	HiResCmd c = cmd;
	c.origin = origin;
	c.originX = c.x;
	c.originY = c.y;
	int cx = c.clipX, cy = c.clipY, cw = c.clipW, ch = c.clipH;
	if (!intersect(cx, cy, cw, ch, 0, 0, dst->w, dst->h))
		return;
	c.clipX = (Sint16)cx; c.clipY = (Sint16)cy; c.clipW = (Sint16)cw; c.clipH = (Sint16)ch;
	c.underId = c.battle ? 0 : snapshotUnder(c, dst);
	if (dst == S.screen)
	{
		// drawn straight onto the screen buffer: check it now, against the screen pixels
		c.stamp = ++S.stampCounter;
		if (!deliverAsset(c, dst, 0, 0))
			return;
		const std::vector<Uint8> &m = S.masks[c.maskIdx - 1];
		for (int yy = 0; yy < c.clipH; ++yy)
			for (int xx = 0; xx < c.clipW; ++xx)
				if (m[(size_t)yy * c.clipW + xx] == MASK_PROXY || m[(size_t)yy * c.clipW + xx] == MASK_BACK)
					S.stamps[(c.clipY + yy) * S.baseW + c.clipX + xx] = c.stamp;
		S.frame.push_back(c);
		return;
	}
	auto &dl = S.lists[dst];
	dl.erase(std::remove_if(dl.begin(), dl.end(), [&](const HiResCmd &o)
	{
		return o.assetId == c.assetId && o.origin == origin && o.originX == c.originX && o.originY == c.originY
			&& (!c.battle || (o.clipX == c.clipX && o.clipY == c.clipY && o.clipW == c.clipW && o.clipH == c.clipH));
	}), dl.end());
	dl.push_back(c);
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
	if (holder && holder == layerSurf)
		layerSurf = nullptr; // the layered map surface is gone
	if (holder)
		S.lists.erase(holder);
}

void HiResLayer::onBlit(SDL_Surface *src, SDL_Surface *dst, int x, int y)
{
	ProfScope ps(prof.del);
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
				if (!intersect(cx, cy, cw, ch, c.x, c.y, std::max<int>(1, c.w), c.h)) // markers: zero width
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
			c.mapLayer = (src == layerSurf) ? 1 : 0;
			if (c.assetId && !deliverAsset(c, src, x, y))
				continue; // proxy pixels did not reach the screen unchanged
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
	if (toScreen && src == layerSurf && layerVal.size() == (size_t)src->w * src->h && pal)
	{
		// layered map: pixels changed by unrecorded 8bpp draws (arrows, indicators, waypoints, projectiles,
		// messages...) go on top of the map's commands, magnified as they are
		std::vector<Uint32> ov((size_t)src->w * src->h, 0);
		bool any = false;
		SDL_LockSurface(src);
		for (int yy = 0; yy < src->h; ++yy)
		{
			const Uint8 *sp = (const Uint8*)src->pixels + yy * src->pitch;
			const Uint16 *lv = &layerVal[(size_t)yy * src->w];
			for (int xx = 0; xx < src->w; ++xx)
			{
				const Uint8 v = sp[xx];
				if (lv[xx] ? (lv[xx] == v + 1) : (v == layerBg || v == 0))
					continue;
				const SDL_Color &col = pal->colors[v];
				ov[(size_t)yy * src->w + xx] = 0xFF000000u | ((Uint32)col.r << 16) | ((Uint32)col.g << 8) | col.b;
				any = true;
			}
		}
		SDL_UnlockSurface(src);
		if (any)
		{
			HiResCmd c;
			c.kind = HiResCmdKind::Image;
			c.spriteKey = storeSprite(std::move(ov), src->w, src->h);
			c.nearest = 1;
			c.x = (Sint16)x; c.y = (Sint16)y; c.w = (Sint16)src->w; c.h = (Sint16)src->h;
			int cx = x, cy = y, cw = src->w, ch = src->h;
			if (intersect(cx, cy, cw, ch, 0, 0, dst->w, dst->h))
			{
				c.clipX = (Sint16)cx; c.clipY = (Sint16)cy; c.clipW = (Sint16)cw; c.clipH = (Sint16)ch;
				c.stamp = stamp;
				c.mapLayer = 1;
				dl->push_back(c);
			}
		}
	}
}

void HiResLayer::noteWalkRegion(int x, int y, int w, int h)
{
	if (layerSurf && w > 0 && h > 0)
		walkRegions.push_back(SDL_Rect{ (Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h });
}

bool HiResLayer::layered()
{
	// hi-res images off: the battlescape is the stock 8-bit blit (+ stock screen filter); the layer
	// only carries text/UI then
	return _recording && Options::oxceHiResBattleLayered && HiResAssets::battleEnabled();
}

void HiResLayer::layerBegin(SDL_Surface *map, Uint8 bg)
{
	if (!map || !layered())
	{
		layerSurf = nullptr;
		return;
	}
	if (sprites.size() > 20000)
	{
		// bounded store (commands of the previous frame are gone: the map list was just cleared)
		sprites.clear();
		spriteByHash.clear();
		++cacheEpoch;
	}
	layerSurf = map;
	layerBg = bg;
	walkRegions.clear();
	layerVal.assign((size_t)map->w * map->h, 0);
	HiResCmd c;
	c.kind = HiResCmdKind::Image;
	c.clearRect = 1;
	c.fillIdx = bg;
	c.x = 0; c.y = 0; c.w = (Sint16)map->w; c.h = (Sint16)map->h;
	c.clipX = 0; c.clipY = 0; c.clipW = c.w; c.clipH = c.h;
	record(map, c);
}

namespace
{
/// visible rect of a sprite blit (half = right half only, clip = optional extra clip), map coordinates
bool layerRect(SDL_Surface *dst, int w, int h, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH, int &x0, int &y0, int &x1, int &y1)
{
	x0 = dx + (half ? w / 2 : 0); y0 = dy; x1 = dx + w; y1 = dy + h;
	if (clipW > 0 && clipH > 0)
	{
		x0 = std::max(x0, clipX); y0 = std::max(y0, clipY);
		x1 = std::min(x1, clipX + clipW); y1 = std::min(y1, clipY + clipH);
	}
	x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, dst->w); y1 = std::min(y1, dst->h);
	return x0 < x1 && y0 < y1;
}
}

void HiResLayer::layerMark(SDL_Surface *dst, const Uint8 *src, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH)
{
	if (!dst || dst != layerSurf || !src || layerVal.size() != (size_t)dst->w * dst->h)
		return;
	int x0, y0, x1, y1;
	if (!layerRect(dst, w, h, dx, dy, half, clipX, clipY, clipW, clipH, x0, y0, x1, y1))
		return;
	for (int y = y0; y < y1; ++y)
	{
		const Uint8 *sp = src + (size_t)(y - dy) * pitch - dx;
		const Uint8 *dp = (const Uint8*)dst->pixels + y * dst->pitch;
		Uint16 *lv = &layerVal[(size_t)y * dst->w];
		for (int x = x0; x < x1; ++x)
			if (sp[x])
				lv[x] = (Uint16)(dp[x] + 1);
	}
}

void HiResLayer::layerSprite8(SDL_Surface *dst, const Uint8 *src, int w, int h, int pitch, int dx, int dy, bool half, int clipX, int clipY, int clipW, int clipH)
{
	if (!dst || dst != layerSurf || !src || !dst->format->palette)
		return;
	int x0, y0, x1, y1;
	if (!layerRect(dst, w, h, dx, dy, half, clipX, clipY, clipW, clipH, x0, y0, x1, y1))
		return;
	layerMark(dst, src, w, h, pitch, dx, dy, half, clipX, clipY, clipW, clipH);
	// the sprite as it landed on the map (palette colours of the written pixels), base px, its own box
	const SDL_Color *pal = dst->format->palette->colors;
	std::vector<Uint32> px((size_t)w * h, 0);
	bool any = false;
	for (int y = y0; y < y1; ++y)
	{
		const Uint8 *sp = src + (size_t)(y - dy) * pitch - dx;
		const Uint8 *dp = (const Uint8*)dst->pixels + y * dst->pitch;
		Uint32 *op = &px[(size_t)(y - dy) * w - dx];
		for (int x = x0; x < x1; ++x)
			if (sp[x])
			{
				const SDL_Color &col = pal[dp[x]];
				op[x] = 0xFF000000u | ((Uint32)col.r << 16) | ((Uint32)col.g << 8) | col.b;
				any = true;
			}
	}
	if (!any)
		return;
	HiResCmd c;
	c.kind = HiResCmdKind::Image;
	c.battle = 1;
	c.layered = 1;
	c.spriteKey = storeSprite(std::move(px), w, h);
	c.x = (Sint16)dx; c.y = (Sint16)dy; c.w = (Sint16)w; c.h = (Sint16)h;
	c.clipX = (Sint16)x0; c.clipY = (Sint16)y0; c.clipW = (Sint16)(x1 - x0); c.clipH = (Sint16)(y1 - y0);
	record(dst, c);
}

void HiResLayer::onCopy(SDL_Surface *src, SDL_Surface *dst, int fromX, int fromY)
{
	if (!_recording || !src || !dst || src == dst || dst == S.screen)
		return;
	std::vector<HiResCmd> out;
	auto it = S.lists.find(src);
	if (it != S.lists.end())
	{
		for (const HiResCmd &c0 : it->second)
		{
			if (c0.kind != HiResCmdKind::Image || (!c0.assetId && !c0.imageId))
				continue; // text is not copied (the copy is a background for effects)
			HiResCmd c = c0;
			c.x -= fromX; c.y -= fromY; c.clipX -= fromX; c.clipY -= fromY;
			int cx = c.clipX, cy = c.clipY, cw = c.clipW, ch = c.clipH;
			if (!intersect(cx, cy, cw, ch, 0, 0, dst->w, dst->h))
				continue;
			c.clipX = (Sint16)cx; c.clipY = (Sint16)cy; c.clipW = (Sint16)cw; c.clipH = (Sint16)ch;
			c.origin = src;
			c.originX = (Sint16)-fromX;
			c.originY = (Sint16)-fromY;
			c.tint = 0;
			out.push_back(c);
		}
	}
	S.lists[dst] = std::move(out);
}

void HiResLayer::tintSurface(SDL_Surface *holder, Uint32 tint)
{
	if (!_recording || !holder)
		return;
	auto it = S.lists.find(holder);
	if (it == S.lists.end())
		return;
	for (HiResCmd &c : it->second)
		if (c.kind == HiResCmdKind::Image)
			c.tint = tint;
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
	++imageEpoch;
	images[id] = ImageRef{ argb, w, h };
}

void HiResLayer::dropImage(Uint32 id)
{
	++imageEpoch;
	images.erase(id);
}

bool HiResLayer::outputScale(double &sx, double &sy)
{
	if (S.bufW <= 0 || S.bufH <= 0 || S.baseW <= 0 || S.baseH <= 0)
		return false;
	sx = S.bufW / (double)S.baseW;
	sy = S.bufH / (double)S.baseH;
	return true;
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
	S.masks.clear();
	S.backs.clear();
	S.stampCounter = 0;
	std::fill(S.stamps.begin(), S.stamps.end(), 0);
}

bool HiResLayer::caretPosition(double &x, double &y, double &h)
{
	if (!caretValid)
		return false;
	x = caretX; y = caretY; h = caretH;
	return true;
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
	const double tf0 = profNow();
	if (S.baseW > 0)
	{
		glMapActive = false;
		planMapFrame(dstW, dstH, 1);
		logMapPath(false, curPlan.active ? "software output (no OpenGL)" : nullptr);
	}
	SDL_Rect dirty = compose(dstW, dstH, cleared);
	if (dirty.w <= 0 || dirty.h <= 0)
	{
		frameDone(profNow() - tf0);
		return;
	}
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
	frameDone(profNow() - tf0);
}

#ifndef __NO_OPENGL
namespace
{
/// GL map pass (layered battlescape): every map command as a textured quad into an FBO at output
/// resolution (shade = vertex colour, recolour/night vision baked into the texture variant), then the
/// FBO times the map's visibility mask (screen blits over the map) over the screen. CPU path = fallback.
typedef void (APIENTRY *HR_GenFB)(GLsizei, GLuint*);
typedef void (APIENTRY *HR_BindFB)(GLenum, GLuint);
typedef void (APIENTRY *HR_FBTex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *HR_FBStatus)(GLenum);
struct GlMap
{
	bool tried = false, ok = false;
	HR_GenFB genFB = nullptr, delFB = nullptr;
	HR_BindFB bindFB = nullptr;
	HR_FBTex fbTex = nullptr;
	HR_FBStatus fbStatus = nullptr;
	GLuint fbo = 0, fboTex = 0, maskTex = 0;
	int fboW = 0, fboH = 0, maskW = 0, maskH = 0;
	struct Tex { GLuint id = 0; Uint64 used = 0; size_t bytes = 0; };
	std::unordered_map<size_t, Tex> texs;
	size_t texBytes = 0;
	Uint64 use = 0;
	std::vector<Uint32> scratch;
	std::vector<Uint8> maskPx;
	// scale-mask interpolation on the GPU (GLSL + MRT): A = colour + class (fboTex, clsTex),
	// S = one class on its source grid, B = result (outTex)
	bool smTried = false, smOk = false;
	GLuint clsTex = 0, fboB = 0, outTex = 0, fboS = 0, sTex = 0;
	int bW = 0, bH = 0, sW = 0, sH = 0;
	GLuint progDraw = 0, progShrink = 0, progUp = 0;
	bool xbrzTried = false; GLuint progXbrz = 0; std::string xbrzWhy; ///< GLSL xBRZ upscale program (GL >= 3.0)
	// map cache (battle-4): spare targets for the scroll shift, the visibility-cut result C
	GLuint fboT = 0, spA = 0, spCls = 0, spOut = 0, fboC = 0, cTex = 0;
	int spW = 0, spH = 0, cW = 0, cH = 0;
	bool cValid = false;
	Uint64 cutHash = 0;
	double stage[4] = {0, 0, 0, 0};
	int stageFrames = 0;
};
GlMap G;
GLuint glMapResult = 0; ///< texture holding the interpolated map (before the visibility cut) after glMapPass

typedef GLuint (APIENTRY *HR_CreateShader)(GLenum);
typedef void (APIENTRY *HR_ShaderSource)(GLuint, GLsizei, const char *const*, const GLint*);
typedef void (APIENTRY *HR_UInt)(GLuint);
typedef void (APIENTRY *HR_GetIv)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *HR_GetLog)(GLuint, GLsizei, GLsizei*, char*);
typedef GLuint (APIENTRY *HR_CreateProgram)();
typedef void (APIENTRY *HR_Attach)(GLuint, GLuint);
typedef GLint (APIENTRY *HR_UniLoc)(GLuint, const char*);
typedef void (APIENTRY *HR_Uni1i)(GLint, GLint);
typedef void (APIENTRY *HR_Uni1f)(GLint, GLfloat);
typedef void (APIENTRY *HR_Uni2f)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *HR_DrawBuffers)(GLsizei, const GLenum*);
typedef void (APIENTRY *HR_ActiveTex)(GLenum);
struct GlFn
{
	HR_CreateShader createShader = nullptr; HR_ShaderSource shaderSource = nullptr; HR_UInt compileShader = nullptr;
	HR_GetIv getShaderiv = nullptr, getProgramiv = nullptr; HR_GetLog getShaderLog = nullptr, getProgramLog = nullptr;
	HR_CreateProgram createProgram = nullptr; HR_Attach attachShader = nullptr; HR_UInt linkProgram = nullptr, useProgram = nullptr;
	HR_UniLoc uniLoc = nullptr; HR_Uni1i uni1i = nullptr; HR_Uni1f uni1f = nullptr; HR_Uni2f uni2f = nullptr;
	HR_DrawBuffers drawBuffers = nullptr; HR_ActiveTex activeTex = nullptr;
} GF;
#define HR_TEX0 0x84C0
#ifndef HR_COLOR_ATTACHMENT1
#define HR_COLOR_ATTACHMENT1 0x8CE1
#endif

void *glProc(const char *a, const char *b)
{
	void *p = SDL_GL_GetProcAddress(a);
	return p ? p : SDL_GL_GetProcAddress(b);
}

bool glMapInit()
{
	if (G.tried)
		return G.ok;
	G.tried = true;
	G.genFB = (HR_GenFB)glProc("glGenFramebuffers", "glGenFramebuffersEXT");
	G.delFB = (HR_GenFB)glProc("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
	G.bindFB = (HR_BindFB)glProc("glBindFramebuffer", "glBindFramebufferEXT");
	G.fbTex = (HR_FBTex)glProc("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
	G.fbStatus = (HR_FBStatus)glProc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
	G.ok = G.genFB && G.delFB && G.bindFB && G.fbTex && G.fbStatus;
	Log(LOG_INFO) << "HiResLayer: GL map pass " << (G.ok ? "available (framebuffer objects)" : "unavailable (no framebuffer objects): CPU compositing");
	return G.ok;
}

void glMapReset()
{
	// context lost: every GL name is gone
	G.texs.clear(); G.texBytes = 0;
	G.fbo = G.fboTex = G.maskTex = 0;
	G.fboW = G.fboH = G.maskW = G.maskH = 0;
	G.tried = false; G.ok = false;
	G.smTried = false; G.smOk = false;
	G.clsTex = G.fboB = G.outTex = G.fboS = G.sTex = 0;
	G.bW = G.bH = G.sW = G.sH = 0;
	G.progDraw = G.progShrink = G.progUp = 0;
	G.xbrzTried = false; G.progXbrz = 0; G.xbrzWhy.clear();
	G.fboT = G.spA = G.spCls = G.spOut = G.fboC = G.cTex = 0;
	G.spW = G.spH = G.cW = G.cH = 0; G.cValid = false;
	++cacheEpoch;
}

GLuint makeTex(int w, int h, const void *bgra);

const char *const SM_VS =
	"void main() { gl_Position = ftransform(); gl_TexCoord[0] = gl_MultiTexCoord0; gl_FrontColor = gl_Color; }\n";
/// draw: premultiplied texel (shade = vertex colour) or flat colour; class id into the 2nd target
const char *const SM_FS_DRAW =
	"uniform sampler2D tex; uniform float useTex; uniform float cls;\n"
	"void main() {\n"
	"  vec4 c = useTex > 0.5 ? texture2D(tex, gl_TexCoord[0].xy) * vec4(gl_Color.rgb, 1.0) : gl_Color;\n"
	"  if (c.a <= 0.0) discard;\n"
	"  gl_FragData[0] = c; gl_FragData[1] = vec4(cls, 0.0, 0.0, 1.0);\n"
	"}\n";
/// shrink: one sample per source cell (a pixel of the cell's block still showing this class)
const char *const SM_FS_SHRINK =
	"uniform sampler2D colTex; uniform sampler2D clsTex; uniform vec2 outSize; uniform vec2 kps; uniform vec2 ph; uniform float cls;\n"
	"vec4 pick(vec2 o) {\n"
	"  if (o.x < 0.0 || o.y < 0.0 || o.x >= outSize.x || o.y >= outSize.y) return vec4(0.0);\n"
	"  vec2 t = vec2((o.x + 0.5) / outSize.x, 1.0 - (o.y + 0.5) / outSize.y);\n"
	"  if (abs(texture2D(clsTex, t).r - cls) > 0.5 / 255.0) return vec4(0.0);\n"
	"  return vec4(texture2D(colTex, t).rgb, 1.0);\n"
	"}\n"
	"void main() {\n"
	"  vec2 g = floor(gl_FragCoord.xy) - 1.0;\n"
	"  vec2 oc = floor((g + 0.5 + ph) / kps);\n"
	"  vec2 o0 = ceil((g + ph) / kps - 0.0001);\n"
	"  vec2 o1 = ceil((g + 1.0 + ph) / kps - 0.0001) - 1.0;\n"
	"  vec4 r = pick(oc);\n"
	"  if (r.a == 0.0) r = pick(o0);\n"
	"  if (r.a == 0.0) r = pick(o1);\n"
	"  if (r.a == 0.0) r = pick(vec2(o0.x, o1.y));\n"
	"  if (r.a == 0.0) r = pick(vec2(o1.x, o0.y));\n"
	"  gl_FragColor = r;\n"
	"}\n";
/// upscale: alpha-weighted bilinear from the source grid, only on this class's pixels
const char *const SM_FS_UP =
	"uniform sampler2D sTex; uniform sampler2D clsTex; uniform sampler2D colTex; uniform vec2 outSize; uniform vec2 sSize; uniform vec2 kps; uniform vec2 ph; uniform float cls; uniform float debug;\n"
	"vec4 sm(vec2 i) { i = clamp(i, vec2(0.0), sSize - 1.0); return texture2D(sTex, (i + 0.5) / sSize); }\n"
	"void main() {\n"
	"  vec2 o = vec2(floor(gl_FragCoord.x), outSize.y - 1.0 - floor(gl_FragCoord.y));\n"
	"  vec2 t = vec2((o.x + 0.5) / outSize.x, 1.0 - (o.y + 0.5) / outSize.y);\n"
	"  if (abs(texture2D(clsTex, t).r - cls) > 0.5 / 255.0) discard;\n"
	"  if (debug > 0.5) {\n"
	"    float k = floor(cls * 255.0 + 0.5) - 1.0; float m = mod(k, 6.0);\n"
	"    vec3 tint = m < 0.5 ? vec3(1,0,0) : m < 1.5 ? vec3(0,1,0) : m < 2.5 ? vec3(0,0,1) : m < 3.5 ? vec3(1,1,0) : m < 4.5 ? vec3(0,1,1) : vec3(1,0,1);\n"
	"    vec4 p = texture2D(colTex, t); gl_FragColor = vec4(mix(p.rgb, tint, 0.375), p.a); return;\n"
	"  }\n"
	"  vec2 c = (o + 0.5) * kps - ph - 0.5;\n"
	"  vec2 i0 = floor(c); vec2 f = c - i0; i0 += 1.0;\n"
	"  vec4 a = sm(i0), b = sm(i0 + vec2(1.0, 0.0)), d = sm(i0 + vec2(0.0, 1.0)), e = sm(i0 + vec2(1.0, 1.0));\n"
	"  float wa = (1.0 - f.x) * (1.0 - f.y) * a.a, wb = f.x * (1.0 - f.y) * b.a, wd = (1.0 - f.x) * f.y * d.a, we = f.x * f.y * e.a;\n"
	"  float w = wa + wb + wd + we;\n"
	"  if (w <= 0.0) discard;\n"
	"  gl_FragColor = vec4((a.rgb * wa + b.rgb * wb + d.rgb * wd + e.rgb * we) / w, texture2D(colTex, t).a);\n"
	"}\n";

const char *const SM_VS130 =
	"#version 130\n"
	"void main() { gl_Position = ftransform(); gl_TexCoord[0] = gl_MultiTexCoord0; gl_FrontColor = gl_Color; }\n";

GLuint smProgram(const char *fs, const char *vs = SM_VS)
{
	auto compile = [](GLenum type, const char *src) -> GLuint
	{
		GLuint sh = GF.createShader(type);
		GF.shaderSource(sh, 1, &src, nullptr);
		GF.compileShader(sh);
		GLint ok = 0;
		GF.getShaderiv(sh, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
		if (!ok)
		{
			char log[1024] = {0};
			GF.getShaderLog(sh, sizeof(log) - 1, nullptr, log);
			Log(LOG_WARNING) << "HiResLayer: GL scale-mask shader: " << log;
			return 0;
		}
		return sh;
	};
	const GLuint v = compile(0x8B31 /* GL_VERTEX_SHADER */, vs), f = compile(0x8B30 /* GL_FRAGMENT_SHADER */, fs);
	if (!v || !f)
		return 0;
	const GLuint p = GF.createProgram();
	GF.attachShader(p, v);
	GF.attachShader(p, f);
	GF.linkProgram(p);
	GLint ok = 0;
	GF.getProgramiv(p, 0x8B82 /* GL_LINK_STATUS */, &ok);
	if (!ok)
	{
		char log[1024] = {0};
		GF.getProgramLog(p, sizeof(log) - 1, nullptr, log);
		Log(LOG_WARNING) << "HiResLayer: GL scale-mask program: " << log;
		return 0;
	}
	return p;
}

/// GLSL xBRZ upscale program: needs OpenGL >= 3.0 (GLSL 1.30) and a clean compile + link; the answer (and the
/// reason when it is no) is kept per context. Needs glScaleMaskInit() first.
bool glXbrzInit(std::string &why)
{
	if (!G.xbrzTried)
	{
		G.xbrzTried = true;
		const char *ver = (const char *)glGetString(GL_VERSION);
		int major = 0, minor = 0;
		if (ver && std::sscanf(ver, "%d.%d", &major, &minor) < 2) major = minor = 0;
		if (major < 3)
			G.xbrzWhy = std::string("OpenGL ") + (ver ? ver : "unknown") + " is older than 3.0";
		else
		{
			G.progXbrz = smProgram(SM_FS_UP_XBRZ, SM_VS130);
			if (!G.progXbrz)
				G.xbrzWhy = "the xBRZ shader does not compile / link on this driver (OpenGL " + std::string(ver) + ")";
			if (G.progXbrz) GF.useProgram(0);
		}
		Log(LOG_INFO) << "HiResLayer: GPU xBRZ " << (G.progXbrz ? "available (GLSL 1.30, OpenGL " + std::string(ver ? ver : "?") + ")" : "unavailable: " + G.xbrzWhy);
	}
	why = G.xbrzWhy;
	return G.progXbrz != 0;
}

/// GLSL + glDrawBuffers available and the three programs built (once per context)
bool glScaleMaskInit()
{
	if (G.smTried)
		return G.smOk;
	G.smTried = true;
	GF.createShader = (HR_CreateShader)glProc("glCreateShader", "glCreateShaderObjectARB");
	GF.shaderSource = (HR_ShaderSource)glProc("glShaderSource", "glShaderSourceARB");
	GF.compileShader = (HR_UInt)glProc("glCompileShader", "glCompileShaderARB");
	GF.getShaderiv = (HR_GetIv)glProc("glGetShaderiv", "glGetShaderiv");
	GF.getProgramiv = (HR_GetIv)glProc("glGetProgramiv", "glGetProgramiv");
	GF.getShaderLog = (HR_GetLog)glProc("glGetShaderInfoLog", "glGetShaderInfoLog");
	GF.getProgramLog = (HR_GetLog)glProc("glGetProgramInfoLog", "glGetProgramInfoLog");
	GF.createProgram = (HR_CreateProgram)glProc("glCreateProgram", "glCreateProgramObjectARB");
	GF.attachShader = (HR_Attach)glProc("glAttachShader", "glAttachObjectARB");
	GF.linkProgram = (HR_UInt)glProc("glLinkProgram", "glLinkProgramARB");
	GF.useProgram = (HR_UInt)glProc("glUseProgram", "glUseProgramObjectARB");
	GF.uniLoc = (HR_UniLoc)glProc("glGetUniformLocation", "glGetUniformLocationARB");
	GF.uni1i = (HR_Uni1i)glProc("glUniform1i", "glUniform1iARB");
	GF.uni1f = (HR_Uni1f)glProc("glUniform1f", "glUniform1fARB");
	GF.uni2f = (HR_Uni2f)glProc("glUniform2f", "glUniform2fARB");
	GF.drawBuffers = (HR_DrawBuffers)glProc("glDrawBuffers", "glDrawBuffersARB");
	GF.activeTex = (HR_ActiveTex)glProc("glActiveTexture", "glActiveTextureARB");
	const bool fn = GF.createShader && GF.shaderSource && GF.compileShader && GF.getShaderiv && GF.getProgramiv && GF.getShaderLog
		&& GF.getProgramLog && GF.createProgram && GF.attachShader && GF.linkProgram && GF.useProgram && GF.uniLoc && GF.uni1i
		&& GF.uni1f && GF.uni2f && GF.drawBuffers && GF.activeTex;
	if (fn)
	{
		G.progDraw = smProgram(SM_FS_DRAW);
		G.progShrink = smProgram(SM_FS_SHRINK);
		G.progUp = smProgram(SM_FS_UP);
	}
	GLint maxDb = 0;
	if (fn) glGetIntegerv(0x8824 /* GL_MAX_DRAW_BUFFERS */, &maxDb);
	G.smOk = fn && G.progDraw && G.progShrink && G.progUp && maxDb >= 2;
	if (fn) GF.useProgram(0);
	Log(LOG_INFO) << "HiResLayer: GL scale-mask " << (G.smOk ? "available (GLSL + 2 draw buffers)" : "unavailable: CPU scale-mask");
	return G.smOk;
}

void quadF(double x0, double y0, double x1, double y1)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2d(x0, y0);
	glTexCoord2f(1, 0); glVertex2d(x1, y0);
	glTexCoord2f(1, 1); glVertex2d(x1, y1);
	glTexCoord2f(0, 1); glVertex2d(x0, y1);
	glEnd();
}

/// texture 'tex' (size w x h) attached to framebuffer 'fb' (created when 0); false = incomplete
bool smTarget(GLuint &fb, GLuint &tex, int w, int h)
{
	if (!fb) G.genFB(1, &fb);
	if (tex) glDeleteTextures(1, &tex);
	tex = makeTex(w, h, nullptr);
	G.bindFB(GL_FRAMEBUFFER_EXT, fb);
	G.fbTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
	return G.fbStatus(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;
}

GLuint makeTex(int w, int h, const void *bgra)
{
	GLuint t = 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
	return t;
}

/// cached texture of an output-size picture; 'make' fills the pixels when it is not cached
template <class F>
GLuint cachedTex(size_t key, int w, int h, F make)
{
	auto it = G.texs.find(key);
	if (it != G.texs.end())
	{
		it->second.used = ++G.use;
		return it->second.id;
	}
	if (G.texBytes > ((size_t)256 << 20) || G.texs.size() > 6000)
	{
		std::vector<std::pair<Uint64, size_t>> order;
		for (auto &t : G.texs) order.emplace_back(t.second.used, t.first);
		std::sort(order.begin(), order.end());
		for (size_t i = 0; i < order.size() / 2; ++i)
		{
			auto e = G.texs.find(order[i].second);
			glDeleteTextures(1, &e->second.id);
			G.texBytes -= e->second.bytes;
			G.texs.erase(e);
		}
	}
	const Uint32 *px = make();
	if (!px)
		return 0;
	GlMap::Tex t;
	t.id = makeTex(w, h, px);
	t.used = ++G.use;
	t.bytes = (size_t)w * h * 4;
	G.texBytes += t.bytes;
	G.texs[key] = t;
	return t.id;
}

void quad(int x0, int y0, int x1, int y1, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1)
{
	glBegin(GL_QUADS);
	glTexCoord2f(s0, t0); glVertex2i(x0, y0);
	glTexCoord2f(s1, t0); glVertex2i(x1, y0);
	glTexCoord2f(s1, t1); glVertex2i(x1, y1);
	glTexCoord2f(s0, t1); glVertex2i(x0, y1);
	glEnd();
}

/// Draws the map commands of this frame (or only what changed, per the plan). False = not drawn (the CPU path must compose them).
bool glMapPass(int dstW, int dstH, int outW, int outH, int top, int bottom, int left, int right, bool forceFull = false)
{
	size_t first = S.frame.size();
	for (size_t i = 0; i < S.frame.size(); ++i)
		if (S.frame[i].mapLayer && S.frame[i].kind == HiResCmdKind::Image) { first = i; break; }
	if (first == S.frame.size())
		return true; // nothing on the map this frame
	const Uint32 mapStamp = S.frame[first].stamp;
	const double sx = dstW / (double)S.baseW, sy = dstH / (double)S.baseH;
	const MapPlan P = curPlan;
	bool full = P.full || forceFull;

	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, dstW, dstH, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
	glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
	auto popState = [&]()
	{
		G.bindFB(GL_FRAMEBUFFER_EXT, 0);
		glMatrixMode(GL_TEXTURE); glPopMatrix();
		glMatrixMode(GL_MODELVIEW); glPopMatrix();
		glMatrixMode(GL_PROJECTION); glPopMatrix();
		glPopAttrib();
		glViewport(vp[0], vp[1], vp[2], vp[3]);
	};

	if (!G.fbo || G.fboW != dstW || G.fboH != dstH)
	{
		full = true;
		G.cValid = false;
		G.spW = 0;
		if (!G.fbo) G.genFB(1, &G.fbo);
		if (G.fboTex) glDeleteTextures(1, &G.fboTex);
		G.fboTex = makeTex(dstW, dstH, nullptr);
		G.fboW = dstW; G.fboH = dstH;
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
		G.fbTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, G.fboTex, 0);
		if (G.smOk)
		{
			// class target (scale mask) + result target B
			if (G.clsTex) glDeleteTextures(1, &G.clsTex);
			G.clsTex = makeTex(dstW, dstH, nullptr);
			G.fbTex(GL_FRAMEBUFFER_EXT, HR_COLOR_ATTACHMENT1, GL_TEXTURE_2D, G.clsTex, 0);
			const bool okA = G.fbStatus(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;
			const bool okB = smTarget(G.fboB, G.outTex, dstW, dstH);
			G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
			if (!okA || !okB)
			{
				Log(LOG_WARNING) << "HiResLayer: GL scale-mask framebuffers incomplete: CPU scale-mask";
				G.smOk = false;
				G.fbTex(GL_FRAMEBUFFER_EXT, HR_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
				G.fboW = 0; // rebuild next frame
				popState();
				return false;
			}
		}
		if (G.fbStatus(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		{
			Log(LOG_WARNING) << "HiResLayer: GL map pass framebuffer incomplete: CPU compositing";
			G.ok = false;
			popState();
			return false;
		}
	}
	G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
	glViewport(0, 0, dstW, dstH);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 0);
	// scale mask on the GPU: colour + class targets, everything nearest on its source grid
	const bool interp = HiResResample::mode() != HiResResample::NEAREST;
	if (interp && (!G.smOk || !G.clsTex))
	{
		popState();
		return false;
	}
	const GLenum bufs2[2] = { GL_COLOR_ATTACHMENT0_EXT, HR_COLOR_ATTACHMENT1 };
	auto ortho = [](int w, int h)
	{
		glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, w, h, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
	};
	int mx0, my0, mx1, my1;
	outRect(SDL_Rect{ (Sint16)P.mx0, (Sint16)P.my0, (Uint16)(P.mx1 - P.mx0), (Uint16)(P.my1 - P.my0) }, sx, sy, dstW, dstH, mx0, my0, mx1, my1);
	double tShift = 0, tDraw = 0, tInterp = 0, tCut = 0;
	const double tStart = profNow();

	// --- scroll: move the finished frame (colour, class, result) by the scroll amount
	if (!full && (P.DX || P.DY))
	{
		bool okShift = true;
		if (G.spW != dstW || G.spH != dstH || !G.fboT || !G.spA || !G.spCls || (interp && !G.spOut))
		{
			if (G.spA) glDeleteTextures(1, &G.spA);
			if (G.spCls) glDeleteTextures(1, &G.spCls);
			if (G.spOut) glDeleteTextures(1, &G.spOut);
			if (!G.fboT) G.genFB(1, &G.fboT);
			G.spA = makeTex(dstW, dstH, nullptr);
			G.spCls = makeTex(dstW, dstH, nullptr);
			G.spOut = makeTex(dstW, dstH, nullptr);
			G.spW = dstW; G.spH = dstH;
		}
		auto shiftOne = [&](GLuint &tex, GLuint &spare)
		{
			if (!okShift) return;
			G.bindFB(GL_FRAMEBUFFER_EXT, G.fboT);
			G.fbTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, spare, 0);
			if (G.fbStatus(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) { okShift = false; return; }
			glViewport(0, 0, dstW, dstH);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_BLEND);
			glClear(GL_COLOR_BUFFER_BIT);
			const int dx0 = std::max(mx0, mx0 + P.DX), dx1 = std::min(mx1, mx1 + P.DX);
			const int dy0 = std::max(my0, my0 + P.DY), dy1 = std::min(my1, my1 + P.DY);
			if (dx0 < dx1 && dy0 < dy1)
			{
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, tex);
				glColor4f(1, 1, 1, 1);
				quad(dx0, dy0, dx1, dy1, (dx0 - P.DX) / (float)dstW, 1.0f - (dy0 - P.DY) / (float)dstH, (dx1 - P.DX) / (float)dstW, 1.0f - (dy1 - P.DY) / (float)dstH);
			}
			std::swap(tex, spare);
		};
		if (GF.useProgram) GF.useProgram(0);
		shiftOne(G.fboTex, G.spA);
		if (interp) { shiftOne(G.clsTex, G.spCls); shiftOne(G.outTex, G.spOut); }
		// re-attach the swapped textures
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
		G.fbTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, G.fboTex, 0);
		if (interp) G.fbTex(GL_FRAMEBUFFER_EXT, HR_COLOR_ATTACHMENT1, GL_TEXTURE_2D, G.clsTex, 0);
		if (interp)
		{
			G.bindFB(GL_FRAMEBUFFER_EXT, G.fboB);
			G.fbTex(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, G.outTex, 0);
		}
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
		if (!okShift)
			full = true;
		else
			for (ScaleClass &sc : SM.classes)
			{
				if (sc.bx1 < 0) continue;
				sc.bx0 = std::max(mx0, sc.bx0 + P.DX); sc.bx1 = std::min(mx1, sc.bx1 + P.DX);
				sc.by0 = std::max(my0, sc.by0 + P.DY); sc.by1 = std::min(my1, sc.by1 + P.DY);
				if (sc.bx0 >= sc.bx1 || sc.by0 >= sc.by1) { sc.bx0 = sc.by0 = INT32_MAX; sc.bx1 = sc.by1 = -1; }
			}
		glViewport(0, 0, dstW, dstH);
		ortho(dstW, dstH);
		if (prof.on) glFinish();
		tShift = profNow() - tStart;
	}
	if (full)
	{
		G.cValid = false;
		SM.classes.clear();
	}

	// --- rects to redraw (output px); the whole frame when full
	std::vector<SDL_Rect> rects;
	if (full)
		rects.push_back(SDL_Rect{ 0, 0, (Uint16)dstW, (Uint16)dstH });
	else
		for (const SDL_Rect &r : P.rects)
		{
			int x0, y0, x1, y1;
			outRect(r, sx, sy, dstW, dstH, x0, y0, x1, y1);
			if (x0 < x1 && y0 < y1)
				rects.push_back(SDL_Rect{ (Sint16)x0, (Sint16)y0, (Uint16)(x1 - x0), (Uint16)(y1 - y0) });
		}
	const bool render = full || !P.unchanged;
	if (!forceFull)
	{
		if (full) ++FS.full;
		else if (P.unchanged) ++FS.unchanged;
		else if (P.DX || P.DY) ++FS.scroll;
		else ++FS.dirty;
		FS.rectCount += (long long)rects.size();
	}

	if (render)
	{
		G.cValid = false;
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
		glViewport(0, 0, dstW, dstH);
		ortho(dstW, dstH);
		if (interp)
		{
			GF.drawBuffers(2, bufs2);
			GF.useProgram(G.progDraw);
			GF.uni1i(GF.uniLoc(G.progDraw, "tex"), 0);
			if (SM.classes.empty())
				scaleClass(1.0, 1.0, 0.0, 0.0);
		}
		const GLint locUseTex = interp ? GF.uniLoc(G.progDraw, "useTex") : -1, locCls = interp ? GF.uniLoc(G.progDraw, "cls") : -1;
		auto setCls = [&](bool useTex, int k)
		{
			if (!interp) return;
			GF.uni1f(locUseTex, useTex ? 1.0f : 0.0f);
			GF.uni1f(locCls, k / 255.0f);
		};
		const SDL_Color *bpal = HiResAssets::battlePalette();
		const int mode = HiResResample::mode();
		int bbx0 = 0, bby0 = 0, bbx1 = 0, bby1 = 0;
		auto noteBB = [&](int k)
		{
			if (k < 1 || k > (int)SM.classes.size()) return;
			ScaleClass &sc = SM.classes[k - 1];
			sc.bx0 = std::min(sc.bx0, bbx0); sc.by0 = std::min(sc.by0, bby0);
			sc.bx1 = std::max(sc.bx1, bbx1); sc.by1 = std::max(sc.by1, bby1);
		};
		auto drawCmd = [&](const HiResCmd &c, int rx0, int ry0, int rx1, int ry1)
		{
			if (!c.mapLayer || c.kind != HiResCmdKind::Image)
				return;
			int x0 = std::max(0, (int)std::floor(c.clipX * sx)), y0 = std::max(0, (int)std::floor(c.clipY * sy));
			int x1 = std::min(dstW, (int)std::ceil((c.clipX + c.clipW) * sx));
			int y1 = std::min(dstH, (int)std::ceil((c.clipY + c.clipH) * sy));
			if (x0 >= x1 || y0 >= y1)
				return;
			// extent of the command (class bounding box), before the rect is applied
			bbx0 = x0; bby0 = y0; bbx1 = x1; bby1 = y1;
			if (!c.clearRect)
			{
				bbx0 = std::max(bbx0, (int)std::floor(c.x * sx)); bby0 = std::max(bby0, (int)std::floor(c.y * sy));
				bbx1 = std::min(bbx1, (int)std::ceil((c.x + c.w) * sx)); bby1 = std::min(bby1, (int)std::ceil((c.y + c.h) * sy));
			}
			x0 = std::max(x0, rx0); y0 = std::max(y0, ry0); x1 = std::min(x1, rx1); y1 = std::min(y1, ry1);
			if (x0 >= x1 || y0 >= y1)
				return;
			glScissor(x0, dstH - y1, x1 - x0, y1 - y0);
			if (c.clearRect)
			{
				glDisable(GL_TEXTURE_2D);
				setCls(false, 1);
				glColor4ub(c.fill.r, c.fill.g, c.fill.b, 255);
				quad(x0, y0, x1, y1);
				noteBB(1);
				return;
			}
			const int X0 = (int)std::lround(c.x * sx), Y0 = (int)std::lround(c.y * sy);
			const int oW = (int)std::lround((c.x + c.w) * sx) - X0, oH = (int)std::lround((c.y + c.h) * sy) - Y0;
			if (oW <= 0 || oH <= 0)
				return;
			GLuint tex = 0;
			float f = 1.0f;
			if (interp)
			{
				// nearest on the source grid: quad at exact (fractional) output coordinates, class id
				int k = 0, tw = 0, th = 0;
				if (c.spriteKey)
				{
					auto si = sprites.find(c.spriteKey);
					if (si == sprites.end())
						return;
					tw = si->second.w; th = si->second.h;
					size_t key = 0x5350384241ull;
					auto mix = [&](size_t v) { key = (key ^ v) * 1099511628211ull; };
					mix(c.spriteKey); mix((size_t)tw); mix((size_t)th);
					const Sprite8 &sp = si->second;
					tex = cachedTex(key, tw, th, [&]() { return sp.px.data(); });
					k = 1;
				}
				else if (c.assetId)
				{
					double fx = 0, fy = 0;
					if (HiResAssets::nativeScale(c.assetId, fx, fy))
					{
						const int nW = std::max(1, (int)std::lround(c.srcW * fx)), nH = std::max(1, (int)std::lround(c.srcH * fy));
						const double kx = nW / (double)c.w, ky = nH / (double)c.h;
						if (sx / kx > 1.0 + 1e-6 && sy / ky > 1.0 + 1e-6)
							k = scaleClass(kx, ky, fracOf(c.x * kx), fracOf(c.y * ky));
						if (k)
						{
							const bool variant = c.nvColor || c.rcFrom[0] || c.rcFrom[1] || c.rcFrom[2] || c.rcFrom[3];
							size_t key = 0x4E41544956ull;
							auto mix = [&](size_t v) { key = (key ^ v) * 1099511628211ull; };
							mix(c.assetId); mix(c.palKey); mix((size_t)c.srcX); mix((size_t)c.srcY); mix((size_t)c.srcW); mix((size_t)c.srcH);
							mix((size_t)nW); mix((size_t)nH);
							if (variant)
							{
								mix(c.nvColor);
								for (int q = 0; q < 4; ++q) { mix(c.rcFrom[q]); mix(c.rcTo[q]); }
							}
							tex = cachedTex(key, nW, nH, [&]() -> const Uint32*
							{
								const Uint32 *px = HiResAssets::rendered(c.assetId, c.palKey, c.srcX, c.srcY, c.srcW, c.srcH, nW, nH);
								if (!px || !variant)
									return px;
								const Uint8 *bm = HiResAssets::renderedMask(c.assetId, c.srcX, c.srcY, c.srcW, c.srcH, nW, nH);
								G.scratch.resize((size_t)nW * nH);
								for (size_t q = 0; q < G.scratch.size(); ++q)
									G.scratch[q] = battlePixel(c, px[q], bm ? bm[q] : 0, bpal, 256);
								return G.scratch.data();
							});
							f = HiResAssets::shadeFactor(c.shade) / 256.0f;
						}
					}
				}
				if (k)
				{
					if (!tex)
						return;
					(void)tw; (void)th;
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, tex);
					setCls(true, k);
					glColor4f(f, f, f, 1.0f);
					quadF(c.x * sx, c.y * sy, (c.x + c.w) * sx, (c.y + c.h) * sy);
					noteBB(k);
					return;
				}
				setCls(true, 0); // 1x / downscaled hi-res picture: final (drawn as before)
			}
			if (c.spriteKey)
			{
				size_t k = 0x5350524954ull;
				auto mix = [&](size_t v) { k = (k ^ v) * 1099511628211ull; };
				mix(c.spriteKey); mix((size_t)oW); mix((size_t)oH); mix(c.nearest ? 1000 : (size_t)mode);
				tex = cachedTex(k, oW, oH, [&]() { return spriteRendered(c.spriteKey, oW, oH, c.nearest != 0); });
			}
			else if (c.assetId)
			{
				const bool variant = c.nvColor || c.rcFrom[0] || c.rcFrom[1] || c.rcFrom[2] || c.rcFrom[3];
				size_t k = 0x4153534554ull;
				auto mix = [&](size_t v) { k = (k ^ v) * 1099511628211ull; };
				mix(c.assetId); mix(c.palKey); mix((size_t)c.srcX); mix((size_t)c.srcY); mix((size_t)c.srcW); mix((size_t)c.srcH);
				mix((size_t)oW); mix((size_t)oH); mix((size_t)mode);
				if (variant)
				{
					mix(c.nvColor);
					for (int q = 0; q < 4; ++q) { mix(c.rcFrom[q]); mix(c.rcTo[q]); }
				}
				tex = cachedTex(k, oW, oH, [&]() -> const Uint32*
				{
					const Uint32 *px = HiResAssets::rendered(c.assetId, c.palKey, c.srcX, c.srcY, c.srcW, c.srcH, oW, oH);
					if (!px || !variant)
						return px;
					const Uint8 *bm = HiResAssets::renderedMask(c.assetId, c.srcX, c.srcY, c.srcW, c.srcH, oW, oH);
					G.scratch.resize((size_t)oW * oH);
					for (size_t p = 0; p < G.scratch.size(); ++p)
						G.scratch[p] = battlePixel(c, px[p], bm ? bm[p] : 0, bpal, 256);
					return G.scratch.data();
				});
				f = HiResAssets::shadeFactor(c.shade) / 256.0f;
			}
			if (!tex)
				return;
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, tex);
			glColor4f(f, f, f, 1.0f);
			quad(X0, Y0, X0 + oW, Y0 + oH);
	
		};
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		for (const SDL_Rect &R : rects)
		{
			glEnable(GL_SCISSOR_TEST);
			glScissor(R.x, dstH - (R.y + R.h), R.w, R.h);
			glClear(GL_COLOR_BUFFER_BIT);
			for (size_t i = first; i < S.frame.size(); ++i)
				drawCmd(S.frame[i], R.x, R.y, R.x + R.w, R.y + R.h);
		}
		glDisable(GL_SCISSOR_TEST);
		if (prof.on) glFinish();
		tDraw = profNow() - tStart - tShift;
		if (SM.classes.size() > 200)
			++cacheEpoch; // class table nearly full: next frame starts over
	}
	GLuint presentTex = interp ? G.outTex : G.fboTex;
	if (render && interp)
	{
		// B = A's colour in the rects; then per class: shrink A to its source grid (S), interpolate up into B (rects x class box)
		GF.drawBuffers(1, bufs2);
		GF.useProgram(0);
		glDisable(GL_BLEND);
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fboB);
		glViewport(0, 0, dstW, dstH);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, G.fboTex);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_SCISSOR_TEST);
		for (const SDL_Rect &R : rects)
		{
			glScissor(R.x, dstH - (R.y + R.h), R.w, R.h);
			quad(R.x, R.y, R.x + R.w, R.y + R.h, R.x / (float)dstW, 1.0f - R.y / (float)dstH, (R.x + R.w) / (float)dstW, 1.0f - (R.y + R.h) / (float)dstH);
		}
		glDisable(GL_SCISSOR_TEST);
		const bool debug = Options::oxceHiResDebugScaleMask;
		for (size_t ci = 0; ci < SM.classes.size(); ++ci)
		{
			const ScaleClass &sc = SM.classes[ci];
			if (sc.bx1 < 0)
				continue; // nothing of this class on the map
			const float cls = (ci + 1) / 255.0f;
			const float kpx = (float)(sc.kx / sx), kpy = (float)(sc.ky / sy);
			const int W = (int)std::ceil(S.baseW * sc.kx) + 3, H = (int)std::ceil(S.baseH * sc.ky) + 3;
			if (!debug)
			{
				if (G.sW != W || G.sH != H || !G.sTex)
				{
					if (!smTarget(G.fboS, G.sTex, W, H))
						continue;
					G.sW = W; G.sH = H;
				}
				G.bindFB(GL_FRAMEBUFFER_EXT, G.fboS);
				glViewport(0, 0, W, H);
				ortho(W, H);
				GF.useProgram(G.progShrink);
				GF.activeTex(HR_TEX0 + 1); glBindTexture(GL_TEXTURE_2D, G.clsTex);
				GF.activeTex(HR_TEX0); glBindTexture(GL_TEXTURE_2D, G.fboTex);
				GF.uni1i(GF.uniLoc(G.progShrink, "colTex"), 0);
				GF.uni1i(GF.uniLoc(G.progShrink, "clsTex"), 1);
				GF.uni2f(GF.uniLoc(G.progShrink, "outSize"), (float)dstW, (float)dstH);
				GF.uni2f(GF.uniLoc(G.progShrink, "kps"), kpx, kpy);
				GF.uni2f(GF.uniLoc(G.progShrink, "ph"), (float)sc.phx, (float)sc.phy);
				GF.uni1f(GF.uniLoc(G.progShrink, "cls"), cls);
				quad(0, 0, W, H);
			}
			G.bindFB(GL_FRAMEBUFFER_EXT, G.fboB);
			glViewport(0, 0, dstW, dstH);
			ortho(dstW, dstH);
			// xBRZ: GLSL port by the next integer factor (virtual pixels), bilinear when not enlarging
			const double clsScale = std::max(sx / sc.kx, sy / sc.ky);
			const bool useX = !debug && HiResResample::mode() == HiResResample::PIXEL && G.progXbrz && clsScale > 1.0 + 1e-9;
			const GLuint progU = useX ? G.progXbrz : G.progUp;
			GF.useProgram(progU);
			GF.activeTex(HR_TEX0 + 2); glBindTexture(GL_TEXTURE_2D, G.fboTex);
			GF.activeTex(HR_TEX0 + 1); glBindTexture(GL_TEXTURE_2D, G.clsTex);
			GF.activeTex(HR_TEX0); glBindTexture(GL_TEXTURE_2D, G.sTex);
			GF.uni1i(GF.uniLoc(progU, "sTex"), 0);
			GF.uni1i(GF.uniLoc(progU, "clsTex"), 1);
			GF.uni1i(GF.uniLoc(progU, "colTex"), 2);
			GF.uni2f(GF.uniLoc(progU, "outSize"), (float)dstW, (float)dstH);
			GF.uni2f(GF.uniLoc(progU, "sSize"), (float)std::max(1, G.sW), (float)std::max(1, G.sH));
			GF.uni2f(GF.uniLoc(progU, "kps"), kpx, kpy);
			GF.uni2f(GF.uniLoc(progU, "ph"), (float)sc.phx, (float)sc.phy);
			GF.uni1f(GF.uniLoc(progU, "cls"), cls);
			if (useX) GF.uni1i(GF.uniLoc(progU, "K"), std::min(6, std::max(2, (int)std::ceil(clsScale - 1e-9))));
			else GF.uni1f(GF.uniLoc(progU, "debug"), debug ? 1.0f : 0.0f);
			glEnable(GL_SCISSOR_TEST);
			for (const SDL_Rect &R : rects)
			{
				// only where the class can be: its bounding box
				const int qx0 = std::max((int)R.x, sc.bx0), qy0 = std::max((int)R.y, sc.by0);
				const int qx1 = std::min(R.x + R.w, sc.bx1), qy1 = std::min(R.y + R.h, sc.by1);
				if (qx0 >= qx1 || qy0 >= qy1) continue;
				glScissor(qx0, dstH - qy1, qx1 - qx0, qy1 - qy0);
				quad(qx0, qy0, qx1, qy1);
			}
			glDisable(GL_SCISSOR_TEST);
		}
		GF.useProgram(0);
		GF.activeTex(HR_TEX0);
		if (prof.on) glFinish();
		tInterp = profNow() - tStart - tShift - tDraw;
	}
	if (render && !interp)
		G.bindFB(GL_FRAMEBUFFER_EXT, G.fbo);
	const double tCut0 = profNow();
	// visibility: base pixels covered by later screen blits (panels, windows over the map) are cut out
	if (G.maskW != S.baseW || G.maskH != S.baseH || !G.maskTex)
	{
		if (G.maskTex) glDeleteTextures(1, &G.maskTex);
		G.maskTex = makeTex(S.baseW, S.baseH, nullptr);
		G.maskW = S.baseW; G.maskH = S.baseH;
		G.cValid = false;
	}
	G.maskPx.resize((size_t)S.baseW * S.baseH * 4);
	bool cut = false;
	for (int y = 0; y < S.baseH; ++y)
	{
		const Uint32 *st = &S.stamps[(size_t)y * S.baseW];
		Uint32 *mp = (Uint32*)&G.maskPx[(size_t)y * S.baseW * 4];
		for (int x = 0; x < S.baseW; ++x)
		{
			const bool vis = st[x] <= mapStamp;
			mp[x] = vis ? 0xFFFFFFFFu : 0u;
			cut |= !vis;
		}
	}
	Uint64 cutHash = 1469598103934665603ull;
	{
		const Uint64 *w = (const Uint64*)G.maskPx.data();
		const size_t nw = G.maskPx.size() / 8;
		for (size_t i = 0; i < nw; ++i) cutHash = (cutHash ^ w[i]) * 1099511628211ull;
	}
	if (cut)
	{
		if (render || !G.cValid || G.cutHash != cutHash || G.cW != dstW || G.cH != dstH)
		{
			if (G.cW != dstW || G.cH != dstH || !G.cTex || !G.fboC)
			{
				if (!smTarget(G.fboC, G.cTex, dstW, dstH))
				{
					Log(LOG_WARNING) << "HiResLayer: GL cut target incomplete: CPU compositing";
					G.ok = false;
					popState();
					return false;
				}
				G.cW = dstW; G.cH = dstH;
			}
			G.bindFB(GL_FRAMEBUFFER_EXT, G.fboC);
			glViewport(0, 0, dstW, dstH);
			ortho(dstW, dstH);
			glDisable(GL_SCISSOR_TEST);
			glDisable(GL_BLEND);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, G.maskTex);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S.baseW, S.baseH, GL_BGRA, GL_UNSIGNED_BYTE, G.maskPx.data());
			glColor4f(1, 1, 1, 1);
			quad(0, 0, dstW, dstH);
			glEnable(GL_BLEND);
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
			glBindTexture(GL_TEXTURE_2D, presentTex);
			quad(0, 0, dstW, dstH, 0, 1, 1, 0);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			G.cValid = true;
			G.cutHash = cutHash;
		}
		presentTex = G.cTex;
	}
	if (prof.on) glFinish();
	tCut = profNow() - tCut0;
	popState();
	// FBO over the screen (OpenGL::refresh's ortho: (0,0) bottom-left of the window)
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, presentTex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1, 1, 1, 1);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2i(left, outH - top);
	glTexCoord2f(1, 1); glVertex2i(outW - right, outH - top);
	glTexCoord2f(1, 0); glVertex2i(outW - right, bottom);
	glTexCoord2f(0, 0); glVertex2i(left, bottom);
	glEnd();
	glDisable(GL_BLEND);
	glErrorCheck();
	glMapResult = interp ? G.outTex : G.fboTex;
	if (prof.on && render)
	{
		G.stage[0] += tShift; G.stage[1] += tDraw; G.stage[2] += tInterp; G.stage[3] += tCut;
		if (++G.stageFrames == 60)
		{
			Log(LOG_INFO) << "HiResLayer map pass (ms/frame, rendered frames): shift " << G.stage[0] / 60 << " draw " << G.stage[1] / 60
				<< " interpolate " << G.stage[2] / 60 << " cut " << G.stage[3] / 60;
			G.stage[0] = G.stage[1] = G.stage[2] = G.stage[3] = 0; G.stageFrames = 0;
		}
	}
	return true;
}

/// Debug (oxceHiResVerifyGPU): the map the GPU just produced against the CPU resampler's full recompose of the same frame.
/// Logs the number of differing pixels over the map (exact and by size of the channel error). The CPU buffers are used
/// only as scratch: the class table, the class map and the frame buffer are restored afterwards.
void verifyGpuVsCpu(int dstW, int dstH)
{
	if (curPlan.mx1 <= curPlan.mx0 || curPlan.my1 <= curPlan.my0) return;
	size_t gs = 0;
	bool found = false;
	for (size_t i = 0; i < S.frame.size() && !found; ++i)
		if (S.frame[i].mapLayer && S.frame[i].kind == HiResCmdKind::Image) { gs = i; found = true; }
	if (!found) return;
	while (gs > 0 && S.frame[gs - 1].stamp == S.frame[gs].stamp) --gs;
	size_t ge = gs;
	while (ge < S.frame.size() && S.frame[ge].stamp == S.frame[gs].stamp) ++ge;
	std::vector<Uint8> a((size_t)dstW * dstH * 4);
	glBindTexture(GL_TEXTURE_2D, glMapResult);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, a.data());
	const MapPlan keep = curPlan;
	const std::vector<ScaleClass> classes = SM.classes;
	const std::vector<Uint8> cls = SM.cls;
	const std::vector<Uint32> rgba = S.rgba;
	const double sx = dstW / (double)S.baseW, sy = dstH / (double)S.baseH;
	composeMapCPU(gs, ge, dstW, dstH, sx, sy, true);
	SDL_Rect mapBase = { (Sint16)keep.mx0, (Sint16)keep.my0, (Uint16)(keep.mx1 - keep.mx0), (Uint16)(keep.my1 - keep.my0) };
	int mx0, my0, mx1, my1;
	outRect(mapBase, sx, sy, dstW, dstH, mx0, my0, mx1, my1);
	long long total = 0, exact = 0, big1 = 0, big8 = 0, alphaBad = 0; int maxd = 0;
	int bx0 = dstW, by0 = dstH, bx1 = -1, by1 = -1;
	for (int y = my0; y < my1; ++y)
		for (int x = mx0; x < mx1; ++x)
		{
			const Uint32 c = MB.res[(size_t)y * dstW + x];
			const Uint8 *g = &a[((size_t)(dstH - 1 - y) * dstW + x) * 4]; // GL rows run bottom-up; B G R A
			const int ca = c >> 24, ga = g[3];
			++total;
			if ((ca == 0) != (ga == 0)) { ++alphaBad; continue; }
			if (!ca) continue;
			const int d = std::max(std::abs((int)((c >> 16) & 0xFF) - g[2]), std::max(std::abs((int)((c >> 8) & 0xFF) - g[1]), std::abs((int)(c & 0xFF) - g[0])));
			if (!d) continue;
			++exact; maxd = std::max(maxd, d);
			if (d > 1) ++big1;
			if (d > 8) ++big8;
			bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y);
		}
	SM.classes = classes; SM.cls = cls; S.rgba = rgba;
	Log(LOG_INFO) << "HiResLayer verify (GPU vs CPU, " << (HiResResample::mode() == HiResResample::PIXEL ? "xBRZ" : HiResResample::mode() == HiResResample::HQX ? "HQx" : "smooth")
		<< "): " << keep.reason << " rects " << keep.rects.size() << ": " << total << " px, " << exact << " differ (" << big1 << " by >1, " << big8 << " by >8, max "
		<< maxd << "), " << alphaBad << " alpha mismatches" << (exact ? " in (" + std::to_string(bx0) + "," + std::to_string(by0) + ")-(" + std::to_string(bx1) + "," + std::to_string(by1) + ")" : std::string());
}
}
#endif

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
	const double tc0 = prof.on ? profNow() : 0;
	// layered battlescape on the GPU (option oxceHiResBattleGL), CPU compositing otherwise
	// with interpolation the GL map pass runs the scale mask on the GPU (GLSL + 2 draw buffers);
	// without those features the map is composed (and scale-masked) on the CPU and uploaded
	const bool optGL = Options::oxceHiResBattleGL && layerSurf;
	const bool fbo = optGL && glMapInit();
	const bool interpMode = HiResResample::mode() != HiResResample::NEAREST;
	// automatic GPU / CPU choice for the filter: GPU when OpenGL >= 3.0 (for the pixel-art filters) and the shaders build
	const bool smOk = fbo && interpMode && glScaleMaskInit();
	static std::string filterWhy;
	bool filterGpu = true;
	filterWhy.clear();
	if (smOk && HiResResample::mode() == HiResResample::PIXEL)
		filterGpu = glXbrzInit(filterWhy);
	else if (smOk && HiResResample::mode() == HiResResample::HQX)
	{
		filterGpu = false;
		filterWhy = "HQx is not ported to the GPU yet";
	}
	const bool wantGL = fbo && (!interpMode || (smOk && filterGpu));
	static std::string cpuReasonBuf;
	if (smOk && !filterGpu)
		cpuReasonBuf = std::string(HiResResample::mode() == HiResResample::PIXEL ? "xBRZ" : "HQx") + " on the CPU resampler: " + filterWhy;
	const char *cpuReason = !Options::oxceHiResBattleGL ? "option oxceHiResBattleGL is off"
		: !fbo ? "no OpenGL framebuffer objects" : (smOk && !filterGpu) ? cpuReasonBuf.c_str() : !wantGL ? "no GLSL / multiple draw buffers (scale-mask on the CPU)" : nullptr;
	glMapActive = false;
	double tg = 0;
	const double tf0 = profNow();
	planMapFrame(dstW, dstH, wantGL ? 2 : 1);
	if (wantGL)
	{
		const double t0 = prof.on ? profNow() : 0;
		glMapActive = glMapPass(dstW, dstH, outW, outH, top, bottom, left, right);
		if (!glMapActive)
		{
			// GL map pass failed (framebuffer incomplete...): this frame and the next ones on the CPU
			++FS.fallback;
			planMapFrame(dstW, dstH, 1);
			cpuReason = "GL map pass failed (framebuffer incomplete or unsupported), fell back";
		}
		else if (Options::oxceHiResVerifyGPU && curPlan.active && HiResResample::mode() != HiResResample::NEAREST)
		{
			verifyGpuVsCpu(dstW, dstH);
			if (Options::oxceHiResVerifyReuse && !curPlan.full)
				Log(LOG_INFO) << "HiResLayer: oxceHiResVerifyReuse skipped in this frame (oxceHiResVerifyGPU active)";
		}
		else if (Options::oxceHiResVerifyReuse && !curPlan.full && curPlan.active)
		{
			// debug: compare the reused result with a full recompose, log the difference
			std::vector<Uint8> a((size_t)dstW * dstH * 4), b(a.size());
			glBindTexture(GL_TEXTURE_2D, glMapResult);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, a.data());
			const MapPlan keep = curPlan;
			glMapPass(dstW, dstH, outW, outH, top, bottom, left, right, true);
			glBindTexture(GL_TEXTURE_2D, glMapResult);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, b.data());
			long long bad = 0; int bx0 = dstW, by0 = dstH, bx1 = -1, by1 = -1;
			for (int y = 0; y < dstH; ++y)
				for (int x = 0; x < dstW; ++x)
					if (std::memcmp(&a[((size_t)y * dstW + x) * 4], &b[((size_t)y * dstW + x) * 4], 4))
					{
						++bad; bx0 = std::min(bx0, x); by0 = std::min(by0, y); bx1 = std::max(bx1, x); by1 = std::max(by1, y);
					}
			Log(LOG_INFO) << "HiResLayer verify (GL): " << keep.reason << " dx " << keep.dx << " dy " << keep.dy << " rects " << keep.rects.size()
				<< ": " << bad << " pixels differ" << (bad ? " in (" + std::to_string(bx0) + "," + std::to_string(by0) + ")-(" + std::to_string(bx1) + "," + std::to_string(by1) + ")" : std::string());
		}
		if (prof.on) { glFinish(); tg = profNow() - t0; }
	}
	logMapPath(glMapActive, glMapActive ? nullptr : (curPlan.active ? cpuReason : nullptr));
	SDL_Rect dirty = compose(dstW, dstH, cleared);
	glMapActive = false;
	const double tc1 = prof.on ? profNow() : 0;
	SDL_Rect upload = cleared;
	if (!CC.skipped)
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
	if (prof.on)
	{
		glFinish();
		const double tc2 = profNow();
		prof.comp += tc1 - tc0 - tg; prof.up += tc2 - tc1; prof.gl += tg; prof.cmds += S.frame.size();
		if (++prof.frames == 60)
		{
			Log(LOG_INFO) << "HiResLayer prof (ms/frame): record " << prof.rec / 60 << " deliver " << prof.del / 60 << " compose " << prof.comp / 60
				<< " (scale-mask " << prof.smask / 60 << ") upload " << prof.up / 60 << " glmap " << prof.gl / 60 << " cmds " << prof.cmds / 60;
			prof.rec = prof.del = prof.comp = prof.up = prof.gl = prof.smask = 0; prof.frames = 0; prof.cmds = 0;
		}
	}

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
	frameDone(profNow() - tf0);
	// restore the state OpenGL::refresh() relies on
	glBindTexture(GL_TEXTURE_2D, gl->gltexture);
#endif
}

}
