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

namespace OpenXcom
{

class TextEdit;

/**
 * Windows IME (IMM32) support for text fields (8.7.1-hires fork).
 * SDL 1.2 (windib) never calls TranslateMessage and ignores WM_IME_*, so the IME cannot compose.
 * This module subclasses the SDL window procedure, pre-pumps the message queue with TranslateMessage
 * for plain key-downs while a TextEdit has focus, shows the composition inline in that TextEdit and
 * delivers committed characters as SDL key-down events (unicode set). Outside text fields the IME
 * context is detached so hotkeys work in any IME mode.
 * On other platforms all functions are empty inlines: behaviour is unchanged.
 */
namespace Ime
{
#ifdef _WIN32
	/// Subclasses the current SDL window (call after every SDL_SetVideoMode / window recreation).
	void attach();
	/// Restores the SDL window procedure (before window recreation and on shutdown).
	void detach();
	/// Output geometry: scale of one base pixel and letterbox offset, in window client pixels.
	void setGeometry(double scaleX, double scaleY, int offsetX, int offsetY);
	/// Pumps the Windows message queue (TranslateMessage for WM_KEYDOWN while a text field has focus).
	void pump();
	/// A text edit got focus; allowIme = false for numeric fields (IME stays off).
	void focus(TextEdit *edit, bool allowIme);
	/// A text edit lost focus or is destroyed.
	void unfocus(TextEdit *edit);
	/// Called every frame by the focused text edit (only states on top think): IME is detached while it is covered.
	void think(TextEdit *edit);
	/// IME position (start of the composition) of the focused text edit in screen base pixels, fractional
	/// with hi-res text (laid out by the overlay); for the candidate window.
	void setCaret(TextEdit *edit, double x, double y, double h);
	/// Discards the IME's current composition (the text edit committed it itself).
	void cancelComposition(TextEdit *edit);
#else
	inline void attach() {}
	inline void detach() {}
	inline void setGeometry(double, double, int, int) {}
	inline void pump() {}
	inline void focus(TextEdit *, bool) {}
	inline void unfocus(TextEdit *) {}
	inline void think(TextEdit *) {}
	inline void setCaret(TextEdit *, double, double, double) {}
	inline void cancelComposition(TextEdit *) {}
#endif
}

}
