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

#pragma once

#include <d3d9.h>
#include <dxva2api.h>

#ifndef FCC
#define FCC(ch4) ((((DWORD)(ch4) & 0xFF) << 24) |     \
                  (((DWORD)(ch4) & 0xFF00) << 8) |    \
                  (((DWORD)(ch4) & 0xFF0000) >> 8) |  \
                  (((DWORD)(ch4) & 0xFF000000) >> 24))
#endif

#define D3DFMT_NV12 (D3DFORMAT)FCC('NV12')
#define D3DFMT_YV12 (D3DFORMAT)FCC('YV12')
#define D3DFMT_P010 (D3DFORMAT)FCC('P010')
#define D3DFMT_AYUV (D3DFORMAT)FCC('AYUV')

#define PCIV_AMDATI      0x1002
#define PCIV_NVIDIA      0x10DE
#define PCIV_INTEL       0x8086

#ifdef _DEBUG
#define DLog(...) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__)
#define DLogIf(f,...) {if (f) DbgLogInfo(LOG_TRACE, 3, __VA_ARGS__);}
#define DLogError(...) DbgLogInfo(LOG_ERROR, 3, __VA_ARGS__)
#else
#define DLog(...) __noop
#define DLogIf(f,...) __noop
#define DLogError(...) __noop
#endif

#define SAFE_CLOSE_HANDLE(p) { if (p) { if ((p) != INVALID_HANDLE_VALUE) ASSERT(CloseHandle(p)); (p) = nullptr; } }

#define ALIGN(x, a) (((x)+(a)-1)&~((a)-1)) 

inline CStringW CStringFromGUID(const GUID& guid)
{
	WCHAR buff[40] = {};
	if (StringFromGUID2(guid, buff, 39) <= 0) {
		StringFromGUID2(GUID_NULL, buff, 39);
	}
	return CStringW(buff);
}

CStringW HR2Str(const HRESULT hr);

const wchar_t* D3DFormatToString(const D3DFORMAT format);
const wchar_t* DXGIFormatToString(const DXGI_FORMAT format);
const wchar_t* DXVA2VPDeviceToString(const GUID& guid);

typedef void(*CopyFrameDataFn)(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);

struct FmtConvParams_t {
	GUID            Subtype;
	D3DFORMAT       D3DFormat;
	DXGI_FORMAT     DXGIFormat;
	int             Packsize;
	int             PitchCoeff;
	bool            bRGB;
	CopyFrameDataFn Func;
};

const FmtConvParams_t* GetFmtConvParams(GUID subtype);

// YUY2, AYUV
void CopyFrameAsIs(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);
// RGB32 to X8R8G8B8, ARGB32 to A8R8G8B8
void CopyFrameUpsideDown(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);
// RGB24 to X8R8G8B8
void CopyFrameRGB24UpsideDown(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);
// YV12
void CopyFrameYV12(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);
// NV12, P010
void CopyFramePackedUV(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);

void CopyFrameData(const D3DFORMAT format, const UINT width, const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch, const UINT src_size);

void ClipToSurface(const int texW, const int texH, RECT& s, RECT& d);
