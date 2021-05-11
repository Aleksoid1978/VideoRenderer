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

#include "stdafx.h"
#include <dxva2api.h>
#include "Helper.h"
#include "DX9Helper.h"
#include "DX9Device.h"

// CDX9Device

CDX9Device::CDX9Device()
{
	HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		return;
	}

	DXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		m_pD3DEx.Release();
		return;
	}

	m_evInit.Reset();
	m_evQuit.Reset();
	m_evThreadFinishJob.Reset();

	m_deviceThread = std::thread([this] { DeviceThreadFunc(); });
}

CDX9Device::~CDX9Device()
{
	if (m_deviceThread.joinable()) {
		m_evQuit.Set();
		m_deviceThread.join();
	}

	ReleaseDX9Device();

	m_pD3DDeviceManager.Release();
	m_nResetTocken = 0;

	m_pD3DEx.Release();
}

void CDX9Device::DeviceThreadFunc()
{
	HANDLE hEvts[] = { m_evInit, m_evQuit };

	for (;;) {
		const auto dwObject = WaitForMultipleObjects(std::size(hEvts), hEvts, FALSE, INFINITE);
		m_hrThread = E_FAIL;
		switch (dwObject) {
			case WAIT_OBJECT_0:
				m_hrThread = InitDX9DeviceInternal(&m_bChangeDeviceThread);
				m_evThreadFinishJob.Set();
				break;
			default:
				return;
		}
	}
}

HRESULT CDX9Device::InitDX9DeviceInternal(bool* pChangeDevice)
{
	DLog(L"CDX9Device::InitDX9DeviceInternal()");

	CheckPointer(m_pD3DEx, E_FAIL);

	const UINT currentAdapter = GetAdapter(m_hDX9Wnd, m_pD3DEx);
	bool bTryToReset = (currentAdapter == m_nCurrentAdapter9) && m_pD3DDevEx;
	if (!bTryToReset) {
		ReleaseDX9Device();
		m_nCurrentAdapter9 = currentAdapter;
	}

#ifdef _DEBUG
	D3DADAPTER_IDENTIFIER9 AdapID9 = {};
	if (S_OK == m_pD3DEx->GetAdapterIdentifier(m_nCurrentAdapter9, 0, &AdapID9)) {
		DLog(L"Graphics D3D9 adapter: {} ({:04X}:{:04X})", A2WStr(AdapID9.Description), AdapID9.VendorId, AdapID9.DeviceId);
	}
#endif

	ZeroMemory(&m_DisplayMode, sizeof(D3DDISPLAYMODEEX));
	m_DisplayMode.Size = sizeof(D3DDISPLAYMODEEX);
	HRESULT hr = m_pD3DEx->GetAdapterDisplayModeEx(m_nCurrentAdapter9, &m_DisplayMode, nullptr);
	DLog(L"Display Mode: {}x{}, {}{}", m_DisplayMode.Width, m_DisplayMode.Height, m_DisplayMode.RefreshRate, (m_DisplayMode.ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED) ? 'i' : 'p');

	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));
	m_d3dpp.Windowed = TRUE;
	m_d3dpp.hDeviceWindow = m_hDX9Wnd;
	m_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	m_d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
	m_d3dpp.BackBufferCount = 1;
	m_d3dpp.BackBufferWidth = m_DisplayMode.Width;
	m_d3dpp.BackBufferHeight = m_DisplayMode.Height;
	m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	if (bTryToReset) {
		bTryToReset = SUCCEEDED(hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr));
		DLog(L"    => ResetEx() : {}", HR2Str(hr));
	}

	if (!bTryToReset) {
		ReleaseDX9Device();
		hr = m_pD3DEx->CreateDeviceEx(
			m_nCurrentAdapter9, D3DDEVTYPE_HAL, m_hDX9Wnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS,
			&m_d3dpp, nullptr, &m_pD3DDevEx);
		DLog(L"    => CreateDeviceEx() : {}", HR2Str(hr));
	}

	if (FAILED(hr)) {
		return hr;
	}
	if (!m_pD3DDevEx) {
		return E_FAIL;
	}

	while (hr == D3DERR_DEVICELOST) {
		DLog(L"    => D3DERR_DEVICELOST. Trying to Reset.");
		hr = m_pD3DDevEx->CheckDeviceState(m_hDX9Wnd);
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		DLog(L"    => D3DERR_DEVICENOTRESET");
		hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr);
	}

	if (S_OK == hr && !bTryToReset) {
		hr = m_pD3DDeviceManager->ResetDevice(m_pD3DDevEx, m_nResetTocken);
	}

	if (pChangeDevice) {
		*pChangeDevice = !bTryToReset;
	}

	return hr;
}

HRESULT CDX9Device::InitDX9Device(const HWND hwnd, bool* pChangeDevice/* = nullptr*/)
{
	CheckPointer(m_pD3DEx, E_FAIL);

	m_hDX9Wnd = hwnd;

	m_bChangeDeviceThread = false;
	m_evInit.Set();
	WaitForSingleObject(m_evThreadFinishJob, INFINITE);

	if (pChangeDevice) {
		*pChangeDevice = m_bChangeDeviceThread;
	}

	return m_hrThread;
}

void CDX9Device::ReleaseDX9Device()
{
	m_pD3DDevEx.Release();
}
