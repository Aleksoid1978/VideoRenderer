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

#include <atltypes.h>

class CDX9VideoProcessor
{
private:
	HMODULE m_hD3D9Lib = nullptr;
	HMODULE m_hDxva2Lib = nullptr;
	CComPtr<IDirect3D9Ex>       m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex> m_pD3DDevEx;
	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT m_nResetTocken = 0;
	HANDLE m_hDevice = nullptr;

	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	DXVA2_Fixed32 m_DXVA2ProcAmpValues[4] = {};
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;
	DWORD m_frame = 0;

public:
	CDX9VideoProcessor();
	~CDX9VideoProcessor();

	HRESULT Init();
	void ClearDX9();
};
