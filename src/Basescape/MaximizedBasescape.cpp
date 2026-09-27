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
#include "MaximizedBasescape.h"
#include "../Engine/Game.h"
#include "../Engine/Screen.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"

namespace OpenXcom
{

int MaximizedBasescape::_holders = 0;
bool MaximizedBasescape::_switched = false;

void MaximizedBasescape::enter(Game *game)
{
	if (_entered)
		return;
	_entered = true;
	_game = game;
	if (_holders++ == 0 && Options::maximizeInfoScreens &&
		(Options::baseXResolution != Screen::ORIGINAL_WIDTH || Options::baseYResolution != Screen::ORIGINAL_HEIGHT))
	{
		Options::baseXResolution = Screen::ORIGINAL_WIDTH;
		Options::baseYResolution = Screen::ORIGINAL_HEIGHT;
		_game->getScreen()->resetDisplay(false);
		_switched = true;
		Log(LOG_INFO) << "Basescape: maximized (base resolution 320x200)";
	}
}

void MaximizedBasescape::leave()
{
	if (!_entered)
		return;
	_entered = false;
	if (--_holders == 0 && _switched)
	{
		_switched = false;
		Screen::updateScale(Options::geoscapeScale, Options::baseXGeoscape, Options::baseYGeoscape, true);
		_game->getScreen()->resetDisplay(false);
		Log(LOG_INFO) << "Basescape: geoscape resolution restored (" << Options::baseXResolution << "x" << Options::baseYResolution << ")";
	}
}

}
