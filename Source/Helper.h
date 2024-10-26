/*
* (C) 2018-2024 see Authors.txt
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
#include <mfobjects.h>
#include "Utils/Util.h"
#include "Utils/StringUtil.h"
#include "csputils.h"
#include "../Include/IMediaSideData.h"

constexpr auto D3DFMT_YV12 = static_cast<D3DFORMAT>(FCC('YV12'));
constexpr auto D3DFMT_NV12 = static_cast<D3DFORMAT>(FCC('NV12'));
constexpr auto D3DFMT_P010 = static_cast<D3DFORMAT>(FCC('P010'));
constexpr auto D3DFMT_P016 = static_cast<D3DFORMAT>(FCC('P016'));
constexpr auto D3DFMT_P210 = static_cast<D3DFORMAT>(FCC('P210'));
constexpr auto D3DFMT_P216 = static_cast<D3DFORMAT>(FCC('P216'));
constexpr auto D3DFMT_AYUV = static_cast<D3DFORMAT>(FCC('AYUV'));
constexpr auto D3DFMT_Y410 = static_cast<D3DFORMAT>(FCC('Y410'));
constexpr auto D3DFMT_Y416 = static_cast<D3DFORMAT>(FCC('Y416'));

constexpr auto D3DFMT_PLANAR      = static_cast<D3DFORMAT>(0xFFFF);
constexpr auto DXGI_FORMAT_PLANAR = static_cast<DXGI_FORMAT>(0xFFFF);

constexpr UINT PCIV_AMDATI = 0x1002;
constexpr UINT PCIV_NVIDIA = 0x10DE;
constexpr UINT PCIV_INTEL  = 0x8086;

constexpr REFERENCE_TIME INVALID_TIME = INT64_MIN;

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
	const wchar_t* const description;
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
	CF_NV12,
	CF_P010,
	CF_P016,
	CF_YUY2,
	CF_P210,
	CF_P216,
	CF_Y210, // experimental
	CF_Y216, // experimental
	CF_AYUV,
	CF_Y410,
	CF_Y416,
	CF_YV12,
	CF_YV16,
	CF_YV24,
	CF_YUV420P8,
	CF_YUV422P8,
	CF_YUV444P8,
	CF_YUV420P10,
	CF_YUV420P16,
	CF_YUV422P10,
	CF_YUV422P16,
	CF_YUV444P10,
	CF_YUV444P16,
	CF_GBRP8,
	CF_GBRP16,
	CF_RGB24,
	CF_XRGB32,
	CF_ARGB32,
	CF_r210,
	CF_RGB48,
	CF_BGR48,
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
CopyFrameDataFn GetCopyPlaneFunction(const FmtConvParams_t& params);

// YUY2, AYUV, RGB32 to D3DFMT_X8R8G8B8, ARGB32 to D3DFMT_A8R8G8B8
void CopyPlaneAsIs(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
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
// r210
void CopyFrameR210(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);
// YUV444P10
void CopyPlane10to16(const UINT lines, BYTE * dst, UINT dst_pitch, const BYTE * src, int src_pitch);

void ConvertXRGB10toXRGB8(const UINT lines, BYTE* dst, UINT dst_pitch, const BYTE* src, int src_pitch);

void ClipToSurface(const int texW, const int texH, RECT& s, RECT& d);

void set_colorspace(const DXVA2_ExtendedFormat extfmt, mp_colorspace& colorspace);

BITMAPINFOHEADER* GetBIHfromVIHs(const AM_MEDIA_TYPE* mt);

HRESULT SaveToBMP(BYTE* src, const UINT src_pitch, const UINT width, const UINT height, const UINT bitdepth, const wchar_t* filename);

HRESULT SaveToImage(BYTE* src, const UINT pitch, const UINT width, const UINT height, const UINT bitdepth, const std::wstring_view filename);

DXVA2_ExtendedFormat SpecifyExtendedFormat(DXVA2_ExtendedFormat exFormat, const FmtConvParams_t& fmtParams, const UINT width, const UINT height);

void GetExtendedFormatString(LPCSTR (&strs)[6], const DXVA2_ExtendedFormat exFormat, const ColorSystem_t colorSystem);
