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
#include "ItemSprite.h"
#include "../Mod/Mod.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/RuleItem.h"
#include "../Mod/Armor.h"
#include "../Engine/HiResAssets.h"
#include "../Engine/Logger.h"
#include "../Engine/Script.h"
#include <set>

namespace OpenXcom
{

/**
 * Sets up a ItemSprite with the specified size and position.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
ItemSprite::ItemSprite(Surface* dest, const Mod* mod, const SavedBattleGame* save, int frame) :
	_itemSurface(const_cast<Mod*>(mod)->getSurfaceSet("FLOOROB.PCK")),
	_animationFrame(frame),
	_dest(dest),
	_save(save)
{

}

/**
 * Deletes the ItemSprite.
 */
ItemSprite::~ItemSprite()
{

}

/**
 * Draws a item, using the drawing rules of the item or unit if it's corpse.
 * This function is called by Map, for each item on the screen.
 */
void ItemSprite::draw(const BattleItem* item, int x, int y, int shade)
{
	const Surface* sprite = item->getFloorSprite(_itemSurface, _save, _animationFrame, shade);
	if (sprite)
	{
		ScriptWorkerBlit work;
		BattleItem::ScriptFill(&work, item, _save, BODYPART_ITEM_FLOOR, _animationFrame, shade);
		work.executeBlit(sprite, _dest, x, y, shade);
		if (HiResAssets::any())
			HiResItem::mapDraw(sprite, _dest, x, y, shade, item);
	}
}

/**
 * Draws shadow of item.
 */
void ItemSprite::drawShadow(const BattleItem* item, int x, int y)
{
	const Surface* sprite = item->getFloorSprite(_itemSurface, _save, _animationFrame, 16);
	if (sprite)
	{
		sprite->blitNShade(_dest, x, y, 16);
		if (HiResAssets::any())
			HiResItem::mapDraw(sprite, _dest, x, y, 16, nullptr);
	}
}

namespace HiResItem
{

void unitRecolor(const BattleUnit *unit, Uint8 from[4], Uint8 to[4])
{
	for (int k = 0; k < 4; ++k) { from[k] = 0; to[k] = 0; }
	if (!unit)
		return;
	const Armor *armor = unit->getArmor();
	const int groups[4] = { armor->getFaceColorGroup(), armor->getHairColorGroup(), armor->getUtileColorGroup(), armor->getRankColorGroup() };
	for (const auto &pr : unit->getRecolor())
		for (int k = 0; k < 4; ++k)
			if (groups[k] > 0 && (groups[k] << 4) == pr.first && !from[k])
			{
				from[k] = pr.first;
				to[k] = pr.second;
				break;
			}
}

bool customScript(const BattleItem *item)
{
	if (!item)
		return false;
	const auto &scr = item->getRules()->getScript<ModScript::RecolorItemSprite>();
	if (!scr || scr.isDefaultCode())
		return false;
	static std::set<const RuleItem*> warned;
	if (warned.insert(item->getRules()).second)
		Log(LOG_WARNING) << "HiResAssets: item " << item->getRules()->getType() << " has a custom recolorItemSprite script: its 32-bit pictures are drawn without recolour (shade / brightness only)";
	return true;
}

void recolor(const BattleItem *item, Uint8 from[4], Uint8 to[4])
{
	for (int k = 0; k < 4; ++k) { from[k] = 0; to[k] = 0; }
	if (!item)
		return;
	if (item->getRules()->getScript<ModScript::RecolorItemSprite>())
		return; // own script: default code = no unit recolour; custom = shade only (customScript warns)
	unitRecolor(item->getUnit(), from, to);
}

void mapDraw(const Surface *frame, Surface *dest, int x, int y, int shade, const BattleItem *item, int clipX, int clipY, int clipW, int clipH)
{
	if (!frame || !dest)
		return;
	const SDL_Surface *ps = const_cast<Surface*>(frame)->getSurface();
	if (HiResAssets::proxyByPixels(ps->pixels) == ps)
	{
		Uint8 from[4], to[4];
		if (customScript(item))
		{
			for (int k = 0; k < 4; ++k) { from[k] = 0; to[k] = 0; }
		}
		else
		{
			recolor(item, from, to);
		}
		HiResAssets::onBattleDraw(ps, dest->getSurface(), x, y, shade, false, 0, from, to, clipX, clipY, clipW, clipH);
	}
	else
	{
		HiResAssets::battleMark8(dest->getSurface(), (const Uint8*)ps->pixels, ps->w, ps->h, ps->pitch, x, y, false, clipX, clipY, clipW, clipH);
	}
}

int scriptBrightness(const Surface *frame, const BattleItem *item, const SavedBattleGame *save, int part, int animFrame)
{
	if (!frame || !item || !customScript(item))
		return 256;
	SDL_Surface *ps = const_cast<Surface*>(frame)->getSurface();
	const SDL_Color *pal = HiResAssets::battlePalette();
	if (!pal && ps->format->palette)
		pal = ps->format->palette->colors;
	if (!pal)
		return 256;
	Surface tmp(ps->w, ps->h, 0, 0);
	tmp.clear();
	ScriptWorkerBlit work;
	BattleItem::ScriptFill(&work, item, save, part, animFrame, 0);
	tmp.lock();
	work.executeBlit(frame, &tmp, 0, 0, 0);
	tmp.unlock();
	auto lum = [&](Uint8 i) { return 299 * pal[i].r + 587 * pal[i].g + 114 * pal[i].b; };
	long long a = 0, b = 0;
	const SDL_Surface *ts = tmp.getSurface();
	for (int y = 0; y < ps->h; ++y)
	{
		const Uint8 *sp = (const Uint8*)ps->pixels + y * ps->pitch;
		const Uint8 *tp = (const Uint8*)ts->pixels + y * ts->pitch;
		for (int x = 0; x < ps->w; ++x)
			if (sp[x])
			{
				a += lum(sp[x]);
				b += tp[x] ? lum(tp[x]) : 0;
			}
	}
	if (a <= 0)
		return 256;
	return (int)std::max(0LL, std::min(512LL, (b * 256 + a / 2) / a));
}

}

} //namespace OpenXcom
