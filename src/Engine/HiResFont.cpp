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
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <set>
#include "Logger.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace OpenXcom
{

namespace
{
/// UTF-8 path aware existence check (std::ifstream/fopen use the ANSI code page on Windows).
bool fileExists(const std::string &p)
{
	std::error_code ec;
	return std::filesystem::is_regular_file(std::filesystem::u8path(p), ec);
}
#ifdef _WIN32
std::wstring toWide(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	if (n <= 1) return std::wstring();
	std::wstring w(n - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}
std::string fromWide(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return std::string();
	std::string s(n - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
	return s;
}
/// Environment variable as UTF-8 (getenv() returns ANSI code page text, which mangles e.g. Korean user names).
std::string envUtf8(const wchar_t *name)
{
	const wchar_t *v = _wgetenv(name);
	return v ? fromWide(v) : std::string();
}
unsigned long ftStreamRead(FT_Stream stream, unsigned long offset, unsigned char *buffer, unsigned long count)
{
	FILE *f = (FILE*)stream->descriptor.pointer;
	if (_fseeki64(f, (long long)offset, SEEK_SET) != 0)
		return count == 0 ? 1 : 0; // count == 0: seek request, non-zero result means error
	if (count == 0)
		return 0;
	return (unsigned long)fread(buffer, 1, count, f);
}
void ftStreamClose(FT_Stream stream)
{
	if (stream->descriptor.pointer)
		fclose((FILE*)stream->descriptor.pointer);
	delete stream;
}
#endif
/// FT_New_Face replacement that accepts UTF-8 paths on every platform.
/// On Windows FreeType's builtin stream uses fopen() (ANSI code page), so open the file with _wfopen
/// and hand FreeType a custom stream instead (no need to read the whole file into memory).
FT_Error openFace(FT_Library lib, const std::string &path, FT_Long index, FT_Face *face)
{
#ifdef _WIN32
	std::wstring w = toWide(path);
	FILE *f = w.empty() ? nullptr : _wfopen(w.c_str(), L"rb");
	if (!f)
		return FT_Err_Cannot_Open_Resource;
	_fseeki64(f, 0, SEEK_END);
	long long size = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	if (size <= 0 || size > 0x7fffffffLL)
	{
		fclose(f);
		return FT_Err_Cannot_Open_Stream;
	}
	FT_Stream stream = new FT_StreamRec();
	stream->size = (unsigned long)size;
	stream->pos = 0;
	stream->descriptor.pointer = f;
	stream->read = ftStreamRead;
	stream->close = ftStreamClose;
	FT_Open_Args args;
	memset(&args, 0, sizeof(args));
	args.flags = FT_OPEN_STREAM;
	args.stream = stream;
	// FreeType calls stream->close (which frees the stream) also when FT_Open_Face fails
	return FT_Open_Face(lib, &args, index, face);
#else
	return FT_New_Face(lib, path.c_str(), index, face);
#endif
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

std::vector<std::string> HiResFont::fontDirs()
{
	std::vector<std::string> dirs;
#ifdef _WIN32
	std::string windir = envUtf8(L"WINDIR");
	if (!windir.empty())
		dirs.push_back(windir + "/Fonts/");
	std::string local = envUtf8(L"LOCALAPPDATA");
	if (!local.empty())
		dirs.push_back(local + "/Microsoft/Windows/Fonts/"); // per-user installed fonts (Win10+)
	dirs.push_back("C:/Windows/Fonts/");
#elif defined(__APPLE__)
	if (const char *home = std::getenv("HOME"))
	{
		dirs.push_back(std::string(home) + "/Library/Fonts/");
	}
	dirs.push_back("/Library/Fonts/");
	dirs.push_back("/System/Library/Fonts/");
#else
	if (const char *home = std::getenv("HOME"))
	{
		dirs.push_back(std::string(home) + "/.local/share/fonts/");
		dirs.push_back(std::string(home) + "/.fonts/");
	}
	dirs.push_back("/usr/local/share/fonts/");
	dirs.push_back("/usr/share/fonts/");
#endif
	return dirs;
}

namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}
bool isFontFile(const std::string &name)
{
	std::string l = lower(name);
	auto ends = [&](const char *e) { size_t n = strlen(e); return l.size() > n && l.compare(l.size() - n, n, e) == 0; };
	return ends(".ttf") || ends(".ttc") || ends(".otf") || ends(".otc");
}
std::string baseName(const std::string &p)
{
	size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? p : p.substr(s + 1);
}
void scanDir(const std::filesystem::path &dir, int depth, std::vector<std::string> &out, std::set<std::string> &seen)
{
	std::error_code ec;
	if (depth > 5 || !std::filesystem::is_directory(dir, ec))
		return;
	for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code ec2;
		if (it->is_directory(ec2))
		{
			scanDir(it->path(), depth + 1, out, seen);
		}
		else
		{
			std::string p = it->path().u8string();
			if (isFontFile(p) && seen.insert(HiResFont::normalizePath(p)).second)
				out.push_back(p);
		}
	}
}
#ifdef _WIN32
/// Font files registered in HKLM/HKCU ...\Windows NT\CurrentVersion\Fonts (value data = file name or absolute path).
void registryFonts(std::vector<std::string> &out, std::set<std::string> &seen, const std::string &winFonts)
{
	HKEY roots[2] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for (HKEY root : roots)
	{
		HKEY key;
		if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &key) != ERROR_SUCCESS)
			continue;
		for (DWORD i = 0; ; ++i)
		{
			wchar_t name[512]; DWORD nameLen = 512;
			BYTE data[2048]; DWORD dataLen = sizeof(data) - 2; DWORD type = 0;
			LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, data, &dataLen);
			if (r == ERROR_NO_MORE_ITEMS)
				break;
			if (r != ERROR_SUCCESS || type != REG_SZ)
				continue;
			data[dataLen] = 0; data[dataLen + 1] = 0;
			const wchar_t *w = (const wchar_t*)data;
			int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n <= 1)
				continue;
			std::string file(n - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, w, -1, &file[0], n, nullptr, nullptr);
			if (file.find(':') == std::string::npos && file.find('\\') != 0)
				file = winFonts + file;
			if (isFontFile(file) && seen.insert(HiResFont::normalizePath(file)).second)
				out.push_back(file);
		}
		RegCloseKey(key);
	}
}
#endif
std::string utf16beToUtf8(const FT_Byte *s, FT_UInt len)
{
	std::string out;
	for (FT_UInt i = 0; i + 1 < len; i += 2)
	{
		unsigned c = (s[i] << 8) | s[i + 1];
		if (c >= 0xD800 && c <= 0xDFFF)
			continue; // no surrogates in family names we care about
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
	}
	return out;
}
}

const std::vector<HiResFontInfo> &HiResFont::enumerateSystemFonts(bool rescan)
{
	static std::vector<HiResFontInfo> list;
	static bool done = false;
	if (done && !rescan)
		return list;
	done = true;
	list.clear();
	std::vector<std::string> files;
	std::set<std::string> seen;
	for (const auto &d : fontDirs())
	{
		scanDir(std::filesystem::u8path(d), 0, files, seen);
	}
#ifdef _WIN32
	std::string winFonts = "C:/Windows/Fonts/";
	std::string windir = envUtf8(L"WINDIR");
	if (!windir.empty())
		winFonts = windir + "/Fonts/";
	registryFonts(files, seen, winFonts);
#endif
	FT_Library lib;
	if (FT_Init_FreeType(&lib) != 0)
		return list;
	for (const auto &file : files)
	{
		FT_Face face;
		if (openFace(lib, file, -1, &face) != 0)
			continue;
		FT_Long n = face->num_faces;
		FT_Done_Face(face);
		for (FT_Long i = 0; i < n && i < 64; ++i)
		{
			if (openFace(lib, file, i, &face) != 0)
				continue;
			if (FT_IS_SCALABLE(face) && FT_Select_Charmap(face, FT_ENCODING_UNICODE) == 0 && face->family_name)
			{
				HiResFontInfo info;
				info.path = file;
				info.index = (int)i;
				info.family = face->family_name;
				info.style = face->style_name ? face->style_name : "";
				info.hangul = FT_Get_Char_Index(face, 0xAC00) != 0 && FT_Get_Char_Index(face, 0xD7A3) != 0;
				FT_UInt names = FT_Get_Sfnt_Name_Count(face);
				for (FT_UInt k = 0; k < names; ++k)
				{
					FT_SfntName nm;
					if (FT_Get_Sfnt_Name(face, k, &nm) == 0 && nm.platform_id == TT_PLATFORM_MICROSOFT &&
						nm.language_id == TT_MS_LANGID_KOREAN_KOREA && nm.name_id == TT_NAME_ID_FONT_FAMILY)
					{
						info.familyKo = utf16beToUtf8(nm.string, nm.string_len);
						break;
					}
				}
				list.push_back(info);
			}
			FT_Done_Face(face);
		}
	}
	FT_Done_FreeType(lib);
	std::sort(list.begin(), list.end(), [](const HiResFontInfo &a, const HiResFontInfo &b)
	{
		if (a.hangul != b.hangul) return a.hangul; // Korean-capable fonts first
		std::string la = lower(a.family), lb = lower(b.family);
		if (la != lb) return la < lb;
		return a.style < b.style;
	});
	// short, portable spec when the plain file name resolves to the same file
	for (auto &info : list)
	{
		std::string shortSpec = baseName(info.path) + (info.index ? "#" + std::to_string(info.index) : "");
		int idx = 0;
		std::string resolved = resolvePath(shortSpec, &idx);
		info.spec = (normalizePath(resolved) == normalizePath(info.path)) ? shortSpec : info.path + (info.index ? "#" + std::to_string(info.index) : "");
	}
	Log(LOG_INFO) << "HiResFont: found " << list.size() << " scalable faces in " << files.size() << " font files";
	return list;
}

std::string HiResFont::normalizePath(const std::string &path)
{
	std::string out;
	out.reserve(path.size());
	for (char c : path)
	{
		char n = (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
		if (n == '/' && !out.empty() && out.back() == '/')
			continue;
		out += n;
	}
	return out;
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
	for (const auto &d : fontDirs())
	{
		if (fileExists(d + spec))
			return d + spec;
	}
	// fonts in sub folders (Linux distributions): find by file name
	static std::vector<std::string> deep;
	static bool scanned = false;
	if (!scanned)
	{
		scanned = true;
		std::set<std::string> seen;
		for (const auto &d : fontDirs())
			scanDir(std::filesystem::u8path(d), 0, deep, seen);
	}
	std::string want = lower(spec);
	for (const auto &p : deep)
	{
		if (lower(baseName(p)) == want)
			return p;
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
		if (openFace((FT_Library)_lib, path, index, &face) != 0)
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

int HiResFont::selectFace(int face, int size4)
{
	FT_Face f = (FT_Face)_faces[face];
	// 26.6 character size at 72 dpi = pixel size
	if (FT_Set_Char_Size(f, 0, (FT_F26Dot6)size4 * 16, 72, 72) != 0)
		return -1;
	return face;
}

std::pair<int,int> HiResFont::getMetrics(int size4)
{
	auto it = _metrics.find(size4);
	if (it != _metrics.end())
		return it->second;
	std::pair<int,int> m(size4 / 5, -size4 / 20);
	if (!_faces.empty() && selectFace(0, size4) >= 0)
	{
		FT_Face f = (FT_Face)_faces[0];
		m.first = (int)(f->size->metrics.ascender >> 6);
		m.second = (int)(f->size->metrics.descender >> 6);
	}
	_metrics[size4] = m;
	return m;
}

int HiResFont::getAdvance(UCode c, int size4)
{
	const HiResGlyph &g = getGlyph(c, size4, 0);
	return g.ok ? g.advance : -1;
}

int HiResFont::getKerning64(const HiResGlyph &left, const HiResGlyph &right, int size4)
{
	if (!left.ok || !right.ok || left.face < 0 || left.face != right.face)
		return 0;
	FT_Face f = (FT_Face)_faces[left.face];
	if (!FT_HAS_KERNING(f) || selectFace(left.face, size4) < 0)
		return 0;
	FT_Vector k;
	if (FT_Get_Kerning(f, left.glyphIndex, right.glyphIndex, FT_KERNING_UNFITTED, &k) != 0)
		return 0;
	return (int)k.x;
}

const HiResGlyph &HiResFont::getGlyph(UCode c, int size4, int outline64, int bold64, int hscale256)
{
	hscale256 = std::max(128, std::min(256, hscale256));
	uint64_t key = ((uint64_t)c << 32) | ((uint64_t)(size4 & 0xFFF) << 20) | ((uint64_t)(outline64 & 0x3FF) << 10) | (uint64_t)(bold64 & 0x3FF)
		| ((uint64_t)(256 - hscale256) << 53);
	auto it = _cache.find(key);
	if (it != _cache.end())
		return it->second;
	HiResGlyph &g = _cache[key];
	int face = faceFor(c);
	if (face < 0 || size4 <= 0 || selectFace(face, size4) < 0)
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
	g.adv64 = (int)(f->glyph->linearHoriAdvance >> 10); // 16.16 -> 26.6
	g.face = face;
	g.glyphIndex = FT_Get_Char_Index(f, c);
	if (hscale256 != 256)
	{
		g.adv64 = (int)(((long long)g.adv64 * hscale256 + 128) >> 8);
		g.advance = (g.adv64 + 32) >> 6;
		if (f->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
		{
			FT_Matrix m;
			m.xx = (FT_Fixed)hscale256 << 8; // 16.16
			m.xy = 0;
			m.yx = 0;
			m.yy = 0x10000;
			FT_Outline_Transform(&f->glyph->outline, &m);
		}
	}
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
