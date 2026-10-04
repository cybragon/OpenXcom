#pragma once
/*
 * Copyright 2010-2019 OpenXcom Developers.
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
#include "OptionsBaseState.h"
#include <vector>
#include <string>

namespace OpenXcom
{

class Text;
class ComboBox;
class ToggleTextButton;

/**
 * Options tab for the hi-res (TTF) text overlay: master switch and, per game
 * font type (Font.dat id), the system font and a size adjustment.
 * Changes apply immediately (live), Cancel reverts them.
 */
class OptionsFontsState : public OptionsBaseState
{
private:
	static const int MAX_ROWS = 5;
	Text *_txtHiRes, *_txtHeadType, *_txtHeadFont, *_txtHeadSize;
	ToggleTextButton *_btnHiRes;
	std::vector<Text*> _txtRow;
	std::vector<ComboBox*> _cbxFont, _cbxSize;
	std::vector<std::string> _rowIds;    ///< "" = default font (all types)
	std::vector<std::string> _specs;     ///< combo index -> font spec ("" = automatic / inherit)
	std::vector<int> _sizes;             ///< combo index -> size %
public:
	/// Creates the Fonts Options state.
	OptionsFontsState(OptionsOrigin origin);
	/// Cleans up the Fonts Options state.
	~OptionsFontsState();
	/// Master switch.
	void btnHiResClick(Action *action);
	/// Font or size changed.
	void cbxChange(Action *action);
};

}
