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

#if __has_include("../revision.h")
	#include "../revision.h"
#endif

// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
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

	m_pVideoRenderer->GetSettings(
		m_bUseD3D11,
		m_bShowStats,
		m_bDeintDouble,
		m_iSurfaceFmt,
		m_iUpscaling,
		m_iDownscaling,
		m_bInterpolateAt50pct);

	CheckDlgButton(IDC_CHECK1, m_bUseD3D11    ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_bShowStats   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK3, m_bDeintDouble ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, BST_CHECKED);
	GetDlgItem(IDC_CHECK4).EnableWindow(FALSE);
	CheckDlgButton(IDC_CHECK5, m_bInterpolateAt50pct ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"8-bit Integer");
	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"10-bit Integer");
	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"16-bit Floating Point (DX9 only)");
	SendDlgItemMessageW(IDC_COMBO1, CB_SETCURSEL, m_iSurfaceFmt, 0);

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_iUpscaling, 0);

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_iDownscaling, 0);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
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
	auto FmtConvParams = GetFmtConvParams(frameinfo.Subtype);
	if (FmtConvParams) {
		str.Append(L"\r\n\r\n  Input");
		str.AppendFormat(L"\r\nFormat : %S", FmtConvParams->str);
		str.AppendFormat(L"\r\nWidth  : %u", frameinfo.Width);
		str.AppendFormat(L"\r\nHeight : %u", frameinfo.Height);
	}
	SetDlgItemTextW(IDC_EDIT1, str);

#ifdef MPCVR_REV_DATE
#ifdef MPCVR_REV_HASH
	str.Format(L"Experimental MPC Video Renderer %s (%s)",
		_CRT_WIDE(_CRT_STRINGIZE(MPCVR_REV_DATE)),
		_CRT_WIDE(_CRT_STRINGIZE(MPCVR_REV_HASH)));
	SetDlgItemTextW(IDC_STATIC1, str);
#endif // MPCVR_REV_HASH
#endif // MPCVR_REV_DATE

	SetCursor(m_hWnd, IDC_ARROW);
	SetCursor(m_hWnd, IDC_COMBO1, IDC_HAND);

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		LRESULT lValue;
		const int nID = LOWORD(wParam);

		if (HIWORD(wParam) == BN_CLICKED) {
			if (nID == IDC_CHECK1) {
				m_bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (HIWORD(wParam) == CBN_SELCHANGE) {
			if (nID == IDC_COMBO1) {
				lValue = SendDlgItemMessageW(IDC_COMBO1, CB_GETCURSEL, 0, 0);
				if (lValue != m_iSurfaceFmt) {
					m_iSurfaceFmt = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_iUpscaling) {
					m_iUpscaling = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_iDownscaling) {
					m_iDownscaling = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	m_pVideoRenderer->SetSettings(
		m_bUseD3D11,
		m_bShowStats,
		m_bDeintDouble,
		m_iSurfaceFmt,
		m_iUpscaling,
		m_iDownscaling,
		m_bInterpolateAt50pct);
	m_pVideoRenderer->SaveSettings();

	return S_OK;
}
