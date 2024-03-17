// Copyright (c) 2020-2024 v0lt, Aleksoid
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#ifndef FCC
#define FCC(ch4) ((((DWORD)(ch4) & 0xFF) << 24) |     \
                  (((DWORD)(ch4) & 0xFF00) << 8) |    \
                  (((DWORD)(ch4) & 0xFF0000) >> 8) |  \
                  (((DWORD)(ch4) & 0xFF000000) >> 24))
#endif

#ifndef DEFINE_MEDIATYPE_GUID
#define DEFINE_MEDIATYPE_GUID(name, format) \
    DEFINE_GUID(name,                       \
    format, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
#endif

template <typename... Args>
inline void DebugLogFmt(std::wstring_view format, Args&& ...args)
{
	DbgLogInfo(LOG_TRACE, 3, std::vformat(format, std::make_wformat_args(args...)).c_str());
}

#ifdef _DEBUG
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

#define ALIGN(x, a)           __ALIGN_MASK(x,(decltype(x))(a)-1)
#define __ALIGN_MASK(x, mask) (((x)+(mask))&~(mask))

// Media subtypes
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y8,        0x20203859);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y800,      0x30303859);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y16,       0x10003159); // Y1[0][16]
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_YV16,      0x36315659);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_YV24,      0x34325659);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_I420,      0x30323449); // from "wmcodecdsp.h"
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y42B,      0x42323459);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_444P,      0x50343434);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y210,      0x30313259);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y216,      0x36313259);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y410,      0x30313459);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_Y416,      0x36313459);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_YUV444P16, 0x10003359); // Y3[0][16]
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_RGB48,     0x30424752); // RGB[48] (RGB0)
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_BGR48,     0x30524742); // BGR[48] (BGR0)
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_BGRA64,    0x40415242); // BRA[64] (BRA@)
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_b48r,      0x72383462);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_b64a,      0x61343662);
DEFINE_MEDIATYPE_GUID(MEDIASUBTYPE_r210,      0x30313272);
DEFINE_GUID(MEDIASUBTYPE_LAV_RAWVIDEO, 0xd80fa03c, 0x35c1, 0x4fa1, 0x8c, 0x8e, 0x37, 0x5c, 0x86, 0x67, 0x16, 0x6e);

// non-standard values for Transfer Matrix
#define VIDEOTRANSFERMATRIX_FCC     6
#define VIDEOTRANSFERMATRIX_YCgCo   7

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
