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
#include "NumberText.h"
#include "../Engine/HiResLayer.h"
#include "../Engine/Options.h"
#include <sstream>
#include <string>

namespace OpenXcom
{

/**
 * Sets up a blank number text.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
NumberText::NumberText(int width, int height, int x, int y) : Surface(width, height, x, y), _value(0), _bordered(false), _color(0)
{
	_chars[0] = new Surface(3, 5);
	_chars[0]->lock();
	_chars[0]->setPixel(0, 0, 1);
	_chars[0]->setPixel(1, 0, 1);
	_chars[0]->setPixel(2, 0, 1);
	_chars[0]->setPixel(0, 1, 1);
	_chars[0]->setPixel(0, 2, 1);
	_chars[0]->setPixel(0, 3, 1);
	_chars[0]->setPixel(2, 1, 1);
	_chars[0]->setPixel(2, 2, 1);
	_chars[0]->setPixel(2, 3, 1);
	_chars[0]->setPixel(0, 4, 1);
	_chars[0]->setPixel(1, 4, 1);
	_chars[0]->setPixel(2, 4, 1);
	_chars[0]->unlock();

	_chars[1] = new Surface(3, 5);
	_chars[1]->lock();
	_chars[1]->setPixel(1, 0, 1);
	_chars[1]->setPixel(1, 1, 1);
	_chars[1]->setPixel(1, 2, 1);
	_chars[1]->setPixel(1, 3, 1);
	_chars[1]->setPixel(0, 4, 1);
	_chars[1]->setPixel(1, 4, 1);
	_chars[1]->setPixel(2, 4, 1);
	_chars[1]->setPixel(0, 1, 1);
	_chars[1]->unlock();

	_chars[2] = new Surface(3, 5);
	_chars[2]->lock();
	_chars[2]->setPixel(0, 0, 1);
	_chars[2]->setPixel(1, 0, 1);
	_chars[2]->setPixel(2, 0, 1);
	_chars[2]->setPixel(2, 1, 1);
	_chars[2]->setPixel(0, 2, 1);
	_chars[2]->setPixel(1, 2, 1);
	_chars[2]->setPixel(2, 2, 1);
	_chars[2]->setPixel(0, 3, 1);
	_chars[2]->setPixel(0, 4, 1);
	_chars[2]->setPixel(1, 4, 1);
	_chars[2]->setPixel(2, 4, 1);
	_chars[2]->unlock();

	_chars[3] = new Surface(3, 5);
	_chars[3]->lock();
	_chars[3]->setPixel(0, 0, 1);
	_chars[3]->setPixel(1, 0, 1);
	_chars[3]->setPixel(2, 0, 1);
	_chars[3]->setPixel(2, 1, 1);
	_chars[3]->setPixel(2, 2, 1);
	_chars[3]->setPixel(2, 3, 1);
	_chars[3]->setPixel(0, 2, 1);
	_chars[3]->setPixel(1, 2, 1);
	_chars[3]->setPixel(0, 4, 1);
	_chars[3]->setPixel(1, 4, 1);
	_chars[3]->setPixel(2, 4, 1);
	_chars[3]->unlock();

	_chars[4] = new Surface(3, 5);
	_chars[4]->lock();
	_chars[4]->setPixel(0, 0, 1);
	_chars[4]->setPixel(0, 1, 1);
	_chars[4]->setPixel(0, 2, 1);
	_chars[4]->setPixel(1, 2, 1);
	_chars[4]->setPixel(2, 0, 1);
	_chars[4]->setPixel(2, 1, 1);
	_chars[4]->setPixel(2, 2, 1);
	_chars[4]->setPixel(2, 3, 1);
	_chars[4]->setPixel(2, 4, 1);
	_chars[4]->unlock();

	_chars[5] = new Surface(3, 5);
	_chars[5]->lock();
	_chars[5]->setPixel(0, 0, 1);
	_chars[5]->setPixel(1, 0, 1);
	_chars[5]->setPixel(2, 0, 1);
	_chars[5]->setPixel(0, 1, 1);
	_chars[5]->setPixel(0, 2, 1);
	_chars[5]->setPixel(1, 2, 1);
	_chars[5]->setPixel(2, 2, 1);
	_chars[5]->setPixel(2, 3, 1);
	_chars[5]->setPixel(0, 4, 1);
	_chars[5]->setPixel(1, 4, 1);
	_chars[5]->setPixel(2, 4, 1);
	_chars[5]->unlock();

	_chars[6] = new Surface(3, 5);
	_chars[6]->lock();
	_chars[6]->setPixel(0, 0, 1);
	_chars[6]->setPixel(1, 0, 1);
	_chars[6]->setPixel(2, 0, 1);
	_chars[6]->setPixel(0, 1, 1);
	_chars[6]->setPixel(0, 2, 1);
	_chars[6]->setPixel(1, 2, 1);
	_chars[6]->setPixel(2, 2, 1);
	_chars[6]->setPixel(0, 3, 1);
	_chars[6]->setPixel(2, 3, 1);
	_chars[6]->setPixel(0, 4, 1);
	_chars[6]->setPixel(1, 4, 1);
	_chars[6]->setPixel(2, 4, 1);
	_chars[6]->unlock();

	_chars[7] = new Surface(3, 5);
	_chars[7]->lock();
	_chars[7]->setPixel(0, 0, 1);
	_chars[7]->setPixel(1, 0, 1);
	_chars[7]->setPixel(2, 0, 1);
	_chars[7]->setPixel(2, 1, 1);
	_chars[7]->setPixel(2, 2, 1);
	_chars[7]->setPixel(2, 3, 1);
	_chars[7]->setPixel(2, 4, 1);
	_chars[7]->unlock();

	_chars[8] = new Surface(3, 5);
	_chars[8]->lock();
	_chars[8]->setPixel(0, 0, 1);
	_chars[8]->setPixel(1, 0, 1);
	_chars[8]->setPixel(2, 0, 1);
	_chars[8]->setPixel(0, 1, 1);
	_chars[8]->setPixel(0, 2, 1);
	_chars[8]->setPixel(0, 3, 1);
	_chars[8]->setPixel(2, 1, 1);
	_chars[8]->setPixel(2, 2, 1);
	_chars[8]->setPixel(2, 3, 1);
	_chars[8]->setPixel(1, 2, 1);
	_chars[8]->setPixel(0, 4, 1);
	_chars[8]->setPixel(1, 4, 1);
	_chars[8]->setPixel(2, 4, 1);
	_chars[8]->unlock();

	_chars[9] = new Surface(3, 5);
	_chars[9]->lock();
	_chars[9]->setPixel(0, 0, 1);
	_chars[9]->setPixel(1, 0, 1);
	_chars[9]->setPixel(2, 0, 1);
	_chars[9]->setPixel(0, 1, 1);
	_chars[9]->setPixel(0, 2, 1);
	_chars[9]->setPixel(2, 1, 1);
	_chars[9]->setPixel(2, 2, 1);
	_chars[9]->setPixel(2, 3, 1);
	_chars[9]->setPixel(1, 2, 1);
	_chars[9]->setPixel(0, 4, 1);
	_chars[9]->setPixel(1, 4, 1);
	_chars[9]->setPixel(2, 4, 1);
	_chars[9]->unlock();

	for (int i = 0; i < 10; ++i)
	{
		_borderedChars[i] = new Surface(5, 7);
		// give it a border
		// this is the "darker" shade that goes in the corners.
		for (int j = 0; j <= 2; j += 2)
		{
			for (int k = 0; k <= 2; k += 2)
			{
				_chars[i]->blitNShade(_borderedChars[i], j, k, 11);
			}
		}
		// this is the "slightly darker" version that goes in four cardinals.
		for (int z = 0; z <= 2; z += 2)
		{
			_chars[i]->blitNShade(_borderedChars[i], z, 1, 8);
			_chars[i]->blitNShade(_borderedChars[i], 1, z, 8);
		}
		// and finally the number itself
		_chars[i]->blitNShade(_borderedChars[i], 1, 1, 0);
	}
}

/**
 *
 */
NumberText::~NumberText()
{
	for (int i = 0; i < 10; ++i)
	{
		delete _chars[i];
		delete _borderedChars[i];
	}
}

/**
 * Changes the value used to render the number.
 * @param value Number value.
 */
void NumberText::setValue(unsigned int value)
{
	_value = value;
	_redraw = true;
}

/**
 * Returns the value used to render the number.
 * @return Number value.
 */
unsigned int NumberText::getValue() const
{
	return _value;
}

/**
 * Changes the color used to render the number.
 * @param color Color value.
 */
void NumberText::setColor(Uint8 color)
{
	_color = color;
	_redraw = true;
}

/**
 * Returns the color used to render the number.
 * @return Color value.
 */
Uint8 NumberText::getColor() const
{
	return _color;
}

/**
 * Replaces a certain amount of colors in the number text palette.
 * @param colors Pointer to the set of colors.
 * @param firstcolor Offset of the first color to replace.
 * @param ncolors Amount of colors to replace.
 */
void NumberText::setPalette(const SDL_Color *colors, int firstcolor, int ncolors)
{
	Surface::setPalette(colors, firstcolor, ncolors);
	for (int i = 0; i < 10; ++i)
	{
		_chars[i]->setPalette(colors, firstcolor, ncolors);
		_borderedChars[i]->setPalette(colors, firstcolor, ncolors);
	}
}

/**
 * Draws all the digits in the number.
 */
void NumberText::draw()
{
	Surface::draw();
	std::ostringstream ss;
	ss << _value;
	std::string s = ss.str();
	// State members (_ttfDigits): own font type NUMBER_TEXT (HiResLayer fontRule), sized to the 5 px digit
	// cell, and the TTF digits replace the bitmap ones (both would show: they are not the same width).
	// Others (e.g. inventory stack numbers, composed with blitNShade): bitmap + slot-0 TTF as before.
	// every NumberText uses the bold digit font type BATTLE_DIGITS (oxceHiResDigitFont)
	const int numSlot = !(Options::oxceHiResText && HiResLayer::recording()) ? -1
		: HiResLayer::registerFontId(_bordered ? "BATTLE_DIGITS_BORDERED" : "BATTLE_DIGITS");
	const bool ttf = numSlot >= 0 && HiResLayer::canRenderGlyph('0', numSlot);
	if (!ttf || !_ttfDigits)
	{
		int x = 0;
		if (!_bordered)
		{
			for (std::string::iterator i = s.begin(); i != s.end(); ++i)
			{
				_chars[*i - '0']->blitNShade(this, x, 0);
				x += _chars[*i - '0']->getWidth() + 1;
			}
		}
		else
		{
			for (std::string::iterator i = s.begin(); i != s.end(); ++i)
			{
				_borderedChars[*i - '0']->blitNShade(this, x, 0);
				x += _chars[*i - '0']->getWidth() + 1; // no this isn't a typo, i want to use the same spacing regardless.
			}
		}
	}

	this->offset(_color);

	// hi-res text: prefer TTF digits over the 3x5 bitmap (palette-recolor still uses the 8bpp fallback).
	// 32-bit digit sheets are not used — NumberText relies on index offset(_color).
	if (ttf)
	{
		const Uint32 run = HiResLayer::newRun();
		int px = 0;
		for (char ch : s)
		{
			const int gw = _chars[ch - '0']->getWidth();
			HiResCmd cmd;
			cmd.kind = HiResCmdKind::Glyph;
			cmd.code = (UCode)ch;
			cmd.run = run;
			cmd.x = (Sint16)px;
			cmd.y = 0;
			cmd.w = (Sint16)gw;
			cmd.h = (Sint16)getHeight();
			cmd.cellH = (Sint16)getHeight();
			cmd.lineH = (Uint8)std::max(1, getHeight());
			// centre on the digit's own rows (the 3x5 digit sits at the top of a possibly taller surface,
			// e.g. the 15x12 visible-enemy boxes)
			cmd.inkTopL = 0;
			cmd.inkBotL = (Sint8)std::max(0, _chars[ch - '0']->getHeight() - 1);
			cmd.slot = (Uint8)numSlot;
			cmd.fillIdx = (Uint8)(_color + 1);
			cmd.lineIdx = (Uint8)(_color + 4);
			cmd.clipX = 0; cmd.clipY = 0;
			cmd.clipW = (Sint16)getWidth(); cmd.clipH = (Sint16)getHeight();
			HiResLayer::record(getSurface(), cmd);
			px += gw + 1;
		}
	}
}

void NumberText::setBordered(bool bordered)
{
	_bordered = bordered;
}

}
