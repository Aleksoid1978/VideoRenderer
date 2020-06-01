/*
* (C) 2020 see Authors.txt
*
* This file is part of MPC-BE.
*
* MPC-BE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-BE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include <cctype>
#include <locale>

//
// convert string to lower or upper case
//

inline void str_tolower(std::string& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); } );
	// char for std::tolower should be converted to unsigned char
}

inline void str_toupper(std::string& s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); } );
	// char for std::toupper should be converted to unsigned char
}

inline void str_tolower(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
}

inline void str_toupper(std::wstring& s)
{
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
}

inline void str_tolower_all(std::wstring& s)
{
	const std::ctype<wchar_t>& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
	f.tolower(&s[0], &s[0] + s.size());
}

inline void str_toupper_all(std::wstring& s)
{
	const std::ctype<wchar_t>& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
	f.toupper(&s[0], &s[0] + s.size());
}

//
// split a string using char delimiter
//

void str_split(const std::string& str, std::vector<std::string>& tokens, char delim);

void str_split(const std::wstring& wstr, std::vector<std::wstring>& tokens, wchar_t delim);

//
// trimming whitespace
//

inline const std::string str_trim(const std::string_view& sv)
{
	auto sfront = std::find_if_not(sv.begin(), sv.end(), [](int c) {return isspace(c); });
	auto sback = std::find_if_not(sv.rbegin(), sv.rend(), [](int c) {return isspace(c); }).base();
	return (sback <= sfront ? std::string() : std::string(sfront, sback));
}

inline const std::wstring str_trim(const std::wstring_view& sv)
{
	auto sfront = std::find_if_not(sv.begin(), sv.end(), [](int c) {return iswspace(c); });
	auto sback = std::find_if_not(sv.rbegin(), sv.rend(), [](int c) {return iswspace(c); }).base();
	return (sback <= sfront ? std::wstring() : std::wstring(sfront, sback));
}

//
// trimming a character at the end
//

inline void str_trim_end(std::string& s, const char ch)
{
	s.erase(s.find_last_not_of(ch) + 1);
}

inline void str_trim_end(std::wstring& s, const wchar_t ch)
{
	s.erase(s.find_last_not_of(ch) + 1);
}

//
//
//

void str_replace(std::string& s, const std::string& from, const std::string& to);
void str_replace(std::wstring& s, const std::wstring& from, const std::wstring& to);

//
// simple convert ANSI string to wide character string
//

inline const std::wstring A2WStr(const std::string_view& sv)
{
	return std::wstring(sv.begin(), sv.end());
}

//
// converting strings of different formats
//

std::string ConvertWideToANSI(const std::wstring& wstr);

std::wstring ConvertAnsiToWide(const std::string& str);

std::string ConvertWideToUtf8(const std::wstring& wstr);

std::wstring ConvertUtf8ToWide(const std::string& str);
