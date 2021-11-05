/*
* (C) 2018-2021 see Authors.txt
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

#include <dxva2api.h>
#include "Utils/Util.h"
#include "Utils/StringUtil.h"
#include "csputils.h"
#include "../Include/IMediaSideData.h"

#define D3DFMT_YV12 (D3DFORMAT)FCC('YV12')
#define D3DFMT_NV12 (D3DFORMAT)FCC('NV12')
#define D3DFMT_P010 (D3DFORMAT)FCC('P010')
#define D3DFMT_P016 (D3DFORMAT)FCC('P016')
#define D3DFMT_P210 (D3DFORMAT)FCC('P210')
#define D3DFMT_P216 (D3DFORMAT)FCC('P216')
#define D3DFMT_AYUV (D3DFORMAT)FCC('AYUV')
#define D3DFMT_Y410 (D3DFORMAT)FCC('Y410')
#define D3DFMT_Y416 (D3DFORMAT)FCC('Y416')

#define D3DFMT_PLANAR (D3DFORMAT)0xFFFF

#define DXGI_FORMAT_PLANAR (DXGI_FORMAT)0xFFFF

#define PCIV_AMDATI      0x1002
#define PCIV_NVIDIA      0x10DE
#define PCIV_INTEL       0x8086

DEFINE_GUID(CLSID_XySubFilter,            0x2DFCB782, 0xEC20, 0x4A7C, 0xB5, 0x30, 0x45, 0x77, 0xAD, 0xB3, 0x3F, 0x21);
DEFINE_GUID(CLSID_XySubFilter_AutoLoader, 0x6B237877, 0x902B, 0x4C6C, 0x92, 0xF6, 0xE6, 0x31, 0x69, 0xA5, 0x16, 0x6C);

struct VR_Extradata {
	LONG  QueryWidth;
	LONG  QueryHeight;
	LONG  FrameWidth;
	LONG  FrameHeight;
	DWORD Compression;
};

struct ScalingShaderResId {
	UINT shaderX;
	UINT shaderY;
	wchar_t* const description;
};

LPCWSTR GetNameAndVersion();

std::wstring MediaType2Str(const CMediaType *pmt);

const wchar_t* D3DFormatToString(const D3DFORMAT format);
const wchar_t* DXGIFormatToString(const DXGI_FORMAT format);
std::wstring DXVA2VPDeviceToString(const GUID& guid);
void SetDefaultDXVA2ProcAmpRanges(DXVA2_ValueRange(&DXVA2ProcAmpRanges)[4]);
void SetDefaultDXVA2ProcAmpValues(DXVA2_ProcAmpValues& DXVA2ProcAmpValues);
bool IsDefaultDXVA2ProcAmpValues(const DXVA2_ProcAmpValues& DXVA2ProcAmpValues);

typedef void(*CopyFrameDataFn)(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);

enum ColorFormat_t {
	CF_NONE = 0,
	CF_YV12,
	CF_YUV420P8,
	CF_NV12,
	CF_P010,
	CF_P016,
	CF_YUY2,
	CF_YV16,
	CF_P210,
	CF_P216,
	CF_YV24,
	CF_YUV444P8,
	CF_AYUV,
	CF_Y410,
	CF_Y416,
	CF_YUV420P16,
	CF_YUV422P16,
	CF_YUV444P16,
	CF_RGB24,
	CF_GBRP8,
	CF_XRGB32,
	CF_ARGB32,
	CF_RGB48,
	CF_BGR48,
	CF_GBRP16,
	CF_BGRA64,
	CF_B64A,
	CF_Y8,
	CF_Y16,
};

enum ColorSystem_t {
	CS_YUV,
	CS_RGB,
	CS_GRAY
};

struct DX9PlaneConfig {
	D3DFORMAT   FmtPlane1;
	D3DFORMAT   FmtPlane2;
	D3DFORMAT   FmtPlane3;
	UINT        div_chroma_w;
	UINT        div_chroma_h;
};

struct DX11PlaneConfig_t {
	DXGI_FORMAT FmtPlane1;
	DXGI_FORMAT FmtPlane2;
	DXGI_FORMAT FmtPlane3;
	UINT        div_chroma_w;
	UINT        div_chroma_h;
};

struct FmtConvParams_t {
	ColorFormat_t      cformat;
	const wchar_t*     str;
	D3DFORMAT          DXVA2Format;
	D3DFORMAT          D3DFormat;
	DX9PlaneConfig*    pDX9Planes;
	DXGI_FORMAT        VP11Format;
	DXGI_FORMAT        DX11Format;
	DX11PlaneConfig_t* pDX11Planes;
	int                Packsize;
	int                PitchCoeff;
	ColorSystem_t      CSType;
	int                Subsampling;
	int                CDepth;
	CopyFrameDataFn    Func;
	CopyFrameDataFn    FuncSSSE3;
};

ColorFormat_t GetColorFormat(const CMediaType* pmt);
const FmtConvParams_t& GetFmtConvParams(const ColorFormat_t fmt);
const FmtConvParams_t& GetFmtConvParams(const CMediaType* pmt);
CopyFrameDataFn GetCopyFunction(const FmtConvParams_t& params);

// YUY2, AYUV, RGB32 to D3DFMT_X8R8G8B8, ARGB32 to D3DFMT_A8R8G8B8
void CopyFrameAsIs(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
void CopyGpuFrame_SSE41(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// RGB24 to D3DFMT_X8R8G8B8
void CopyFrameRGB24(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
void CopyRGB24_SSSE3(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch); // 30% faster than CopyFrameRGB24().
// RGB48, b48r to D3DFMT_A16B16G16R16
void CopyFrameRGB48(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
void CopyRGB48_SSSE3(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch); // Not faster than CopyFrameRGB48().
// BGR48 to D3DFMT_A16B16G16R16
void CopyFrameBGR48(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// BGRA64 to D3DFMT_A16B16G16R16
void CopyFrameBGRA64(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// b64a to D3DFMT_A16B16G16R16
void CopyFrameB64A(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// YV12
void CopyFrameYV12(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// Y410 (not used)
void CopyFrameY410(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);

void ConvertXRGB10toXRGB8(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);

void ClipToSurface(const int texW, const int texH, RECT& s, RECT& d);

void set_colorspace(const DXVA2_ExtendedFormat& extfmt, mp_colorspace& colorspace);

BITMAPINFOHEADER* GetBIHfromVIHs(const AM_MEDIA_TYPE* mt);

HRESULT SaveToBMP(BYTE* src, const UINT src_pitch, const UINT width, const UINT height, const UINT bitdepth, const wchar_t* filename);

HRESULT SaveToImage(BYTE* src, const UINT pitch, const UINT width, const UINT height, const UINT bitdepth, const std::wstring_view& filename);

DXVA2_ExtendedFormat SpecifyExtendedFormat(DXVA2_ExtendedFormat exFormat, const FmtConvParams_t& fmtParams, const UINT width, const UINT height);

void GetExtendedFormatString(LPCSTR (&strs)[6], const DXVA2_ExtendedFormat exFormat, const ColorSystem_t colorSystem);
