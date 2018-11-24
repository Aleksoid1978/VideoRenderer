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
	case D3DFMT_A8R8G8B8:      return L"A8R8G8B8";      // DXVA-HD
	case D3DFMT_X8R8G8B8:      return L"X8R8G8B8";
	case D3DFMT_A2R10G10B10:   return L"A2R10G10B10";
	case D3DFMT_A8P8:          return L"A8P8";          // DXVA-HD
	case D3DFMT_P8:            return L"P8";            // DXVA-HD
	case D3DFMT_A16B16G16R16F: return L"A16B16G16R16F"; // for shaders
	case D3DFMT_A32B32G32R32F: return L"A32B32G32R32F"; // for shaders
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
	{ MEDIASUBTYPE_NV12,   D3DFMT_NV12,     DXGI_FORMAT_NV12,           1, 3, false, &CopyFramePackedUV },
	{ MEDIASUBTYPE_YV12,   D3DFMT_YV12,     DXGI_FORMAT_UNKNOWN,        1, 3, false, &CopyFrameYV12},
	{ MEDIASUBTYPE_P010,   D3DFMT_P010,     DXGI_FORMAT_P010,           2, 3, false, &CopyFramePackedUV },
	{ MEDIASUBTYPE_YUY2,   D3DFMT_YUY2,     DXGI_FORMAT_YUY2,           2, 2, false, &CopyFrameAsIs },
	{ MEDIASUBTYPE_AYUV,   D3DFMT_AYUV,     DXGI_FORMAT_AYUV,           4, 2, false, &CopyFrameAsIs },
	{ MEDIASUBTYPE_RGB32,  D3DFMT_X8R8G8B8, DXGI_FORMAT_B8G8R8X8_UNORM, 4, 2, true,  &CopyFrameUpsideDown },
	{ MEDIASUBTYPE_ARGB32, D3DFMT_A8R8G8B8, DXGI_FORMAT_B8G8R8A8_UNORM, 4, 2, true,  &CopyFrameUpsideDown },
	{ MEDIASUBTYPE_RGB24,  D3DFMT_X8R8G8B8, DXGI_FORMAT_B8G8R8X8_UNORM, 3, 2, true,  &CopyFrameRGB24UpsideDown },
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

void CopyFrameAsIs(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch)
{
	for (UINT y = 0; y < height; ++y) {
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameUpsideDown(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch)
{
	src += src_pitch * (height - 1);

	for (UINT y = 0; y < height; ++y) {
		memcpy(dst, src, src_pitch);
		src -= src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameRGB24UpsideDown(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch)
{
	src += src_pitch * (height - 1);
	UINT line_pixels = src_pitch / 3;

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

		src -= src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameYV12(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch)
{
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

void CopyFramePackedUV(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch)
{
	UINT lines = height * 3 / 2;

	for (UINT y = 0; y < lines; ++y) {
		memcpy(dst, src, src_pitch);
		src += src_pitch;
		dst += dst_pitch;
	}
}

void CopyFrameData(const D3DFORMAT format, const UINT width, const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch, const UINT src_size)
{
	//UINT linesize = std::min(src_pitch, dst_pitch); // TODO

	if (format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8) {
		const UINT linesize = width * 4;
		src += src_pitch * (height - 1);

		for (UINT y = 0; y < height; ++y) {
			memcpy(dst, src, linesize);
			src -= src_pitch;
			dst += dst_pitch;
		}
	}
	else if (src_pitch == dst_pitch) {
		ASSERT(src_size == src_pitch * height);
		memcpy(dst, src, src_size);
	}
	else if (format == D3DFMT_YV12) {
		for (UINT y = 0; y < height; ++y) {
			memcpy(dst, src, width);
			src += src_pitch;
			dst += dst_pitch;
		}

		const UINT chromaline = width / 2;
		const UINT chromaheight = height / 2;
		src_pitch /= 2;
		dst_pitch /= 2;
		for (UINT y = 0; y < chromaheight; ++y) {
			memcpy(dst, src, chromaline);
			src += src_pitch;
			dst += dst_pitch;
			memcpy(dst, src, chromaline);
			src += src_pitch;
			dst += dst_pitch;
		}
	}
	else {
		UINT linesize = width;
		UINT lines = height;
		if (format == D3DFMT_YUY2 || format == D3DFMT_P010) {
			linesize *= 2;
		} else if (format == D3DFMT_AYUV) {
			linesize *= 4;
		}
		if (format == D3DFMT_NV12 || format == D3DFMT_P010) {
			lines = lines * 3 / 2;
		}

		for (UINT y = 0; y < lines; ++y) {
			memcpy(dst, src, linesize);
			src += src_pitch;
			dst += dst_pitch;
		}
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
