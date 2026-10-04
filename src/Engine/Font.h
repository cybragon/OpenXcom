#pragma once
/*
 * Copyright 2010-2016 OpenXcom Developers.
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
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>
#include <SDL.h>
#include "../Engine/Yaml.h"
#include "Unicode.h"

#include "Surface.h"

namespace OpenXcom
{

class Surface;
class Palette;

struct FontImage
{
	int width, height, spacing;
	Surface *surface;
};

/**
 * Takes care of loading and storing each character in a sprite font.
 * Sprite fonts consist of a set of characters split in fixed-size regions.
 * @note The characters don't all need to be the same size, they can
 * have blank space and will be automatically lined up properly.
 */
class Font
{
private:
	std::vector<FontImage> _images;
	std::unordered_map< UCode, std::pair<size_t, SDL_Rect> > _chars;
	bool _monospace;
	std::string _id;                ///< font id from Font.dat (FONT_BIG...)
	int _hiresSlot = -1;            ///< hi-res overlay font slot, -1 = none
	mutable std::unordered_map<UCode, std::pair<int,int>> _inkRows;
	/// Determines the size and position of each character in the font.
	void init(size_t index, const UString &str);
public:

	/// Default palette for terminal text.
	static const SDL_Color TerminalColors[2];

	/// Creates a blank font.
	Font();
	/// Cleans up the font.
	~Font();
	/// Loads the font from YAML.
	void load(const YAML::YamlNodeReader& reader);
	/// Generate the terminal font.
	void loadTerminal();
	/// Gets a particular character from the font, with its real size.
	SurfaceCrop getChar(UCode c) const;
	/// Gets the font's character width.
	int getWidth() const;
	/// Gets the font's character height.
	int getHeight() const;
	/// Gets the spacing between characters.
	int getSpacing() const;
	/// Gets the size of a particular character;
	SDL_Rect getCharSize(UCode c) const;
	/// Is this a monospace (terminal) font?
	bool isMonospace() const { return _monospace; }
	/// Font id (from Font.dat) and hi-res overlay slot.
	void setId(const std::string &id, int hiresSlot) { _id = id; _hiresSlot = hiresSlot; }
	const std::string &getId() const { return _id; }
	int getHiResSlot() const { return _hiresSlot; }
	/// First/last row (relative to the cell top) of the glyph body pixels (font values 1-3). False if unknown.
	bool getInkRows(UCode c, int &top, int &bottom) const;
	/// Does the bitmap font contain this character?
	bool hasChar(UCode c) const { return _chars.find(c) != _chars.end(); }
};

}
