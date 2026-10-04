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
#include "OptionsFontsState.h"
#include <algorithm>
#include <cstdlib>
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/Screen.h"
#include "../Engine/Action.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/HiResFont.h"
#include "../Engine/HiResLayer.h"
#include "../Mod/Mod.h"
#include "../Interface/Text.h"
#include "../Interface/ComboBox.h"
#include "../Interface/ToggleTextButton.h"

namespace OpenXcom
{

namespace
{
std::string fontTypeLabel(const std::string &id)
{
	if (id.empty()) return "STR_HIRES_FONT_DEFAULT";
	if (id == "FONT_BIG") return "STR_HIRES_FONT_BIG";
	if (id == "FONT_SMALL") return "STR_HIRES_FONT_SMALL";
	if (id == "FONT_GEO_BIG") return "STR_HIRES_FONT_GEO_BIG";
	if (id == "FONT_GEO_SMALL") return "STR_HIRES_FONT_GEO_SMALL";
	return id;
}
}

/**
 * Initializes all the elements in the Fonts Options screen.
 * @param origin Game section that originated this state.
 */
OptionsFontsState::OptionsFontsState(OptionsOrigin origin) : OptionsBaseState(origin)
{
	setCategory(_btnFonts);

	// rows: default font (all types) + one per Font.dat id (first MAX_ROWS - 1)
	_rowIds.push_back("");
	for (const auto &id : HiResLayer::fontIds())
	{
		if ((int)_rowIds.size() >= MAX_ROWS) break;
		_rowIds.push_back(id);
	}

	_txtHiRes = new Text(108, 16, 94, 8);
	_btnHiRes = new ToggleTextButton(104, 16, 206, 8);
	_txtHeadType = new Text(56, 9, 94, 28);
	_txtHeadFont = new Text(116, 9, 152, 28);
	_txtHeadSize = new Text(42, 9, 270, 28);
	for (size_t i = 0; i < _rowIds.size(); ++i)
	{
		int y = 38 + (int)i * 18;
		_txtRow.push_back(new Text(56, 16, 94, y));
		_cbxFont.push_back(new ComboBox(this, 116, 16, 152, y));
		_cbxSize.push_back(new ComboBox(this, 42, 16, 270, y));
	}

	add(_txtHiRes, "text", "videoMenu");
	add(_btnHiRes, "button", "videoMenu");
	add(_txtHeadType, "text", "videoMenu");
	add(_txtHeadFont, "text", "videoMenu");
	add(_txtHeadSize, "text", "videoMenu");
	for (size_t i = 0; i < _rowIds.size(); ++i)
		add(_txtRow[i], "text", "videoMenu");
	// lower rows first, so the drop-down lists of upper rows are drawn over them
	for (size_t i = _rowIds.size(); i-- > 0; )
	{
		add(_cbxSize[i], "button", "videoMenu");
		add(_cbxFont[i], "button", "videoMenu");
	}
	centerAllSurfaces();

	_txtHiRes->setText(tr("STR_HIRES_TEXT"));
	_txtHiRes->setVerticalAlign(ALIGN_MIDDLE);
	_txtHiRes->setWordWrap(true);
	_btnHiRes->setText(Options::oxceHiResOverlay ? tr("STR_ON_UC") : tr("STR_OFF_UC"));
	_btnHiRes->setPressed(Options::oxceHiResOverlay);
	_btnHiRes->setTooltip("STR_HIRES_TEXT_DESC");
	_btnHiRes->onMouseClick((ActionHandler)&OptionsFontsState::btnHiResClick);
	_btnHiRes->onMouseIn((ActionHandler)&OptionsFontsState::txtTooltipIn);
	_btnHiRes->onMouseOut((ActionHandler)&OptionsFontsState::txtTooltipOut);

	_txtHeadType->setText(tr("STR_HIRES_FONT_TYPE"));
	_txtHeadFont->setText(tr("STR_HIRES_FONT_FACE"));
	_txtHeadSize->setText(tr("STR_HIRES_FONT_SIZE"));

	// system fonts (scanned once per session)
	const bool korean = Options::language.compare(0, 2, "ko") == 0;
	std::vector<std::string> names;
	_specs.push_back("");
	names.push_back(""); // filled per row
	for (const auto &info : HiResFont::enumerateSystemFonts())
	{
		std::string name = (korean && !info.familyKo.empty()) ? info.familyKo : info.family;
		if (!info.style.empty() && info.style != "Regular" && info.style != "Normal" && info.style != "Book")
			name += " " + info.style;
		names.push_back(name);
		_specs.push_back(info.spec);
	}
	std::vector<std::string> sizeNames;
	for (int s = 60; s <= 160; s += 10)
	{
		_sizes.push_back(s);
		sizeNames.push_back(std::to_string(s) + "%");
	}

	for (size_t i = 0; i < _rowIds.size(); ++i)
	{
		const std::string &id = _rowIds[i];
		std::string spec;
		int size = 100;
		if (id.empty())
		{
			spec = Options::oxceHiResFont;
			size = Options::oxceHiResFontSize;
		}
		else
		{
			HiResLayer::getFontSetting(id, spec, size);
		}
		// row label, drawn with the game font it configures
		_txtRow[i]->setText(tr(fontTypeLabel(id)));
		_txtRow[i]->setVerticalAlign(ALIGN_MIDDLE);
		if (!id.empty() && HiResLayer::recording())
		{
			// (bitmap geoscape fonts have almost no Hangul, so only with the overlay on)
			if (Font *f = _game->getMod()->getFont(id, false))
				_txtRow[i]->initText(f, f, _game->getLanguage());
		}
		std::vector<std::string> rowNames = names;
		rowNames[0] = tr(id.empty() ? "STR_HIRES_FONT_AUTO" : "STR_HIRES_FONT_INHERIT");
		size_t sel = 0;
		if (!spec.empty())
		{
			auto it = std::find(_specs.begin(), _specs.end(), spec);
			if (it == _specs.end() && spec.find(';') == std::string::npos)
			{
				// same font given another way (absolute path, other slashes/case): match by resolved file
				int idx = 0;
				std::string want = HiResFont::normalizePath(HiResFont::resolvePath(spec, &idx));
				const auto &fonts = HiResFont::enumerateSystemFonts();
				for (size_t k = 0; !want.empty() && k < fonts.size() && k + 1 < _specs.size(); ++k)
				{
					if (fonts[k].index == idx && HiResFont::normalizePath(fonts[k].path) == want)
					{
						it = _specs.begin() + k + 1;
						break;
					}
				}
			}
			if (it == _specs.end())
			{
				// custom value from options.cfg (path, list...): keep it selectable
				// show just the file name(s) of a custom path
				std::string shown = spec;
				size_t slash = shown.find_last_of("/\\");
				if (slash != std::string::npos && spec.find(';') == std::string::npos)
					shown = shown.substr(slash + 1);
				_specs.push_back(spec);
				names.push_back(shown);
				rowNames.push_back(shown);
				sel = _specs.size() - 1;
			}
			else
			{
				sel = it - _specs.begin();
			}
		}
		_cbxFont[i]->setOptions(rowNames);
		_cbxFont[i]->setSelected(sel);
		_cbxFont[i]->onChange((ActionHandler)&OptionsFontsState::cbxChange);
		_cbxFont[i]->setTooltip(id.empty() ? "STR_HIRES_FONT_DEFAULT_DESC" : "STR_HIRES_FONT_TYPE_DESC");
		_cbxFont[i]->onMouseIn((ActionHandler)&OptionsFontsState::txtTooltipIn);
		_cbxFont[i]->onMouseOut((ActionHandler)&OptionsFontsState::txtTooltipOut);

		size_t ssel = std::min_element(_sizes.begin(), _sizes.end(), [size](int a, int b) { return std::abs(a - size) < std::abs(b - size); }) - _sizes.begin();
		_cbxSize[i]->setOptions(sizeNames);
		_cbxSize[i]->setSelected(ssel);
		_cbxSize[i]->onChange((ActionHandler)&OptionsFontsState::cbxChange);
		_cbxSize[i]->setTooltip("STR_HIRES_FONT_SIZE_DESC");
		_cbxSize[i]->onMouseIn((ActionHandler)&OptionsFontsState::txtTooltipIn);
		_cbxSize[i]->onMouseOut((ActionHandler)&OptionsFontsState::txtTooltipOut);
	}
}

/**
 *
 */
OptionsFontsState::~OptionsFontsState()
{

}

/**
 * Switches the hi-res text overlay on/off. Takes effect immediately: the display is
 * re-initialized if the software path needs a true-colour output, and this tab is rebuilt.
 * @param action Pointer to an action.
 */
void OptionsFontsState::btnHiResClick(Action *)
{
	Options::oxceHiResOverlay = _btnHiRes->getPressed();
	HiResLayer::reconfigure();
	_game->getScreen()->resetDisplay(false);
	_game->popState();
	_game->pushState(new OptionsFontsState(_origin));
}

/**
 * A font or size combo box changed: store it and re-configure the overlay (live).
 * @param action Pointer to an action.
 */
void OptionsFontsState::cbxChange(Action *)
{
	for (size_t i = 0; i < _rowIds.size(); ++i)
	{
		const std::string &spec = _specs[std::min(_cbxFont[i]->getSelected(), _specs.size() - 1)];
		int size = _sizes[std::min(_cbxSize[i]->getSelected(), _sizes.size() - 1)];
		if (_rowIds[i].empty())
		{
			Options::oxceHiResFont = spec;
			Options::oxceHiResFontSize = size;
		}
		else
		{
			HiResLayer::setFontSetting(_rowIds[i], spec, size);
		}
	}
	HiResLayer::reconfigure();
}

}
