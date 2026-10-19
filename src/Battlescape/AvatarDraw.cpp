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
#include "AvatarDraw.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include "../Engine/Surface.h"
#include "../Engine/HiResAssets.h"
#include "../Engine/HiResCanvas.h"
#include "../Engine/HiResLayer.h"
#include "../Engine/Screen.h"
#include <cmath>
#include "../Mod/Mod.h"
#include "../Engine/Palette.h"
#include <climits>
#include <cstdint>
#include "../Mod/Armor.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/Soldier.h"

namespace OpenXcom
{

namespace AvatarDraw
{

namespace
{
/// The paperdoll pieces of the avatar, back to front (the stock battlescape lookup).
std::vector<Surface*> pieces(Mod *mod, Soldier *soldier)
{
	std::vector<Surface*> out;
	Armor *customArmor = soldier->getArmor();
	if (!soldier->getRules()->getArmorForAvatar().empty())
	{
		customArmor = mod->getArmor(soldier->getRules()->getArmorForAvatar());
	}
	if (customArmor->hasLayersDefinition())
	{
		for (const auto& layer : soldier->getArmorLayers(customArmor))
		{
			out.push_back(mod->getSurface(layer, true));
		}
		return out;
	}
	std::string look = soldier->getArmor()->getSpriteInventory();
	if (!soldier->getRules()->getArmorForAvatar().empty())
	{
		look = mod->getArmor(soldier->getRules()->getArmorForAvatar())->getSpriteInventory();
	}
	const std::string gender = soldier->getGender() == GENDER_MALE ? "M" : "F";
	std::stringstream ss;
	Surface *surf = 0;
	for (int i = 0; i <= RuleSoldier::LookVariantBits; ++i)
	{
		ss.str("");
		ss << look;
		ss << gender;
		ss << (int)soldier->getLook() + (soldier->getLookVariant() & (RuleSoldier::LookVariantMask >> i)) * 4;
		ss << ".SPK";
		surf = mod->getSurface(ss.str(), false);
		if (surf)
		{
			break;
		}
	}
	if (!surf)
	{
		ss.str("");
		ss << look;
		ss << ".SPK";
		surf = mod->getSurface(ss.str(), false);
	}
	if (!surf)
	{
		surf = mod->getSurface(look, true);
	}
	out.push_back(surf);
	return out;
}
}

bool enabled(Mod *mod)
{
	return mod->getSurface("AvatarBackground", false) != 0;
}

void draw8(Mod *mod, Soldier *soldier, Surface *dst, int x, int y)
{
	Surface *customBg = mod->getSurface("AvatarBackground", false);
	if (!customBg || !soldier)
		return;
	// a larger (hi-res) background is fitted into the 26x23 slot
	HiResAssets::fit(customBg, WIDTH, HEIGHT)->blitNShade(dst, x, y);
	for (Surface *surf : pieces(mod, soldier))
	{
		SurfaceCrop crop = surf->getCrop();
		crop.getCrop()->x = soldier->getRules()->getAvatarOffsetX();
		crop.getCrop()->y = soldier->getRules()->getAvatarOffsetY();
		crop.getCrop()->w = WIDTH;
		crop.getCrop()->h = HEIGHT;
		crop.setX(crop.getX() + x);
		crop.setY(crop.getY() + y);
		crop.blit(dst);
	}
}

void blitScaled8(Surface *src, Surface *dst, int x, int y, int w, int h)
{
	SDL_Surface *s = src->getSurface(), *d = dst->getSurface();
	if (!s || !d || w <= 0 || h <= 0 || s->w <= 0 || s->h <= 0)
		return;
	SDL_LockSurface(s);
	SDL_LockSurface(d);
	for (int yy = 0; yy < h; ++yy)
	{
		const int dy = y + yy;
		if (dy < 0 || dy >= d->h)
			continue;
		const Uint8 *srow = (const Uint8*)s->pixels + (size_t)(yy * s->h / h) * s->pitch;
		Uint8 *drow = (Uint8*)d->pixels + (size_t)dy * d->pitch;
		for (int xx = 0; xx < w; ++xx)
		{
			const int dx = x + xx;
			const Uint8 p = srow[xx * s->w / w];
			if (dx >= 0 && dx < d->w && p)
				drow[dx] = p;
		}
	}
	SDL_UnlockSurface(d);
	SDL_UnlockSurface(s);
}

void draw8ForPalette(Mod *mod, Soldier *soldier, Surface *dst, int x, int y, const SDL_Color *dstPalette)
{
	Palette *battle = mod->getPalette("PAL_BATTLESCAPE", false);
	if (!battle || !dstPalette)
	{
		draw8(mod, soldier, dst, x, y);
		return;
	}
	const SDL_Color *bp = battle->getColors();
	Uint8 lut[256];
	lut[0] = 0;
	for (int i = 1; i < 256; ++i)
	{
		int best = 1, bestD = INT32_MAX;
		for (int j = 1; j < 256; ++j)
		{
			const int dr = bp[i].r - dstPalette[j].r, dg = bp[i].g - dstPalette[j].g, db = bp[i].b - dstPalette[j].b;
			const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
			if (d < bestD) { bestD = d; best = j; }
		}
		lut[i] = (Uint8)best;
	}
	Surface tmp(WIDTH, HEIGHT, 0, 0);
	tmp.setPalette(bp);
	draw8(mod, soldier, &tmp, 0, 0);
	SDL_Surface *s = tmp.getSurface(), *d = dst->getSurface();
	SDL_LockSurface(s);
	SDL_LockSurface(d);
	for (int yy = 0; yy < HEIGHT; ++yy)
	{
		if (y + yy < 0 || y + yy >= d->h)
			continue;
		const Uint8 *sr = (const Uint8*)s->pixels + yy * s->pitch;
		Uint8 *dr = (Uint8*)d->pixels + (y + yy) * d->pitch;
		for (int xx = 0; xx < WIDTH; ++xx)
			if (sr[xx] && x + xx >= 0 && x + xx < d->w)
				dr[x + xx] = lut[sr[xx]];
	}
	SDL_UnlockSurface(d);
	SDL_UnlockSurface(s);
}

void draw8Scaled(Mod *mod, Soldier *soldier, Surface *dst, int x, int y, int w, int h)
{
	Surface tmp(WIDTH, HEIGHT, 0, 0);
	tmp.setPalette(dst->getPalette());
	draw8(mod, soldier, &tmp, 0, 0);
	blitScaled8(&tmp, dst, x, y, w, h);
}

void drawNative(Mod *mod, Soldier *soldier, HiResCanvas &canvas, int lx, int ly)
{
	Surface *customBg = mod->getSurface("AvatarBackground", false);
	if (!customBg || !soldier)
		return;
	canvas.pushClip(lx, ly, WIDTH, HEIGHT);
	// background first, filling the whole slot at output resolution (never black behind the doll)
	const int pw = canvas.getOutputWidth(), ph = canvas.getOutputHeight();
	const Uint32 *bg = nullptr;
	int bw = 0, bh = 0;
	if (HiResCanvas::hasPicture(customBg, &bw, &bh))
	{
		SDL_Surface *s = customBg->getSurface();
		bg = HiResAssets::nativeRendered(s, s->format->palette, pw, ph);
	}
	if (bg)
		canvas.drawPicture(bg, pw, ph, lx, ly, WIDTH, HEIGHT);
	else
		canvas.drawSurface(HiResAssets::fit(customBg, WIDTH, HEIGHT), lx, ly);
	// the pieces: whole paperdoll drawn at (-avatarOffsetX, -avatarOffsetY), clipped to the slot
	const int offX = soldier->getRules()->getAvatarOffsetX(), offY = soldier->getRules()->getAvatarOffsetY();
	for (Surface *surf : pieces(mod, soldier))
	{
		canvas.drawSurface(surf, lx - offX + surf->getX(), ly - offY + surf->getY());
	}
	canvas.popClip();
}

Surface *flag(Mod *mod, Soldier *soldier, int boxW, int boxH)
{
	if (!soldier)
		return nullptr;
	std::ostringstream flagId;
	flagId << "Flag";
	const std::vector<int> mapping = mod->getFlagByKills();
	if (mapping.empty())
	{
		flagId << soldier->getNationality() + soldier->getRules()->getFlagOffset();
	}
	else
	{
		int index = 0;
		for (int item : mapping)
		{
			if (soldier->getKills() <= item)
			{
				break;
			}
			index++;
		}
		flagId << index + soldier->getRules()->getFlagOffset();
	}
	return HiResAssets::fit(mod->getSurface(flagId.str().c_str(), false), boxW, boxH);
}

bool nativeActive()
{
	return HiResLayer::recording() && HiResAssets::enabled();
}

bool presentNative(const Screen *screen, HiResCanvas *&canvas, Uint64 &lastKey, Uint64 key, Surface *holder, int vw, int vh, bool force,
	const std::function<void(HiResCanvas&)> &draw, SDL_Palette *artPalette)
{
	if (!holder || !nativeActive())
		return false;
	// the screen's scale (output px per base px); the layer's composition buffer size is the same
	// but only known once it composed a frame
	double sx = screen ? screen->getXScale() : 0.0, sy = screen ? screen->getYScale() : 0.0;
	if (sx <= 0 || sy <= 0)
	{
		if (!HiResLayer::outputScale(sx, sy))
			return false;
	}
	const int hw = holder->getWidth(), hh = holder->getHeight();
	key = (key ^ (Uint64)std::lround(sx * 1000)) * 1099511628211ull;
	key = (key ^ (Uint64)std::lround(sy * 1000)) * 1099511628211ull;
	key = (key ^ (Uint64)(hw * 65536 + hh)) * 1099511628211ull;
	const bool changed = key != lastKey || !canvas;
	if (changed)
	{
		if (!canvas)
			canvas = new HiResCanvas();
		lastKey = key;
		canvas->begin(0, 0, vw, vh, sx * hw / vw, sy * hh / vh);
		canvas->setPalette(artPalette ? artPalette : holder->getSurface()->format->palette);
		draw(*canvas);
	}
	if (changed || force)
	{
		HiResLayer::clearSurface(holder->getSurface()); // e.g. the 8bpp background's own asset command
		canvas->present(holder->getSurface(), 0, 0, hw, hh);
	}
	return true;
}

Uint64 contentKey(Mod *mod, Soldier *soldier)
{
	Uint64 h = 1469598103934665603ull;
	auto mix = [&](Uint64 v) { h = (h ^ v) * 1099511628211ull; };
	mix((Uint64)(size_t)soldier);
	if (!soldier)
		return h;
	mix((Uint64)(size_t)soldier->getArmor());
	mix((Uint64)soldier->getLook());
	mix((Uint64)soldier->getLookVariant());
	mix((Uint64)soldier->getGender());
	for (Surface *s : pieces(mod, soldier))
		mix((Uint64)(size_t)s);
	mix((Uint64)(size_t)mod->getSurface("AvatarBackground", false));
	return h;
}

}

}
