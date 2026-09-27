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
#include <vector>
#include <string>
#include <SDL.h>
#include "LocalizedText.h"

namespace OpenXcom
{

class Game;
class Surface;
class InteractiveSurface;
class Window;
class Action;
class SavedBattleGame;
class RuleInterface;
class Sound;
class ScaledPanel;

enum SoldierGender : char;

/**
 * A game state that receives user input and reacts accordingly.
 * Game states typically represent a whole window or screen that
 * the user interacts with, making the game... well, interactive.
 * They automatically handle child elements used to transmit
 * information from/to the user, and are linked to the core game
 * engine which manages them.
 */
class State
{
	friend class Timer;

protected:
	static Game *_game;
	std::vector<Surface*> _surfaces;
	std::vector<Surface*> _surfacesOwned;
	bool _screen;
	bool _soundPlayed;
	InteractiveSurface *_modal;
	RuleInterface *_ruleInterface;
	RuleInterface *_ruleInterfaceParent;
	const Sound* _customSound;

	SDL_Color _palette[256];
	Uint8 _cursorColor;
	/// Geoscape scale "x1, UI optimized": the state's 320x200 layout is drawn on a virtual canvas
	/// magnified by the sidebar factor (text still rendered by HiResLayer at the output resolution).
	ScaledPanel *_uiPanel;
	bool _uiCanvasExempt;
	/// Partial canvas: only these surfaces are magnified (the others are drawn/handled as usual).
	bool _uiPartial, _uiPartialTop;
	std::vector<Surface*> _uiPartialMembers;
	/// Corner canvas: these surfaces (laid out from the top-left screen corner) are magnified from that corner.
	ScaledPanel *_uiCornerPanel;
	std::vector<Surface*> _uiCornerMembers;
	int _uiCornerW, _uiCornerH;
	/// Which canvas a surface belongs to: 0 = none (direct), 1 = main, 2 = corner.
	int uiCanvasOf(const Surface *surface) const;
	/// Creates/updates the UI canvas for a factor (returns false if it does not fit).
	bool prepareUiPanel(int k);
public:
	/// Creates a new state linked to a game.
	State();
	/// Cleans up the state.
	virtual ~State();
	/// Set interface rules.
	void setInterface(const std::string &s, bool alterPal = false, SavedBattleGame *battleGame = 0);
	/// Set window background.
	void setWindowBackground(Window *window, const std::string &s);
	/// Set window background by image name (instead of by interface name).
	void setWindowBackgroundImage(Window* window, const std::string& bgImageName);
	/// Add a optional child element but it will not be displayed.
	template<typename T>
	T* preAdd(T *surface)
	{
		static_assert(std::is_base_of_v<Surface, T>, "Type need to be surface");
		preAdd(static_cast<Surface*>(surface));
		return surface;
	}
	/// Add a optional child element but it will not be displayed.
	void preAdd(Surface *surface);
	/// Adds a child element to the state.
	void add(Surface *surface);
	/// Adds a child element to the state.
	void add(Surface *surface, const std::string &id, const std::string &category, Surface *parent = 0);
	/// Gets whether the state is a full-screen.
	bool isScreen() const;
	/// Toggles whether the state is a full-screen.
	void toggleScreen();
	/// Initializes the state.
	virtual void init();
	/// Handles any events.
	virtual void handle(Action *action);
	/// Runs state functionality every cycle.
	virtual void think();
	/// Blits the state to the screen.
	virtual void blit();
	/// Hides all the state surfaces.
	void hideAll();
	/// Shows all the state surfaces.
	void showAll();
	/// Resets all the state surfaces.
	void resetAll();
	/// Get the localized text.
	LocalizedText tr(const std::string &id) const;
	/// Get the localized text.
	LocalizedText trAlt(const std::string &id, int alt) const;
	/// Get the localized text.
	LocalizedText tr(const std::string &id, unsigned n) const;
	/// Get the localized text.
	LocalizedText tr(const std::string &id, SoldierGender gender) const;
	/// redraw all the text-type surfaces.
	void redrawText();
	/// does the state only have one text list (to scroll)?
	bool hasOnlyOneScrollableTextList() const;
	/// center all surfaces relative to the screen.
	void centerAllSurfaces();
	/// lower all surfaces by half the screen height.
	void lowerAllSurfaces();
	/// switch the colours to use the battlescape palette.
	void applyBattlescapeTheme(const std::string& category);
	/// Sets game object pointer
	static void setGamePtr(Game* game);
	/// Sets a modal surface.
	void setModal(InteractiveSurface *surface);

	/// Changes a set of colors on the state's 8bpp palette.
	void setStatePalette(const SDL_Color *colors, int firstcolor = 0, int ncolors = 256);
	/// Changes a set of colors on the state's 8bpp palette of helper surfaces.
	void setModPalette();

	/// Changes the state's 8bpp palette with certain resources.
	void setStandardPalette(const std::string &palette, int backpals = -1);
	/// Changes the state's 8bpp palette with certain resources.
	void setCustomPalette(SDL_Color *colors, int cursorColor);

	/// Gets the state's 8bpp palette.
	SDL_Color *getPalette();

	/// Let the state know the window has been resized.
	virtual void resize(int &dX, int &dY);
	/// Re-orients all the surfaces in the state.
	virtual void recenter(int dX, int dY);

	/// Excludes this state from the UI canvas (states that lay themselves out on the whole screen).
	void setUiCanvasExempt(bool exempt = true);
	/// Magnifies only 'members' (e.g. the top bar of a globe overlay); with 'top' their 320x200 frame
	/// starts at the top of the screen instead of the vertical center.
	void setUiCanvasPartial(const std::vector<Surface*> &members, bool top);
	/// Magnifies 'members' (laid out in the w x h area at the top-left screen corner) from that corner.
	void setUiCanvasCorner(const std::vector<Surface*> &members, int w, int h);
	/// Magnification of this state's UI canvas (1 = drawn directly, as usual).
	int getUiCanvasFactor() const;
	/// The UI canvas factor states get at the current geoscape resolution (1 = off).
	static int uiCanvasFactorForGeoscape();

	/// Gets cursor X coordinate.
	int getCursorX() const;
	/// Gets cursor Y coordinate.
	int getCursorY() const;
};

}
