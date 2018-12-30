/*
* (C) 2018 see Authors.txt
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
#include "Helper.h"

CStringW HR2Str(const HRESULT hr)
{
	CStringW str;
#define UNPACK_VALUE(VALUE) case VALUE: str = L#VALUE; break;
	switch (hr) {
		// common HRESULT values https://docs.microsoft.com/en-us/windows/desktop/seccrypto/common-hresult-values
		UNPACK_VALUE(S_OK);
		UNPACK_VALUE(S_FALSE);
		UNPACK_VALUE(E_NOTIMPL);
		UNPACK_VALUE(E_NOINTERFACE);
		UNPACK_VALUE(E_POINTER);
		UNPACK_VALUE(E_ABORT);
		UNPACK_VALUE(E_FAIL);
		UNPACK_VALUE(E_UNEXPECTED);
		UNPACK_VALUE(E_ACCESSDENIED);
		UNPACK_VALUE(E_HANDLE);
		UNPACK_VALUE(E_OUTOFMEMORY);
		UNPACK_VALUE(E_INVALIDARG);
		// some D3DERR values https://docs.microsoft.com/en-us/windows/desktop/direct3d9/d3derr
		UNPACK_VALUE(D3DERR_DEVICEHUNG);
		UNPACK_VALUE(D3DERR_DEVICELOST);
		UNPACK_VALUE(D3DERR_DRIVERINTERNALERROR);
		UNPACK_VALUE(D3DERR_INVALIDCALL);
		UNPACK_VALUE(D3DERR_OUTOFVIDEOMEMORY);
	default:
		str.Format(L"0x%08x", hr);
	};
#undef UNPACK_VALUE
	return str;
}

const wchar_t* D3DFormatToString(const D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_A2B10G10R10:   return L"A2B10G10R10";
	case D3DFMT_A8R8G8B8:      return L"A8R8G8B8";      // DXVA-HD
	case D3DFMT_X8R8G8B8:      return L"X8R8G8B8";
	case D3DFMT_A2R10G10B10:   return L"A2R10G10B10";
	case D3DFMT_A8P8:          return L"A8P8";          // DXVA-HD
	case D3DFMT_P8:            return L"P8";            // DXVA-HD
	case D3DFMT_L8:            return L"L8";
	case D3DFMT_L16:           return L"L16";
	case D3DFMT_A16B16G16R16F: return L"A16B16G16R16F";
	case D3DFMT_A32B32G32R32F: return L"A32B32G32R32F";
	case D3DFMT_YUY2:          return L"YUY2";
	case D3DFMT_UYVY:          return L"UYVY";
	case D3DFMT_NV12:          return L"NV12";
	case D3DFMT_YV12:          return L"YV12";
	case D3DFMT_P010:          return L"P010";
	case D3DFMT_AYUV:          return L"AYUV";          // DXVA-HD
	case FCC('AIP8'):          return L"AIP8";          // DXVA-HD
	case FCC('AI44'):          return L"AI44";          // DXVA-HD
	};

	return L"UNKNOWN";
}

const wchar_t* DXGIFormatToString(const DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM:    return L"R8G8B8A8_UNORM";
	case DXGI_FORMAT_B8G8R8A8_UNORM:    return L"B8G8R8A8_UNORM";
	case DXGI_FORMAT_B8G8R8X8_UNORM:    return L"B8G8R8X8_UNORM";
	case DXGI_FORMAT_R10G10B10A2_UNORM: return L"R10G10B10A2_UNORM";
	case DXGI_FORMAT_AYUV:              return L"AYUV";
	case DXGI_FORMAT_NV12:              return L"NV12";
	case DXGI_FORMAT_P010:              return L"P010";
	case DXGI_FORMAT_420_OPAQUE:        return L"420_OPAQUE";
	case DXGI_FORMAT_YUY2:              return L"YUY2";
	case DXGI_FORMAT_AI44:              return L"AI44";
	case DXGI_FORMAT_IA44:              return L"IA44";
	case DXGI_FORMAT_P8:                return L"P8";
	case DXGI_FORMAT_A8P8:              return L"A8P8";
	};

	return L"UNKNOWN";
}

const wchar_t* DXVA2VPDeviceToString(const GUID& guid)
{
	if (guid == DXVA2_VideoProcProgressiveDevice) {
		return L"ProgressiveDevice";
	}
	else if (guid == DXVA2_VideoProcBobDevice) {
		return L"BobDevice";
	}
	else if (guid == DXVA2_VideoProcSoftwareDevice) {
		return L"SoftwareDevice";
	}

	return CStringFromGUID(guid);
}

static FmtConvParams_t s_FmtConvMapping[] = {
	//   subtype          | str     |DXVA2Format     | D3DFormat(DX9)    | DXGIFormat(DX11)   |Packsize|PitchCoeff| bRGB|  Func
	{ MEDIASUBTYPE_YV12,   "YV12",   D3DFMT_YV12,     D3DFMT_UNKNOWN,     DXGI_FORMAT_UNKNOWN,        1, 3,        false, &CopyFrameYV12,       },
	{ MEDIASUBTYPE_NV12,   "NV12",   D3DFMT_NV12,     D3DFMT_UNKNOWN,     DXGI_FORMAT_NV12,           1, 3,        false, &CopyFramePackedUV,   },
	{ MEDIASUBTYPE_P010,   "P010",   D3DFMT_P010,     D3DFMT_UNKNOWN,     DXGI_FORMAT_P010,           2, 3,        false, &CopyFramePackedUV,   },
	{ MEDIASUBTYPE_YUY2,   "YUY2",   D3DFMT_YUY2,     D3DFMT_UNKNOWN,     DXGI_FORMAT_YUY2,           2, 2,        false, &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_AYUV,   "AYUV",   D3DFMT_UNKNOWN,  D3DFMT_X8R8G8B8,    DXGI_FORMAT_AYUV,           4, 2,        false, &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_RGB32,  "RGB32",  D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8,    DXGI_FORMAT_B8G8R8X8_UNORM, 4, 2,        true,  &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_ARGB32, "ARGB32", D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8,    DXGI_FORMAT_B8G8R8A8_UNORM, 4, 2,        true,  &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_RGB24,  "RGB24",  D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8,    DXGI_FORMAT_B8G8R8X8_UNORM, 3, 2,        true,  &CopyFrameRGB24SSSE3, },
	{ MEDIASUBTYPE_Y8,     "Y8",     D3DFMT_UNKNOWN,  D3DFMT_L8,          DXGI_FORMAT_UNKNOWN,        1, 2,        true,  &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_Y800,   "Y800",   D3DFMT_UNKNOWN,  D3DFMT_L8,          DXGI_FORMAT_UNKNOWN,        1, 2,        true,  &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_Y116,   "Y116",   D3DFMT_UNKNOWN,  D3DFMT_L16,         DXGI_FORMAT_UNKNOWN,        2, 2,        true,  &CopyFrameAsIs,       },
	{ MEDIASUBTYPE_Y410,   "Y410",   D3DFMT_UNKNOWN,  D3DFMT_A2B10G10R10, DXGI_FORMAT_UNKNOWN,        4, 2,        false, &CopyFrameAsIs,       },
};

const FmtConvParams_t* GetFmtConvParams(GUID subtype)
{
	for (const auto& fe : s_FmtConvMapping) {
		if (fe.Subtype == subtype) {
			return &fe;
		}
	}
	return nullptr;
}

void CopyFrameAsIs(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	unsigned linesize = abs(src_pitch);

	for (UINT y = 0; y < height; ++y) {
		memcpy(dst, src, linesize);
		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameRGB24(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	UINT line_pixels = abs(src_pitch) / 3;

	for (UINT y = 0; y < height; ++y) {
		uint32_t* src32 = (uint32_t*)src;
		uint32_t* dst32 = (uint32_t*)dst;
		for (UINT i = 0; i < line_pixels; i += 4) {
			uint32_t sa = src32[0];
			uint32_t sb = src32[1];
			uint32_t sc = src32[2];

			dst32[i + 0] = sa;
			dst32[i + 1] = (sa >> 24) | (sb << 8);
			dst32[i + 2] = (sb >> 16) | (sc << 16);
			dst32[i + 3] = sc >> 8;

			src32 += 3;
		}

		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameRGB24SSSE3(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	UINT line_pixels = abs(src_pitch) / 3;
	__m128i mask = _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);

	for (UINT y = 0; y < height; ++y) {
		__m128i *src128 = (__m128i*)src;
		__m128i *dst128 = (__m128i*)dst;
		for (UINT i = 0; i < line_pixels; i += 16) {
			__m128i sa = _mm_load_si128(src128);
			__m128i sb = _mm_load_si128(src128 + 1);
			__m128i sc = _mm_load_si128(src128 + 2);

			__m128i val = _mm_shuffle_epi8(sa, mask);
			_mm_store_si128(dst128, val);
			val = _mm_shuffle_epi8(_mm_alignr_epi8(sb, sa, 12), mask);
			_mm_store_si128(dst128 + 1, val);
			val = _mm_shuffle_epi8(_mm_alignr_epi8(sc, sb, 8), mask);
			_mm_store_si128(dst128 + 2, val);
			val = _mm_shuffle_epi8(_mm_alignr_epi8(sc, sc, 4), mask);
			_mm_store_si128(dst128 + 3, val);

			src128 += 3;
			dst128 += 4;
		}

		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameYV12(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	ASSERT(src_pitch > 0);

	for (UINT y = 0; y < height; ++y) {
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
	}

	const UINT chromaheight = height / 2;
	src_pitch /= 2;
	dst_pitch /= 2;
	for (UINT y = 0; y < chromaheight; ++y) {
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFramePackedUV(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	ASSERT(src_pitch > 0);
	UINT lines = height * 3 / 2;

	for (UINT y = 0; y < lines; ++y) {
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameY410(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, int src_pitch)
{
	ASSERT(src_pitch > 0);
	UINT line_pixels = src_pitch / 4;

	for (UINT y = 0; y < height; ++y) {
		uint32_t* src32 = (uint32_t*)src;
		uint32_t* dst32 = (uint32_t*)dst;
		for (UINT i = 0; i < line_pixels; i++) {
			uint32_t t = src32[i];
			// U = (t & 0x000003ff); Y = (t & 0x000ffc00) >> 10; V = (t & 0x3ff00000) >> 20; A = (t & 0xC0000000) >> 30;
			//dst32[i] = (t & 0xC0000000) | ((t & 0x000fffff) << 10) | ((t & 0x3ff00000) >> 20); // to D3DFMT_A2R10G10B10
			dst32[i] = (t & 0xfff00000) | ((t & 0x000003ff) << 10) | ((t & 0x000ffc00) >> 10); // to D3DFMT_A2B10G10R10
		}
		src += src_pitch;
		dst += dst_pitch;
	}
}

void ClipToSurface(const int texW, const int texH, RECT& s, RECT& d)
{
	const int sw = s.right - s.left;
	const int sh = s.bottom - s.top;
	const int dw = d.right - d.left;
	const int dh = d.bottom - d.top;

	if (d.left >= texW || d.right < 0 || d.top >= texH || d.bottom < 0
		|| sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		SetRectEmpty(&s);
		SetRectEmpty(&d);
		return;
	}

	if (d.right > texW) {
		s.right -= (d.right - texW) * sw / dw;
		d.right = texW;
	}
	if (d.bottom > texH) {
		s.bottom -= (d.bottom - texH) * sh / dh;
		d.bottom = texH;
	}
	if (d.left < 0) {
		s.left += (0 - d.left) * sw / dw;
		d.left = 0;
	}
	if (d.top < 0) {
		s.top += (0 - d.top) * sh / dh;
		d.top = 0;
	}

	return;
}
