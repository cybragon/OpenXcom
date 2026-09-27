#pragma once
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

namespace OpenXcom
{

class Game;

/**
 * "Maximize info screens" for the basescape (Options::maximizeInfoScreens).
 *
 * While at least one holder is entered, the game (base) resolution is 320x200
 * (Screen::ORIGINAL_WIDTH/HEIGHT), so the basescape and every screen opened from it
 * fill the window. The switch happens once when entering the basescape and once when
 * going back to the geoscape; screens opened inside the basescape do not change it
 * (no flicker). Only the base resolution changes: the output resolution stays, so the
 * hi-res overlay (HiResLayer) keeps rendering at the real output resolution.
 */
class MaximizedBasescape
{
private:
	static int _holders;
	static bool _switched;
	Game *_game;
	bool _entered;
public:
	MaximizedBasescape() : _game(nullptr), _entered(false) { }
	~MaximizedBasescape() { leave(); }
	MaximizedBasescape(const MaximizedBasescape&) = delete;
	MaximizedBasescape &operator=(const MaximizedBasescape&) = delete;
	/// Enters the maximized basescape (the first holder switches the resolution, if the option is on).
	void enter(Game *game);
	/// Leaves it (the last holder restores the geoscape resolution).
	void leave();
	/// True while the basescape resolution is forced to 320x200.
	static bool active() { return _switched; }
};

}
