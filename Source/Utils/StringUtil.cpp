/*
* (C) 2020-2023 see Authors.txt
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

#include "stdafx.h"
#include <sstream>
#include "StringUtil.h"

void str_split(const std::string& str, std::vector<std::string>& tokens, char delim)
{
	std::istringstream iss(str);
	std::string tmp;
	while (std::getline(iss, tmp, delim)) {
		if (tmp.size()) {
			tokens.push_back(tmp);
		}
	}
}

void str_split(const std::wstring& wstr, std::vector<std::wstring>& tokens, wchar_t delim)
{
	std::wistringstream iss(wstr);
	std::wstring tmp;
	while (std::getline(iss, tmp, delim)) {
		if (tmp.size()) {
			tokens.push_back(tmp);
		}
	}
}

void str_replace(std::string& s, const std::string_view from, const std::string_view to)
{
	std::string str;
	size_t pos = 0;
	size_t pf = 0;
	while ((pf = s.find(from, pos)) < s.size()) {
		str.append(s, pos, pf - pos);
		str.append(to);
		pos = pf + from.size();
	}
	if (str.size()) {
		str.append(s, pos);
		s = str;
	}
}

void str_replace(std::wstring& s, const std::wstring_view from, const std::wstring_view to)
{
	std::wstring str;
	size_t pos = 0;
	size_t pf = 0;
	while ((pf = s.find(from, pos)) < s.size()) {
		str.append(s, pos, pf - pos);
		str.append(to);
		pos = pf + from.size();
	}
	if (str.size()) {
		str.append(s, pos);
		s = str;
	}
}

std::string ConvertWideToANSI(const std::wstring& wstr)
{
	int count = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
	std::string str(count, 0);
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr);
	return str;
}

std::wstring ConvertAnsiToWide(const char* pstr, int size)
{
	int count = MultiByteToWideChar(CP_ACP, 0, pstr, size, nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_ACP, 0, pstr, size, &wstr[0], count);
	return wstr;
}

std::wstring ConvertAnsiToWide(const std::string& str)
{
	int count = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.length(), &wstr[0], count);
	return wstr;
}

std::string ConvertWideToUtf8(const std::wstring& wstr)
{
	int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
	std::string str(count, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr);
	return str;
}

std::wstring ConvertUtf8ToWide(const std::string& str)
{
	int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
	return wstr;
}

std::wstring ConvertUtf8ToWide(const char* pstr, int size)
{
	int count = MultiByteToWideChar(CP_UTF8, 0, pstr, size, nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, pstr, size, &wstr[0], count);
	return wstr;
}
