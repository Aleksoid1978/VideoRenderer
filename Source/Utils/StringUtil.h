//
// Copyright (c) 2020-2025 v0lt, Aleksoid
//
// SPDX-License-Identifier: MIT
//

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
	std::ignore = f.tolower(&s[0], &s[0] + s.size());
}

inline void str_toupper_all(std::wstring& s)
{
	const std::ctype<wchar_t>& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
	std::ignore = f.toupper(&s[0], &s[0] + s.size());
}

//
// split a string using char delimiter
//

void str_split(const std::string& str, std::vector<std::string>& tokens, char delim);

void str_split(const std::wstring& wstr, std::vector<std::wstring>& tokens, wchar_t delim);

//
// trimming whitespace
//

inline std::string str_trim(const std::string_view sv)
{
	auto sfront = std::find_if_not(sv.begin(), sv.end(), [](int c) {return isspace(c); });
	auto sback = std::find_if_not(sv.rbegin(), sv.rend(), [](int c) {return isspace(c); }).base();
	return (sback <= sfront ? std::string() : std::string(sfront, sback));
}

inline std::wstring str_trim(const std::wstring_view sv)
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
// truncate after a null character
//

inline void str_truncate_after_null(std::string& s)
{
	s.erase(std::find(s.begin(), s.end(), '\0'), s.end());
}

inline void str_truncate_after_null(std::wstring& s)
{
	s.erase(std::find(s.begin(), s.end(), '\0'), s.end());
}

//
// replace substring
//

void str_replace(std::string& s, const std::string_view from, const std::string_view to);
void str_replace(std::wstring& s, const std::wstring_view from, const std::wstring_view to);

//
// simple convert ASCII string to wide character string
//
inline std::wstring A2WStr(const std::string_view sv)
{
	return std::wstring(sv.begin(), sv.end());
}

//
// converting strings of different formats
//

// convert system default Windows ANSI code page string to UTF-16 (wide character) string
std::wstring ConvertAnsiToWide(const std::string_view sv);

std::wstring ConvertUtf8ToWide(const std::string_view sv);

// convert UTF-16 (wide character) string to system default Windows ANSI code page string
std::string ConvertWideToANSI(const std::wstring_view wsv);

std::string ConvertWideToUtf8(const std::wstring_view wsv);
