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
#include <algorithm>
#include "Helper.h"

const wchar_t* D3DFormatToString(D3DFORMAT format)
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

const wchar_t* DXGIFormatToString(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R8G8B8A8_UNORM: return L"R8G8B8A8_UNORM";
	case DXGI_FORMAT_B8G8R8A8_UNORM: return L"B8G8R8A8_UNORM";
	case DXGI_FORMAT_B8G8R8X8_UNORM: return L"B8G8R8X8_UNORM";
	case DXGI_FORMAT_AYUV:           return L"AYUV";
	case DXGI_FORMAT_NV12:           return L"NV12";
	case DXGI_FORMAT_P010:           return L"P010";
	case DXGI_FORMAT_420_OPAQUE:     return L"420_OPAQUE";
	case DXGI_FORMAT_YUY2:           return L"YUY2";
	case DXGI_FORMAT_AI44:           return L"AI44";
	case DXGI_FORMAT_IA44:           return L"IA44";
	case DXGI_FORMAT_P8:             return L"P8";
	case DXGI_FORMAT_A8P8:           return L"A8P8";
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

void CopyFrameData(const D3DFORMAT format, const UINT width, const UINT height, BYTE* dst, const UINT dst_pitch, BYTE* src, const UINT src_pitch, const UINT src_size)
{
	//UINT linesize = std::min(src_pitch, dst_pitch); // TODO

	if (format == D3DFMT_X8R8G8B8) {
		const UINT linesize = width * 4;
		src += src_pitch * (height - 1);

		for (UINT y = 0; y < height; ++y) {
			memcpy(dst, src, linesize);
			src -= src_pitch;
			dst += dst_pitch;
		}
	}
	else if (src_pitch == dst_pitch) {
		memcpy(dst, src, src_size);
	}
	else if (format == D3DFMT_YV12) {
		UINT _dst_pitch = dst_pitch;
		for (UINT y = 0; y < height; ++y) {
			memcpy(dst, src, width);
			src += src_pitch;
			dst += _dst_pitch;
		}

		const UINT chromaline = width / 2;
		const UINT chromaheight = height / 2;
		const UINT chromapitch = src_pitch / 2;
		_dst_pitch /= 2;
		for (UINT y = 0; y < chromaheight; ++y) {
			memcpy(dst, src, chromaline);
			src += chromapitch;
			dst += _dst_pitch;
			memcpy(dst, src, chromaline);
			src += chromapitch;
			dst += _dst_pitch;
		}
	}
	else {
		UINT linesize = width;
		UINT lines = height;
		if (format == D3DFMT_YUY2) {
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
