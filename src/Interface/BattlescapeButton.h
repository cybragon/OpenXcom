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
#include "../Engine/InteractiveSurface.h"
#include <vector>

namespace OpenXcom
{

enum InversionType {INVERT_NONE, INVERT_CLICK, INVERT_TOGGLE};

/**
 * Regular image that works like a button.
 * Unlike the TextButton, this button doesn't draw
 * anything on its own. It takes an existing graphic and
 * treats it as a button, inverting colors when necessary.
 * This is necessary for special buttons like in the Geoscape.
 */
class BattlescapeButton : public InteractiveSurface
{
protected:
	Uint8 _color;
	BattlescapeButton **_group;
	bool _inverted;
	InversionType _toggleMode;
	Surface *_altSurface;
	/// hi-res: the parent pixels copied by State::add; while unpressed only the pixels that differ
	/// from them are drawn, so the (hi-res) parent shows through the button.
	std::vector<Uint8> _parentCopy, _lastPixels;
	Surface *_diffSurface = nullptr;
	bool _diffAny = false;
public:
	/// Creates a new image button with the specified size and position.
	BattlescapeButton(int width, int height, int x = 0, int y = 0);
	/// Cleans up the image button.
	virtual ~BattlescapeButton();
	/// Sets the image button's color.
	void setColor(Uint8 color) override;
	/// Gets the image button's color.
	Uint8 getColor() const;
	/// Sets the image button's group.
	void setGroup(BattlescapeButton **group);
	/// Special handling for mouse presses.
	void mousePress(Action *action, State *state) override;
	/// Special handling for mouse releases.
	void mouseRelease(Action *action, State *state) override;
	/// Invert a button explicitly either ON or OFF.
	void toggle(bool invert);
	/// Allows this button to be toggled on/off with a click.
	void allowToggleInversion();
	/// Allows this button to be toggled on when clicked, and off when released.
	void allowClickInversion();
	/// Sets up the "pressed" surface.
	void initSurfaces(Surface* custom = nullptr);
	/// Remembers the current pixels as the parent copy (hi-res: unchanged pixels are not drawn).
	void rememberParentCopy();
	/// Blits this surface onto another one.
	void blit(SDL_Surface *surface) override;
	/// Alters both versions of the button's X pos.
	void setX(int x) override;
	/// Alters both versions of the button's Y pos.
	void setY(int y) override;

};

}
