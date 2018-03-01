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

const wchar_t* D3DFormatToString(D3DFORMAT format)
{
#define UNPACK_VALUE(VALUE) case VALUE: return L#VALUE;
	switch (format) {
	//	UNPACK_VALUE(D3DFMT_R8G8B8);
		UNPACK_VALUE(D3DFMT_A8R8G8B8);
		UNPACK_VALUE(D3DFMT_X8R8G8B8);
	//	UNPACK_VALUE(D3DFMT_R5G6B5);
	//	UNPACK_VALUE(D3DFMT_X1R5G5B5);
	//	UNPACK_VALUE(D3DFMT_A1R5G5B5);
	//	UNPACK_VALUE(D3DFMT_A4R4G4B4);
	//	UNPACK_VALUE(D3DFMT_R3G3B2);
	//	UNPACK_VALUE(D3DFMT_A8);
	//	UNPACK_VALUE(D3DFMT_A8R3G3B2);
	//	UNPACK_VALUE(D3DFMT_X4R4G4B4);
	//	UNPACK_VALUE(D3DFMT_A2B10G10R10);
	//	UNPACK_VALUE(D3DFMT_A8B8G8R8);
	//	UNPACK_VALUE(D3DFMT_X8B8G8R8);
	//	UNPACK_VALUE(D3DFMT_G16R16);
		UNPACK_VALUE(D3DFMT_A2R10G10B10);
	//	UNPACK_VALUE(D3DFMT_A16B16G16R16);
		UNPACK_VALUE(D3DFMT_A8P8);
		UNPACK_VALUE(D3DFMT_P8);
	//	UNPACK_VALUE(D3DFMT_L8);
	//	UNPACK_VALUE(D3DFMT_A8L8);
	//	UNPACK_VALUE(D3DFMT_A4L4);
	//	UNPACK_VALUE(D3DFMT_V8U8);
	//	UNPACK_VALUE(D3DFMT_L6V5U5);
	//	UNPACK_VALUE(D3DFMT_X8L8V8U8);
	//	UNPACK_VALUE(D3DFMT_Q8W8V8U8);
	//	UNPACK_VALUE(D3DFMT_V16U16);
	//	UNPACK_VALUE(D3DFMT_A2W10V10U10);
		UNPACK_VALUE(D3DFMT_UYVY);
	//	UNPACK_VALUE(D3DFMT_R8G8_B8G8);
		UNPACK_VALUE(D3DFMT_YUY2);
	//	UNPACK_VALUE(D3DFMT_G8R8_G8B8);
	//	UNPACK_VALUE(D3DFMT_DXT1);
	//	UNPACK_VALUE(D3DFMT_DXT2);
	//	UNPACK_VALUE(D3DFMT_DXT3);
	//	UNPACK_VALUE(D3DFMT_DXT4);
	//	UNPACK_VALUE(D3DFMT_DXT5);
	//	UNPACK_VALUE(D3DFMT_D16_LOCKABLE);
	//	UNPACK_VALUE(D3DFMT_D32);
	//	UNPACK_VALUE(D3DFMT_D15S1);
	//	UNPACK_VALUE(D3DFMT_D24S8);
	//	UNPACK_VALUE(D3DFMT_D24X8);
	//	UNPACK_VALUE(D3DFMT_D24X4S4);
	//	UNPACK_VALUE(D3DFMT_D16);
	//	UNPACK_VALUE(D3DFMT_D32F_LOCKABLE);
	//	UNPACK_VALUE(D3DFMT_D24FS8);
	//	UNPACK_VALUE(D3DFMT_D32_LOCKABLE);
	//	UNPACK_VALUE(D3DFMT_S8_LOCKABLE);
	//	UNPACK_VALUE(D3DFMT_L16);
	//	UNPACK_VALUE(D3DFMT_VERTEXDATA);
	//	UNPACK_VALUE(D3DFMT_INDEX16);
	//	UNPACK_VALUE(D3DFMT_INDEX32);
	//	UNPACK_VALUE(D3DFMT_Q16W16V16U16);
	//	UNPACK_VALUE(D3DFMT_MULTI2_ARGB8);
	//	UNPACK_VALUE(D3DFMT_R16F);
	//	UNPACK_VALUE(D3DFMT_G16R16F);
		UNPACK_VALUE(D3DFMT_A16B16G16R16F);
	//	UNPACK_VALUE(D3DFMT_R32F);
	//	UNPACK_VALUE(D3DFMT_G32R32F);
		UNPACK_VALUE(D3DFMT_A32B32G32R32F);
	//	UNPACK_VALUE(D3DFMT_CxV8U8);
	//	UNPACK_VALUE(D3DFMT_A1);
	//	UNPACK_VALUE(D3DFMT_A2B10G10R10_XR_BIAS);
	//	UNPACK_VALUE(D3DFMT_BINARYBUFFER);
	case FCC('NV12'): return L"D3DFMT_NV12";
	case FCC('YV12'): return L"D3DFMT_YV12";
	case FCC('P010'): return L"D3DFMT_P010";
	case FCC('AYUV'): return L"D3DFMT_AYUV";
	case FCC('AIP8'): return L"D3DFMT_AIP8";
	case FCC('AI44'): return L"D3DFMT_AI44";
	};
#undef UNPACK_VALUE

	return L"D3DFMT_UNKNOWN";
}
