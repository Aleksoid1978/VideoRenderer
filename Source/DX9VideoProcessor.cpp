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

#include <vector>
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include "Helper.h"
#include "DX9VideoProcessor.h"

// CDX9VideoProcessor

CDX9VideoProcessor::CDX9VideoProcessor()
{
}

CDX9VideoProcessor::~CDX9VideoProcessor()
{
	if (m_pD3DDeviceManager) {
		if (m_hDevice != INVALID_HANDLE_VALUE) {
			m_pD3DDeviceManager->CloseDeviceHandle(m_hDevice);
			m_hDevice = INVALID_HANDLE_VALUE;
		}
		m_pD3DDeviceManager.Release();
	}

	ClearDX9();

	if (m_hDxva2Lib) {
		FreeLibrary(m_hDxva2Lib);
	}
	if (m_hD3D9Lib) {
		FreeLibrary(m_hD3D9Lib);
	}
}

HRESULT CDX9VideoProcessor::Init()
{
	if (!m_hD3D9Lib) {
		m_hD3D9Lib = LoadLibraryW(L"d3d9.dll");
	}
	if (!m_hDxva2Lib) {
		m_hDxva2Lib = LoadLibraryW(L"dxva2.dll");
	}
	if (!m_hD3D9Lib || !m_hDxva2Lib) {
		return E_FAIL;
	}

	HRESULT(WINAPI *pfnDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex**);
	(FARPROC &)pfnDirect3DCreate9Ex = GetProcAddress(m_hD3D9Lib, "Direct3DCreate9Ex");

	HRESULT hr = pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		hr = pfnDirect3DCreate9Ex(D3D9b_SDK_VERSION, &m_pD3DEx);
	}
	if (!m_pD3DEx) {
		return E_FAIL;
	}

	HRESULT(WINAPI *pfnDXVA2CreateDirect3DDeviceManager9)(UINT* pResetToken, IDirect3DDeviceManager9** ppDeviceManager);
	(FARPROC &)pfnDXVA2CreateDirect3DDeviceManager9 = GetProcAddress(m_hDxva2Lib, "DXVA2CreateDirect3DDeviceManager9");
	pfnDXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		return E_FAIL;
	}

	return S_OK;
}

void CDX9VideoProcessor::ClearDX9()
{
	m_pDXVA2_VP.Release();
	m_pDXVA2_VPService.Release();
	m_pD3DDevEx.Release();
	m_pD3DEx.Release();
}
