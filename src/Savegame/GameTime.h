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
#include <string>
#include "../Engine/Yaml.h"

namespace OpenXcom
{

class Language;

/**
 * Enumerator for time periods.
 */
enum TimeTrigger { TIME_5SEC, TIME_10MIN, TIME_30MIN, TIME_1HOUR, TIME_1DAY, TIME_1MONTH };

/**
 * Stores the current ingame time/date according to GMT.
 * Takes care of managing and representing each component,
 * as well as common time operations.
 */
class GameTime
{
private:
	int _second, _minute, _hour, _weekday, _day, _month, _year;
public:
	/// Creates a new ingame time at a certain point.
	GameTime(int weekday, int day, int month, int year, int hour, int minute, int second);
	/// Cleans up the ingame time.
	~GameTime();
	/// Loads the time from YAML.
	void load(const YAML::YamlNodeReader& reader);
	/// Saves the time to YAML.
	void save(YAML::YamlNodeWriter writer) const;
	/// Checks if it is the last day of the month.
	bool isLastDayOfMonth();
	/// Advances the time by 5 seconds.
	TimeTrigger advance();
	/// Gets the ingame second.
	int getSecond() const;
	/// Gets the ingame minute.
	int getMinute() const;
	/// Gets the ingame hour.
	int getHour() const;
	/// Gets the ingame weekday.
	int getWeekday() const;
	// Gets a string version of the ingame weekday.
	std::string getWeekdayString() const;
	/// Gets the ingame day.
	int getDay() const;
	// Gets a string version of the ingame day.
	std::string getDayString(Language *lang) const;
	/// Gets the ingame month.
	int getMonth() const;
	// Gets a string version of the ingame month.
	std::string getMonthString() const;
	/// Gets the ingame year.
	int getYear() const;
	// Gets a string version of the ingame date and time.
	std::string getFullString() const;
	/// Gets the order of the date parts (0 = day, 1 = month, 2 = year) and their tokens from STR_DATE_FORMAT.
	static void getDateOrder(Language *lang, int order[3], std::string tokens[3]);
	/// Gets the date parts (day, month, year with the literals of STR_DATE_FORMAT) in display order.
	void getDateParts(Language *lang, std::string parts[3], int order[3]) const;
	/// Gets the ingame date formatted with STR_DATE_FORMAT (parts joined with sep).
	std::string getDateString(Language *lang, const std::string &sep = " ") const;
	/// Formats a year with the year token of STR_DATE_FORMAT.
	static std::string formatYear(Language *lang, int year);
	/// Gets the position of the daylight according to the ingame time.
	double getDaylight() const;
};

}
