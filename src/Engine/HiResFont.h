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
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "Unicode.h"

namespace OpenXcom
{

/**
 * Rasterized glyph (8-bit coverage maps) for the hi-res overlay.
 * Coordinates are relative to the pen position on the baseline, y grows down.
 */
struct HiResGlyph
{
	bool ok = false;
	int advance = 0;                 ///< horizontal advance in output pixels (hinted, rounded)
	int adv64 = 0;                   ///< unrounded (linear) advance in 1/64 px, for pen layout
	int face = -1;                   ///< face of the chain that provided the glyph
	unsigned glyphIndex = 0;         ///< glyph index in that face (kerning)
	int fillW = 0, fillH = 0, fillL = 0, fillT = 0;   ///< fill bitmap size/offset (T = pixels above baseline)
	std::vector<uint8_t> fill;
	int lineW = 0, lineH = 0, lineL = 0, lineT = 0;   ///< outline (stroked, expanded) bitmap
	std::vector<uint8_t> line;
};

/**
 * One scalable face found on the system (for the font selection menu).
 */
struct HiResFontInfo
{
	std::string spec;        ///< value to store in options ("malgun.ttf", "gulim.ttc#1", or an absolute path)
	std::string path;        ///< absolute path
	int index = 0;           ///< face index inside a collection (.ttc)
	std::string family;      ///< family name (English / default)
	std::string familyKo;    ///< Korean localized family name, if the font has one
	std::string style;       ///< style name ("Regular", "Bold"...)
	bool hangul = false;     ///< has all-basic Hangul coverage (U+AC00 and U+D7A3)
};

/**
 * TrueType/OpenType font renderer for the hi-res overlay, based on FreeType.
 * Supports a chain of fallback faces (first face that has the glyph wins),
 * per-pixel-size rendering, outline stroking and a glyph cache.
 */
class HiResFont
{
public:
	struct Settings
	{
		int hinting = 1;        ///< 0 = none, 1 = light, 2 = normal (full), 3 = mono (implies no AA)
		bool antialias = true;
	};
private:
	void *_lib = nullptr;              ///< FT_Library
	std::vector<void*> _faces;          ///< FT_Face chain
	std::vector<std::string> _faceNames;
	Settings _settings;
	std::unordered_map<UCode, int> _faceForCode;              ///< -1 = none
	std::unordered_map<uint64_t, HiResGlyph> _cache;
	std::unordered_map<uint64_t, std::pair<int,int>> _metrics; ///< size4 -> (ascender, descender) of primary face
	int selectFace(int face, int size4);
public:
	HiResFont();
	~HiResFont();
	HiResFont(const HiResFont&) = delete;
	HiResFont& operator=(const HiResFont&) = delete;
	/// Resolves a font spec ("name.ttf", "/abs/path.ttc#1") to an existing path. Empty if not found.
	static std::string resolvePath(const std::string &spec, int *faceIndex);
	/// Loads faces from a list of font specs. Returns number of loaded faces.
	int load(const std::vector<std::string> &specs, const Settings &settings);
	bool empty() const { return _faces.empty(); }
	const std::vector<std::string> &getFaceNames() const { return _faceNames; }
	/// Index of first face providing the code point, -1 if none.
	int faceFor(UCode c);
	bool hasGlyph(UCode c) { return faceFor(c) >= 0; }
	/// Sizes ('size4') are pixel sizes (em) in 1/4 pixel, so fractional sizes can be used.
	/// Renders (or fetches from cache) a glyph at a size with an outline radius and emboldening (both in 1/64 pixel).
	/// hscale256: horizontal scale of the glyph (256 = none, 205..255 = narrowed, for text squeezed
	/// into its box); applied to the outline before emboldening/stroking, so stroke widths stay the same.
	const HiResGlyph &getGlyph(UCode c, int size4, int outline64, int bold64 = 0, int hscale256 = 256);
	/// Ascender (positive) and descender (negative) of the primary face at a size (1/4 px).
	std::pair<int,int> getMetrics(int size4);
	/// Advance width of a glyph at a size (1/4 px, no outline), -1 if missing.
	int getAdvance(UCode c, int size4);
	/// Kerning between two glyphs (from getGlyph) in 1/64 px, 0 if unknown/different faces. Legacy 'kern' table only (no GPOS).
	int getKerning64(const HiResGlyph &left, const HiResGlyph &right, int size4);
	/// Scans the system font folders (and on Windows the registry font list) for scalable faces. Cached after the first call.
	static const std::vector<HiResFontInfo> &enumerateSystemFonts(bool rescan = false);
	/// Font folders searched for relative font specs.
	static std::vector<std::string> fontDirs();
	/// Canonical form of a font path for comparisons (lower case, forward slashes, "//" collapsed).
	static std::string normalizePath(const std::string &path);
	/// Drops all cached glyphs.
	void clearCache() { _cache.clear(); }
	/// Bounds the cache (call only when no glyph references are held). An LRU is a TODO.
	void trimCache(size_t maxEntries) { if (_cache.size() > maxEntries) _cache.clear(); }
	size_t cacheSize() const { return _cache.size(); }
};

}
