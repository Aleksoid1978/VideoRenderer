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
#include "resource.h"
#include "Helper.h"

#include "PropPage.h"

// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(NAME("MainProp"), lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
}

CVRMainPPage::~CVRMainPPage()
{
	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	m_bUseD3D11    = m_pVideoRenderer->GetOptionUseD3D11();
	m_bDeintDouble = m_pVideoRenderer->GetOptionDeintDouble();
	m_bAllow10Bit  = m_pVideoRenderer->GetOptionAllow10Bit();
	m_bShowStats   = m_pVideoRenderer->GetOptionShowStatistics();

	CheckDlgButton(IDC_CHECK1, m_bUseD3D11    ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_bDeintDouble ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK3, m_bAllow10Bit  ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_bShowStats   ? BST_CHECKED : BST_UNCHECKED);

	if (!m_pVideoRenderer->GetActive()) {
		GetDlgItem(IDC_EDIT1).ShowWindow(SW_HIDE);
		return S_OK;
	}
	// init monospace font
	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);
	CStringW str;

	CStringW value;
	if (S_OK == m_pVideoRenderer->get_AdapterDecription(value)) {
		str.Format(L"Graphics adapter: %s", value);
	}

	if (m_pVideoRenderer->get_UsedD3D11()) {
		str.Append(L"\r\nVideoProcessor: Direct3D 11");
	} else {
		str.Append(L"\r\nVideoProcessor: DXVA2");
	}

	DXVA2_VideoProcessorCaps dxva2vpcaps;
	if (S_OK == m_pVideoRenderer->get_DXVA2VPCaps(&dxva2vpcaps)) {
		UINT dt = dxva2vpcaps.DeinterlaceTechnology;
		if (dt & DXVA2_DeinterlaceTech_Mask) {
			str.Append(L"\r\nDeinterlaceTechnology:");
			if (dt & DXVA2_DeinterlaceTech_BOBLineReplicate)       str.Append(L" BOBLineReplicate,");
			if (dt & DXVA2_DeinterlaceTech_BOBVerticalStretch)     str.Append(L" BOBVerticalStretch,");
			if (dt & DXVA2_DeinterlaceTech_BOBVerticalStretch4Tap) str.Append(L" BOBVerticalStretch4Tap,");
			if (dt & DXVA2_DeinterlaceTech_MedianFiltering)        str.Append(L" MedianFiltering,");
			if (dt & DXVA2_DeinterlaceTech_EdgeFiltering)          str.Append(L" EdgeFiltering,");
			if (dt & DXVA2_DeinterlaceTech_FieldAdaptive)          str.Append(L" FieldAdaptive,");
			if (dt & DXVA2_DeinterlaceTech_PixelAdaptive)          str.Append(L" PixelAdaptive,");
			if (dt & DXVA2_DeinterlaceTech_MotionVectorSteered)    str.Append(L" MotionVectorSteered,");
			if (dt & DXVA2_DeinterlaceTech_InverseTelecine)        str.Append(L" InverseTelecine");
			str.TrimRight(',');
		}
		if (dxva2vpcaps.NumForwardRefSamples) {
			str.AppendFormat(L"\r\nForwardRefSamples: %u", dxva2vpcaps.NumForwardRefSamples);
		}
		if (dxva2vpcaps.NumBackwardRefSamples) {
			str.AppendFormat(L"\r\nBackwardRefSamples: %u", dxva2vpcaps.NumBackwardRefSamples);
		}
	}

	VRFrameInfo frameinfo;
	m_pVideoRenderer->get_FrameInfo(&frameinfo);
	str.Append(L"\r\n\r\n  Input");
	str.AppendFormat(L"\r\nFormat: %s", D3DFormatToString(frameinfo.D3dFormat));
	str.AppendFormat(L"\r\nWidth : %u", frameinfo.Width);
	str.AppendFormat(L"\r\nHeight: %u", frameinfo.Height);
	SetDlgItemText(IDC_EDIT1, str);

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_CHECK1) {
			m_bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
			SetDirty();
			return (LRESULT)1;
		}
		if (LOWORD(wParam) == IDC_CHECK2) {
			m_bDeintDouble = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
			SetDirty();
			return (LRESULT)1;
		}
		if (LOWORD(wParam) == IDC_CHECK3) {
			m_bAllow10Bit = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
			SetDirty();
			return (LRESULT)1;
		}
		if (LOWORD(wParam) == IDC_CHECK4) {
			m_bShowStats = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
			SetDirty();
			return (LRESULT)1;
		}
		break;
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	m_pVideoRenderer->SetOptionUseD3D11(m_bUseD3D11);
	m_pVideoRenderer->SetOptionDeintDouble(m_bDeintDouble);
	m_pVideoRenderer->SetOptionAllow10Bit(m_bAllow10Bit);
	m_pVideoRenderer->SetOptionShowStatistics(m_bShowStats);
	m_pVideoRenderer->SaveSettings();

	return S_OK;
}
