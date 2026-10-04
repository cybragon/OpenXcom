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
#include "GameTime.h"
#include "../Engine/Language.h"
#include <iomanip>
#include <sstream>
#include <vector>

namespace OpenXcom
{

/**
 * Initializes a new ingame time with a certain starting point.
 * @param weekday Starting weekday.
 * @param day Starting day.
 * @param month Starting month.
 * @param year Starting year.
 * @param hour Starting hour.
 * @param minute Starting minute.
 * @param second Starting second.
 */
GameTime::GameTime(int weekday, int day, int month, int year, int hour, int minute, int second) : _second(second), _minute(minute), _hour(hour), _weekday(weekday), _day(day), _month(month), _year(year)
{
}

/**
 *
 */
GameTime::~GameTime()
{
}

/**
 * Loads the time from a YAML file.
 * @param node YAML node.
 */
void GameTime::load(const YAML::YamlNodeReader& reader)
{
	reader.tryRead("second", _second);
	reader.tryRead("minute", _minute);
	reader.tryRead("hour", _hour);
	reader.tryRead("weekday", _weekday);
	reader.tryRead("day", _day);
	reader.tryRead("month", _month);
	reader.tryRead("year", _year);
}

/**
 * Saves the time to a YAML file.
 * @return YAML node.
 */
void GameTime::save(YAML::YamlNodeWriter writer) const
{
	writer.setAsMap();
	writer.setFlowStyle();
	writer.write("second", _second);
	writer.write("minute", _minute);
	writer.write("hour", _hour);
	writer.write("weekday", _weekday);
	writer.write("day", _day);
	writer.write("month", _month);
	writer.write("year", _year);
}

bool GameTime::isLastDayOfMonth()
{
	int monthDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	// Leap year
	if ((_year % 4 == 0) && !(_year % 100 == 0 && _year % 400 != 0))
		monthDays[1]++;
	return _day == monthDays[_month - 1];
}

/**
 * Advances the ingame time by 5 seconds, automatically correcting
 * the other components when necessary and sending out a trigger when
 * a certain time span has elapsed for time-dependent events.
 * @return Time span trigger.
 */
TimeTrigger GameTime::advance()
{
	TimeTrigger trigger = TIME_5SEC;
	int monthDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	// Leap year
	if ((_year % 4 == 0) && !(_year % 100 == 0 && _year % 400 != 0))
		monthDays[1]++;

	_second += 5;

	if (_second >= 60)
	{
		_minute++;
		_second = 0;
		if (_minute % 10 == 0)
		{
			trigger = TIME_10MIN;
		}
		if (_minute % 30 == 0)
		{
			trigger = TIME_30MIN;
		}
	}
	if (_minute >= 60)
	{
		_hour++;
		_minute = 0;
		trigger = TIME_1HOUR;
	}
	if (_hour >= 24)
	{
		_day++;
		_weekday++;
		_hour = 0;
		trigger = TIME_1DAY;
	}
	if (_weekday > 7)
	{
		_weekday = 1;
	}
	if (_day > monthDays[_month - 1])
	{
		_day = 1;
		_month++;
		trigger = TIME_1MONTH;
	}
	if (_month > 12)
	{
		_month = 1;
		_year++;
	}

	return trigger;
}

/**
 * Returns the current ingame second.
 * @return Second (0-59).
 */
int GameTime::getSecond() const
{
	return _second;
}

/**
 * Returns the current ingame minute.
 * @return Minute (0-59).
 */
int GameTime::getMinute() const
{
	return _minute;
}

/**
 * Returns the current ingame hour.
 * @return Hour (0-23).
 */
int GameTime::getHour() const
{
	return _hour;
}

/**
 * Returns the current ingame weekday.
 * @return Weekday (1-7), starts on Sunday.
 */
int GameTime::getWeekday() const
{
	return _weekday;
}

/**
 * Returns a localizable-string representation of
 * the current ingame weekday.
 * @return Weekday string ID.
 */
std::string GameTime::getWeekdayString() const
{
	std::string weekdays[] = {"STR_SUNDAY", "STR_MONDAY", "STR_TUESDAY", "STR_WEDNESDAY", "STR_THURSDAY", "STR_FRIDAY", "STR_SATURDAY"};
	return weekdays[_weekday - 1];
}

/**
 * Returns the current ingame day.
 * @return Day (1-31).
 */
int GameTime::getDay() const
{
	return _day;
}

/**
 * Returns the localized representation of the current
 * ingame day with the cardinal operator.
 * @param lang Pointer to current language.
 * @return Localized day string.
 */
std::string GameTime::getDayString(Language *lang) const
{
	std::string s;
	switch (_day)
	{
	case 1:
	case 21:
	case 31:
		s = "STR_DATE_FIRST";
		break;
	case 2:
	case 22:
		s = "STR_DATE_SECOND";
		break;
	case 3:
	case 23:
		s = "STR_DATE_THIRD";
		break;
	default:
		s = "STR_DATE_FOURTH";
	}
	return lang->getString(s).arg(_day);
}

/**
 * Returns the current ingame month.
 * @return Month (1-12).
 */
int GameTime::getMonth() const
{
	return _month;
}

/**
 * Returns a localizable-string representation of
 * the current ingame month.
 * @return Month string ID.
 */
std::string GameTime::getMonthString() const
{
	std::string months[] = {"STR_JAN", "STR_FEB", "STR_MAR", "STR_APR", "STR_MAY", "STR_JUN", "STR_JUL", "STR_AUG", "STR_SEP", "STR_OCT", "STR_NOV", "STR_DEC"};
	return months[_month - 1];
}

/**
 * Returns the current ingame year.
 * @return Year.
 */
int GameTime::getYear() const
{
	return _year;
}

/**
 * Reads STR_DATE_FORMAT ("{0}" = day string, "{1}" = month name, "{2}" = year, separated by spaces,
 * e.g. "{0} {1} {2}" or "{2}년 {1} {0}"). Each space separated token holds one placeholder plus
 * optional literals. A missing or malformed format falls back to day month year.
 * @param lang Language.
 * @param order Output: component (0 day, 1 month, 2 year) at each display position.
 * @param tokens Output: token text at each display position.
 */
void GameTime::getDateOrder(Language *lang, int order[3], std::string tokens[3])
{
	static const char *ph[3] = { "{0}", "{1}", "{2}" };
	bool ok = false;
	if (lang)
	{
		std::string fmt = lang->getString("STR_DATE_FORMAT");
		std::vector<std::string> tok;
		std::istringstream in(fmt);
		std::string t;
		while (in >> t) tok.push_back(t);
		if (tok.size() == 3)
		{
			bool seen[3] = { false, false, false };
			ok = true;
			for (int i = 0; i < 3 && ok; ++i)
			{
				int found = -1;
				for (int c = 0; c < 3; ++c)
				{
					if (tok[i].find(ph[c]) != std::string::npos)
					{
						if (found != -1) found = -2;
						else found = c;
					}
				}
				if (found < 0 || seen[found]) ok = false;
				else { seen[found] = true; order[i] = found; tokens[i] = tok[i]; }
			}
		}
	}
	if (!ok)
	{
		for (int i = 0; i < 3; ++i) { order[i] = i; tokens[i] = ph[i]; }
	}
}

namespace
{
std::string replacePlaceholder(std::string token, int comp, const std::string &value)
{
	const std::string ph = "{" + std::to_string(comp) + "}";
	size_t pos = token.find(ph);
	if (pos != std::string::npos) token.replace(pos, ph.size(), value);
	return token;
}
}

/**
 * Returns the date parts in the display order of STR_DATE_FORMAT.
 * @param lang Language.
 * @param parts Output: formatted part at each position.
 * @param order Output: component (0 day, 1 month, 2 year) at each position.
 */
void GameTime::getDateParts(Language *lang, std::string parts[3], int order[3]) const
{
	std::string tokens[3];
	getDateOrder(lang, order, tokens);
	const std::string values[3] = { getDayString(lang), lang->getString(getMonthString()), std::to_string(_year) };
	for (int i = 0; i < 3; ++i)
	{
		parts[i] = replacePlaceholder(tokens[i], order[i], values[order[i]]);
	}
}

/**
 * Returns the ingame date formatted with STR_DATE_FORMAT.
 * @param lang Language.
 * @param sep Separator between the parts.
 * @return Date string.
 */
std::string GameTime::getDateString(Language *lang, const std::string &sep) const
{
	std::string parts[3];
	int order[3];
	getDateParts(lang, parts, order);
	return parts[0] + sep + parts[1] + sep + parts[2];
}

/**
 * Formats a year with the year token of STR_DATE_FORMAT (e.g. "1999" or "1999년").
 * @param lang Language.
 * @param year Year.
 * @return Year string.
 */
std::string GameTime::formatYear(Language *lang, int year)
{
	int order[3];
	std::string tokens[3];
	getDateOrder(lang, order, tokens);
	for (int i = 0; i < 3; ++i)
	{
		if (order[i] == 2) return replacePlaceholder(tokens[i], 2, std::to_string(year));
	}
	return std::to_string(year);
}

/**
 * Returns a string version of the ingame date and time.
 * @return Game date and time string.
 */
std::string GameTime::getFullString() const
{
	std::ostringstream ss;
	ss << _year << "-" << std::setfill('0') << std::setw(2) << _month << "-" << std::setw(2) << _day;
	ss << " " << std::setw(2) << _hour << ":" << std::setw(2) << _minute << ":" << std::setw(2) << _second;
	return ss.str();
}

/**
 * Returns the current position of the daylight emitted on the globe
 * according to the current ingame time, so the value is 0 when the light
 * starts at 0 longitude (6h) and 1 when the light ends at 0 longitude (18h).
 * @return Daylight position (0-1).
 */
double GameTime::getDaylight() const
{
	return (double)((((((_hour + 18) % 24) * 60) + _minute) * 60) + _second) / (60 * 60 * 24);
}

}
