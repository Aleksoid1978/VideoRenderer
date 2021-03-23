/*
* (C) 2020 see Authors.txt
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
#include "Util.h"

LPCWSTR GetWindowsVersion()
{
	if (IsWindows10OrGreater()) {
		return L"10";
	}
	else if (IsWindows8Point1OrGreater()) {
		return L"8.1";
	}
	else if (IsWindows8OrGreater()) {
		return L"8";
	}
	else if (IsWindows7SP1OrGreater()) {
		return L"7 SP1";
	}
	else if (IsWindows7OrGreater()) {
		return L"7";
	}
	return L"Vista or older";
}

std::wstring HR2Str(const HRESULT hr)
{
	std::wstring str;
#define UNPACK_VALUE(VALUE) case VALUE: str = L#VALUE; break;
#define UNPACK_HR_WIN32(VALUE) case (((VALUE) & 0x0000FFFF) | (FACILITY_WIN32 << 16) | 0x80000000): str = L#VALUE; break;
	switch (hr) {
		// Common HRESULT Values https://docs.microsoft.com/en-us/windows/desktop/seccrypto/common-hresult-values
		UNPACK_VALUE(S_OK);
#ifdef _WINERROR_
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
		// some COM Error Codes (Generic) https://docs.microsoft.com/en-us/windows/win32/com/com-error-codes-1
		UNPACK_VALUE(REGDB_E_CLASSNOTREG);
		// some COM Error Codes (UI, Audio, DirectX, Codec) https://docs.microsoft.com/en-us/windows/win32/com/com-error-codes-10
		UNPACK_VALUE(DXGI_STATUS_OCCLUDED);
		UNPACK_VALUE(DXGI_STATUS_MODE_CHANGED);
		UNPACK_VALUE(DXGI_ERROR_INVALID_CALL);
		UNPACK_VALUE(DXGI_ERROR_DEVICE_REMOVED);
		UNPACK_VALUE(DXGI_ERROR_DEVICE_RESET);
		UNPACK_VALUE(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT);
		// some System Error Codes https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes
		UNPACK_HR_WIN32(ERROR_GEN_FAILURE);
		UNPACK_HR_WIN32(ERROR_NOT_SUPPORTED);
		UNPACK_HR_WIN32(ERROR_INSUFFICIENT_BUFFER);
		UNPACK_HR_WIN32(ERROR_MOD_NOT_FOUND);
		UNPACK_HR_WIN32(ERROR_INVALID_WINDOW_HANDLE);
		UNPACK_HR_WIN32(ERROR_CLASS_ALREADY_EXISTS);
#endif
#ifdef _D3D9_H_
		// some D3DERR values https://docs.microsoft.com/en-us/windows/desktop/direct3d9/d3derr
		UNPACK_VALUE(S_PRESENT_OCCLUDED);
		UNPACK_VALUE(S_PRESENT_MODE_CHANGED);
		UNPACK_VALUE(D3DERR_DEVICEHUNG);
		UNPACK_VALUE(D3DERR_DEVICELOST);
		UNPACK_VALUE(D3DERR_DEVICENOTRESET);
		UNPACK_VALUE(D3DERR_DEVICEREMOVED);
		UNPACK_VALUE(D3DERR_DRIVERINTERNALERROR);
		UNPACK_VALUE(D3DERR_INVALIDCALL);
		UNPACK_VALUE(D3DERR_OUTOFVIDEOMEMORY);
		UNPACK_VALUE(D3DERR_WASSTILLDRAWING);
		UNPACK_VALUE(D3DERR_NOTAVAILABLE);
#endif
	default:
		str = fmt::format(L"{:#010x}", (uint32_t)hr);
	};
#undef UNPACK_VALUE
#undef UNPACK_HR_WIN32
	return str;
}

HRESULT GetDataFromResource(LPVOID& data, DWORD& size, UINT resid)
{
	static const HMODULE hModule = (HMODULE)&__ImageBase;

	HRSRC hrsrc = FindResourceW(hModule, MAKEINTRESOURCEW(resid), L"FILE");
	if (!hrsrc) {
		return E_INVALIDARG;
	}
	HGLOBAL hGlobal = LoadResource(hModule, hrsrc);
	if (!hGlobal) {
		return E_FAIL;
	}
	size = SizeofResource(hModule, hrsrc);
	if (!size) {
		return E_FAIL;
	}
	data = LockResource(hGlobal);
	if (!data) {
		return E_FAIL;
	}

	return S_OK;
}
