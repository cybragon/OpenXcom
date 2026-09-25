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
#include "HiResFont.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include FT_OUTLINE_H
#include <cstdlib>
#include <fstream>
#include "Logger.h"

namespace OpenXcom
{

namespace
{
bool fileExists(const std::string &p)
{
	std::ifstream f(p, std::ios::binary);
	return f.good();
}
void copyBitmap(const FT_Bitmap &bm, std::vector<uint8_t> &out, int &w, int &h)
{
	w = bm.width;
	h = bm.rows;
	out.assign(w * h, 0);
	for (int y = 0; y < h; ++y)
	{
		const unsigned char *row = bm.buffer + y * bm.pitch;
		for (int x = 0; x < w; ++x)
		{
			if (bm.pixel_mode == FT_PIXEL_MODE_MONO)
				out[y * w + x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
			else
				out[y * w + x] = row[x];
		}
	}
}
}

HiResFont::HiResFont()
{
	FT_Library lib;
	if (FT_Init_FreeType(&lib) == 0)
		_lib = lib;
	else
		Log(LOG_ERROR) << "HiResFont: FT_Init_FreeType failed";
}

HiResFont::~HiResFont()
{
	for (void *f : _faces)
		FT_Done_Face((FT_Face)f);
	if (_lib)
		FT_Done_FreeType((FT_Library)_lib);
}

std::string HiResFont::resolvePath(const std::string &specIn, int *faceIndex)
{
	std::string spec = specIn;
	*faceIndex = 0;
	size_t hash = spec.rfind('#');
	if (hash != std::string::npos)
	{
		*faceIndex = std::atoi(spec.c_str() + hash + 1);
		spec = spec.substr(0, hash);
	}
	if (spec.empty())
		return "";
	if (fileExists(spec))
		return spec;
	std::vector<std::string> dirs;
	if (const char *windir = std::getenv("WINDIR"))
	{
		dirs.push_back(std::string(windir) + "/Fonts/");
	}
	if (const char *local = std::getenv("LOCALAPPDATA"))
	{
		dirs.push_back(std::string(local) + "/Microsoft/Windows/Fonts/"); // per-user installed fonts (Win10+)
	}
	dirs.push_back("C:/Windows/Fonts/");
	dirs.push_back("/usr/share/fonts/truetype/nanum/");
	dirs.push_back("/usr/share/fonts/opentype/noto/");
	dirs.push_back("/usr/share/fonts/truetype/noto/");
	dirs.push_back("/usr/share/fonts/truetype/dejavu/");
	dirs.push_back("/usr/share/fonts/truetype/");
	dirs.push_back("/Library/Fonts/");
	dirs.push_back("/System/Library/Fonts/");
	for (const auto &d : dirs)
	{
		if (fileExists(d + spec))
			return d + spec;
	}
	return "";
}

int HiResFont::load(const std::vector<std::string> &specs, const Settings &settings)
{
	_settings = settings;
	if (!_lib)
		return 0;
	for (const auto &spec : specs)
	{
		int index = 0;
		std::string path = resolvePath(spec, &index);
		if (path.empty())
		{
			Log(LOG_VERBOSE) << "HiResFont: font not found: " << spec;
			continue;
		}
		FT_Face face;
		if (FT_New_Face((FT_Library)_lib, path.c_str(), index, &face) != 0)
		{
			Log(LOG_WARNING) << "HiResFont: failed to open font " << path;
			continue;
		}
		FT_Select_Charmap(face, FT_ENCODING_UNICODE);
		_faces.push_back(face);
		_faceNames.push_back(path + (index ? "#" + std::to_string(index) : ""));
		Log(LOG_INFO) << "HiResFont: loaded " << path << " (" << face->family_name << ", " << face->num_glyphs << " glyphs)";
	}
	return (int)_faces.size();
}

int HiResFont::faceFor(UCode c)
{
	auto it = _faceForCode.find(c);
	if (it != _faceForCode.end())
		return it->second;
	int found = -1;
	for (size_t i = 0; i < _faces.size(); ++i)
	{
		if (FT_Get_Char_Index((FT_Face)_faces[i], c) != 0)
		{
			found = (int)i;
			break;
		}
	}
	_faceForCode[c] = found;
	return found;
}

int HiResFont::selectFace(int face, int px)
{
	FT_Face f = (FT_Face)_faces[face];
	if (FT_Set_Pixel_Sizes(f, 0, px) != 0)
		return -1;
	return face;
}

std::pair<int,int> HiResFont::getMetrics(int px)
{
	auto it = _metrics.find(px);
	if (it != _metrics.end())
		return it->second;
	std::pair<int,int> m(px * 4 / 5, -px / 5);
	if (!_faces.empty() && selectFace(0, px) >= 0)
	{
		FT_Face f = (FT_Face)_faces[0];
		m.first = (int)(f->size->metrics.ascender >> 6);
		m.second = (int)(f->size->metrics.descender >> 6);
	}
	_metrics[px] = m;
	return m;
}

int HiResFont::getAdvance(UCode c, int px)
{
	const HiResGlyph &g = getGlyph(c, px, 0);
	return g.ok ? g.advance : -1;
}

const HiResGlyph &HiResFont::getGlyph(UCode c, int px, int outline64, int bold64)
{
	uint64_t key = ((uint64_t)c << 32) | ((uint64_t)(px & 0xFFF) << 20) | ((uint64_t)(outline64 & 0x3FF) << 10) | (uint64_t)(bold64 & 0x3FF);
	auto it = _cache.find(key);
	if (it != _cache.end())
		return it->second;
	HiResGlyph &g = _cache[key];
	int face = faceFor(c);
	if (face < 0 || px <= 0 || selectFace(face, px) < 0)
		return g;
	FT_Face f = (FT_Face)_faces[face];

	FT_Int32 flags = FT_LOAD_DEFAULT;
	switch (_settings.hinting)
	{
	case 0: flags |= FT_LOAD_NO_HINTING; break;
	case 1: flags |= FT_LOAD_TARGET_LIGHT; break;
	case 3: flags |= FT_LOAD_TARGET_MONO; break;
	default: flags |= FT_LOAD_TARGET_NORMAL; break;
	}
	FT_Render_Mode mode = (_settings.antialias && _settings.hinting != 3) ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO;
	if (FT_Load_Glyph(f, FT_Get_Char_Index(f, c), flags) != 0)
		return g;
	g.advance = (int)((f->glyph->advance.x + 32) >> 6);
	if (bold64 > 0 && f->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
	{
		// thicken strokes (the bitmap fonts have 1 base-pixel wide strokes)
		FT_Outline_Embolden(&f->glyph->outline, bold64);
		FT_Outline_Translate(&f->glyph->outline, -bold64 / 2, -bold64 / 2);
	}

	FT_Glyph glyph;
	if (FT_Get_Glyph(f->glyph, &glyph) != 0)
		return g;

	// outline (expanded glyph) first, from a copy
	if (outline64 > 0 && glyph->format == FT_GLYPH_FORMAT_OUTLINE)
	{
		FT_Glyph stroked;
		FT_Stroker stroker;
		if (FT_Glyph_Copy(glyph, &stroked) == 0 && FT_Stroker_New((FT_Library)_lib, &stroker) == 0)
		{
			FT_Stroker_Set(stroker, outline64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
			if (FT_Glyph_StrokeBorder(&stroked, stroker, 0, 1) == 0 &&
				FT_Glyph_To_Bitmap(&stroked, mode, nullptr, 1) == 0)
			{
				FT_BitmapGlyph bg = (FT_BitmapGlyph)stroked;
				copyBitmap(bg->bitmap, g.line, g.lineW, g.lineH);
				g.lineL = bg->left;
				g.lineT = bg->top;
			}
			FT_Stroker_Done(stroker);
			FT_Done_Glyph(stroked);
		}
	}
	if (FT_Glyph_To_Bitmap(&glyph, mode, nullptr, 1) == 0)
	{
		FT_BitmapGlyph bg = (FT_BitmapGlyph)glyph;
		copyBitmap(bg->bitmap, g.fill, g.fillW, g.fillH);
		g.fillL = bg->left;
		g.fillT = bg->top;
		g.ok = true;
	}
	FT_Done_Glyph(glyph);
	return g;
}

}
