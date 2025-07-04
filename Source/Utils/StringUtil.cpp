//
// Copyright (c) 2020-2025 v0lt, Aleksoid
//
// SPDX-License-Identifier: MIT
//

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

//
// converting strings of different formats
//

std::wstring ConvertAnsiToWide(const std::string_view sv)
{
	int count = MultiByteToWideChar(CP_ACP, 0, sv.data(), (int)sv.length(), nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_ACP, 0, sv.data(), (int)sv.length(), &wstr[0], count);
	return wstr;
}

std::wstring ConvertUtf8ToWide(const std::string_view sv)
{
	int count = MultiByteToWideChar(CP_UTF8, 0, sv.data(), (int)sv.length(), nullptr, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, sv.data(), (int)sv.length(), &wstr[0], count);
	return wstr;
}

std::string ConvertWideToANSI(const std::wstring_view wsv)
{
	int count = WideCharToMultiByte(CP_ACP, 0, wsv.data(), (int)wsv.length(), nullptr, 0, nullptr, nullptr);
	std::string str(count, 0);
	WideCharToMultiByte(CP_ACP, 0, wsv.data(), -1, &str[0], count, nullptr, nullptr);
	return str;
}

std::string ConvertWideToUtf8(const std::wstring_view wsv)
{
	int count = WideCharToMultiByte(CP_UTF8, 0, wsv.data(), (int)wsv.length(), nullptr, 0, nullptr, nullptr);
	std::string str(count, 0);
	WideCharToMultiByte(CP_UTF8, 0, wsv.data(), -1, &str[0], count, nullptr, nullptr);
	return str;
}
