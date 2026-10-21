#pragma once
/*
 * Copyright 2010-2015 OpenXcom Developers.
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
#include "../Engine/Surface.h"

namespace OpenXcom
{

class BattleUnit;
class BattleItem;
class SavedBattleGame;
class SurfaceSet;
class Mod;

/**
 * A class that renders a specific unit, given its render rules
 * combining the right frames from the surfaceset.
 */
class ItemSprite
{
private:
	const SurfaceSet *_itemSurface;
	int _animationFrame;
	Surface *_dest;
	const SavedBattleGame *_save;


public:
	/// Creates a new ItemSprite at the specified position and size.
	ItemSprite(Surface* dest, const Mod* mod, const SavedBattleGame *_save, int frame);
	/// Cleans up the ItemSprite.
	~ItemSprite();
	/// Draws the item.
	void draw(const BattleItem* item, int x, int y, int shade);
	/// Draws the item shadow.
	void drawShadow(const BattleItem* item, int x, int y);
};

/// Hi-res helpers for item pictures (FLOOROB / HANDOB on the map, BIGOBS in the UI), battle-14.
namespace HiResItem
{
	/// Armor colour-swap pairs of a unit (face, hair, utile, rank: group<<4 -> colour), the same table the
	/// default recolorUnitSprite uses (HiResLayer battleExpected / rampTint).
	void unitRecolor(const BattleUnit *unit, Uint8 from[4], Uint8 to[4]);
	/// True when the item rule has its own (non-default) recolorItemSprite script (warned once per item).
	bool customScript(const BattleItem *item);
	/// Colour-swap pairs of an item: no script = the pairs of its unit (corpse / carrier), custom script = none.
	void recolor(const BattleItem *item, Uint8 from[4], Uint8 to[4]);
	/// Records a map draw of an item frame (after the 8bpp blit): a 32-bit picture is drawn shaded and
	/// recoloured at output resolution, an 8bpp frame becomes a filtered 8bpp sprite command.
	void mapDraw(const Surface *frame, Surface *dest, int x, int y, int shade, const BattleItem *item,
		int clipX = 0, int clipY = 0, int clipW = 0, int clipH = 0);
	/// Brightness (0..256+, 256 = unchanged) a custom recolorItemSprite script gives an item frame, measured
	/// on the 8bpp proxy (palette luminance of the scripted vs the plain pixels); 256 without a custom script.
	int scriptBrightness(const Surface *frame, const BattleItem *item, const SavedBattleGame *save, int part, int animFrame);
}

} //namespace OpenXcom
