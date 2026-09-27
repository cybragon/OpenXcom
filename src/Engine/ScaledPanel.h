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
#include <vector>
#include <SDL.h>

namespace OpenXcom
{

class Surface;
class Action;

/**
 * A group of widgets laid out in a small "logical" space and shown magnified by an
 * integer factor at some place of the screen (e.g. the original 64x200 geoscape
 * sidebar on a high resolution geoscape).
 *
 * The member widgets keep drawing into their 8bpp surfaces at their original size.
 * Every frame they are blitted onto a logical canvas, which is magnified (nearest
 * neighbour) and blitted onto the screen. Hi-res overlay commands (HiResLayer) follow
 * the magnification: their geometry is scaled and their glyphs are rendered at
 * magnification x output resolution, so text stays sharp (nothing is rendered from
 * a magnified bitmap). Mouse actions are mapped back into the logical space.
 */
class ScaledPanel
{
private:
	Surface *_canvas, *_scaled;
	int _scale, _x, _y;
	std::vector<Surface*> _members;
	SDL_Color _palette[256];
	bool _paletteSet;
public:
	/// Creates a panel of logical size w x h, shown magnified 'scale' times at (x, y) on the screen.
	ScaledPanel(int w, int h, int scale, int x, int y);
	~ScaledPanel();
	ScaledPanel(const ScaledPanel&) = delete;
	ScaledPanel &operator=(const ScaledPanel&) = delete;
	/// Adds a member widget (positioned in logical coordinates).
	void add(Surface *surface);
	/// Is this widget a member?
	bool contains(const Surface *surface) const;
	/// Magnification factor.
	int getScale() const { return _scale; }
	/// Screen position of the magnified panel.
	int getX() const { return _x; }
	int getY() const { return _y; }
	/// Starts a frame: clears the logical canvas and syncs its palette.
	void beginFrame(const SDL_Color *palette);
	/// Draws a member onto the logical canvas.
	void blitMember(Surface *surface);
	/// Magnifies the canvas onto the screen.
	void present(SDL_Surface *screen);
	/// Maps a (mouse) action from screen space into the logical space of the panel.
	Action mapAction(const Action *action) const;
};

}
