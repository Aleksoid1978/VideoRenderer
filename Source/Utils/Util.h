//
// Copyright (c) 2020-2025 v0lt, Aleksoid
//
// SPDX-License-Identifier: MIT
//

#pragma once

#ifdef _DEBUG
template <typename... Args>
inline void DebugLogFmt(std::wstring_view format, Args&& ...args)
{
	if (sizeof...(Args)) {
		DbgLogInfo(LOG_TRACE, 3, std::vformat(format, std::make_wformat_args(args...)).c_str());
	} else {
		DbgLogInfo(LOG_TRACE, 3, format.data());
	}
}

template <typename... Args>
inline void DebugLogFmt(std::string_view format, Args&& ...args)
{
	if (sizeof...(Args)) {
		DbgLogInfo(LOG_TRACE, 3, std::vformat(format, std::make_format_args(args...)).c_str());
	} else {
		DbgLogInfo(LOG_TRACE, 3, format.data());
	}
}

#define DLog(...) DebugLogFmt(__VA_ARGS__)
#define DLogIf(f,...) {if (f) DebugLogFmt(__VA_ARGS__);}
#else
#define DLog(...) __noop
#define DLogIf(f,...) __noop
#endif

#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p) = nullptr; } }
#define SAFE_CLOSE_HANDLE(p) { if (p) { if ((p) != INVALID_HANDLE_VALUE) ASSERT(CloseHandle(p)); (p) = nullptr; } }
#define SAFE_DELETE(p)       { if (p) { delete (p); (p) = nullptr; } }

#define QI(i) (riid == __uuidof(i)) ? GetInterface((i*)this, ppv) :
#define IFQIRETURN(i) if (riid == __uuidof(i)) { return GetInterface((i*)this, ppv); }

#define ALIGN(x, a)           __ALIGN_MASK(x,(decltype(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x)+(mask))&~(mask))

template <typename T>
// If the specified value is out of range, set to default values.
inline T discard(T const& val, T const& def, T const& lo, T const& hi)
{
	return (val > hi || val < lo) ? def : val;
}

template <typename T>
inline T round_pow2(T number, T pow2)
{
	ASSERT(pow2 > 0);
	ASSERT(!(pow2 & (pow2 - 1)));
	--pow2;
	if (number < 0) {
		return (number - pow2) & ~pow2;
	} else {
		return (number + pow2) & ~pow2;
	}
}

[[nodiscard]] bool IsWindows11_24H2OrGreater();
LPCWSTR GetWindowsVersion();

inline std::wstring GUIDtoWString(const GUID& guid)
{
	std::wstring str(39, 0);
	int ret = StringFromGUID2(guid, &str[0], 39);
	if (ret) {
		str.resize(ret - 1);
	} else {
		str.clear();
	}
	return str;
}

std::wstring HR2Str(const HRESULT hr);

HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid);

// Usage: SetThreadName ((DWORD)-1, "MainThread");
void SetThreadName(DWORD dwThreadID, const char* threadName);
