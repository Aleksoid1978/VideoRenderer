/*
 * (C) 2018-2023 see Authors.txt
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
#include "DisplayConfig.h"
#include "PropPage.h"

void SetCursor(HWND hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(hWnd, nID), lpCursorName);
}

inline void ComboBox_AddStringData(HWND hWnd, int nIDComboBox, LPCWSTR str, LONG_PTR data)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_ADDSTRING, 0, (LPARAM)str);
	if (lValue != CB_ERR) {
		SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETITEMDATA, lValue, data);
	}
}

inline LONG_PTR ComboBox_GetCurItemData(HWND hWnd, int nIDComboBox)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCURSEL, 0, 0);
	if (lValue != CB_ERR) {
		lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, lValue, 0);
	}
	return lValue;
}

void ComboBox_SelectByItemData(HWND hWnd, int nIDComboBox, LONG_PTR data)
{
	LRESULT lCount = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCOUNT, 0, 0);
	if (lCount != CB_ERR) {
		for (int idx = 0; idx < lCount; idx++) {
			const LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, idx, 0);
			if (data == lValue) {
				SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETCURSEL, idx, 0);
				break;
			}
		}
	}
}


// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
	DLog(L"CVRMainPPage()");
}

CVRMainPPage::~CVRMainPPage()
{
	DLog(L"~CVRMainPPage()");
}

void CVRMainPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11             ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats            ? BST_CHECKED : BST_UNCHECKED);

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO1, m_SetsPP.iTexFormat);

	CheckDlgButton(IDC_CHECK7, m_SetsPP.VPFmts.bNV12          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK8, m_SetsPP.VPFmts.bP01x          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK9, m_SetsPP.VPFmts.bYUY2          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_SetsPP.VPFmts.bOther         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling            ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK13, m_SetsPP.bVPSuperRes          ? BST_CHECKED : BST_UNCHECKED);

    CheckDlgButton(IDC_CHECK18, m_SetsPP.bHdrPreferDoVi       ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK12, m_SetsPP.bHdrPassthrough      ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK14, m_SetsPP.bConvertToSdr        ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO7, CB_SETCURSEL, m_SetsPP.iHdrToggleDisplay, 0);
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETPOS, 1, m_SetsPP.iHdrOsdBrightness);

	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK10, m_SetsPP.bUseDither           ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK17, m_SetsPP.bDeintBlend          ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK11, m_SetsPP.bExclusiveFS         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK15, m_SetsPP.bVBlankBeforePresent ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK16, m_SetsPP.bReinitByDisplay     ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO6, CB_SETCURSEL, m_SetsPP.iResizeStats, 0);

	SendDlgItemMessageW(IDC_COMBO5, CB_SETCURSEL, m_SetsPP.iChromaScaling, 0);
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);

	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);
}

void CVRMainPPage::EnableControls()
{
	if (!IsWindows8OrGreater()) { // Windows 7
		const BOOL bEnable = !m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_STATIC1).EnableWindow(bEnable); // not working for GROUPBOX
		GetDlgItem(IDC_STATIC2).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK7).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK8).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK9).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK4).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK3).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK5).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC3).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO4).EnableWindow(bEnable);
	}
	else if (IsWindows10OrGreater()) {
		const BOOL bEnable = m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_CHECK12).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC5).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO7).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC6).EnableWindow(bEnable);
		GetDlgItem(IDC_SLIDER1).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK13).EnableWindow(bEnable && m_SetsPP.bVPScaling);
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

	m_pVideoRenderer->GetSettings(m_SetsPP);

	if (!IsWindows7SP1OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}
	if (!IsWindows10OrGreater()) {
		GetDlgItem(IDC_CHECK12).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC4).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC5).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO7).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC6).EnableWindow(FALSE);
		GetDlgItem(IDC_SLIDER1).EnableWindow(FALSE);
		GetDlgItem(IDC_CHECK13).EnableWindow(FALSE);
	}

	EnableControls();

	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Fixed font size");
	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase font by window");
	//SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase by DPI"); // TODO

	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"Auto 8/10-bit Integer",  0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"8-bit Integer",          8);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"10-bit Integer",        10);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"16-bit Floating Point", 16);

	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"not used");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"used for fullscreen");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"always used");

	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Jinc2m");

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");

	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETRANGE, 0, MAKELONG(0, 2));
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETTICFREQ, 1, 0);

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	SetControls();

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
				m_SetsPP.bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_SetsPP.bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_SetsPP.bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_SetsPP.bVPScaling = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				GetDlgItem(IDC_CHECK13).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK13) {
				m_SetsPP.bVPSuperRes = IsDlgButtonChecked(IDC_CHECK13) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK7) {
				m_SetsPP.VPFmts.bNV12 = IsDlgButtonChecked(IDC_CHECK7) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK8) {
				m_SetsPP.VPFmts.bP01x = IsDlgButtonChecked(IDC_CHECK8) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK9) {
				m_SetsPP.VPFmts.bYUY2 = IsDlgButtonChecked(IDC_CHECK9) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK4) {
				m_SetsPP.VPFmts.bOther = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK10) {
				m_SetsPP.bUseDither = IsDlgButtonChecked(IDC_CHECK10) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK17) {
				m_SetsPP.bDeintBlend = IsDlgButtonChecked(IDC_CHECK17) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK11) {
				m_SetsPP.bExclusiveFS = IsDlgButtonChecked(IDC_CHECK11) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK15) {
				m_SetsPP.bVBlankBeforePresent = IsDlgButtonChecked(IDC_CHECK15) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK16) {
				m_SetsPP.bReinitByDisplay = IsDlgButtonChecked(IDC_CHECK16) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK18) {
				m_SetsPP.bHdrPreferDoVi = IsDlgButtonChecked(IDC_CHECK18) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK12) {
				m_SetsPP.bHdrPassthrough = IsDlgButtonChecked(IDC_CHECK12) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK14) {
				m_SetsPP.bConvertToSdr = IsDlgButtonChecked(IDC_CHECK14) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON1) {
				m_SetsPP.SetDefault();
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (HIWORD(wParam) == CBN_SELCHANGE) {
			if (nID == IDC_COMBO6) {
				lValue = SendDlgItemMessageW(IDC_COMBO6, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iResizeStats) {
					m_SetsPP.iResizeStats = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO1) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO1);
				if (lValue != m_SetsPP.iTexFormat) {
					m_SetsPP.iTexFormat = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO7) {
				lValue = SendDlgItemMessageW(IDC_COMBO7, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iHdrToggleDisplay) {
					m_SetsPP.iHdrToggleDisplay = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO5) {
				lValue = SendDlgItemMessageW(IDC_COMBO5, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iChromaScaling) {
					m_SetsPP.iChromaScaling = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iUpscaling) {
					m_SetsPP.iUpscaling = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iDownscaling) {
					m_SetsPP.iDownscaling = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
			if (nID == IDC_COMBO4) {
				lValue = SendDlgItemMessageW(IDC_COMBO4, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSwapEffect) {
					m_SetsPP.iSwapEffect = lValue;
					SetDirty();
					return (LRESULT)1;
				}
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER1)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER1, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iHdrOsdBrightness) {
				m_SetsPP.iHdrOsdBrightness = lValue;
				SetDirty();
			}
			return (LRESULT)1;
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();

	return S_OK;
}

// CVRInfoPPage

CVRInfoPPage::CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InfoProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
	DLog(L"CVRInfoPPage()");
}

CVRInfoPPage::~CVRInfoPPage()
{
	DLog(L"~CVRInfoPPage()");

	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

HRESULT CVRInfoPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInfoPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRInfoPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	// init monospace font
	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	std::wstring strInfo(L"Windows ");
	strInfo.append(GetWindowsVersion());
	strInfo.append(L"\r\n");

	std::wstring strVP;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(strVP)) {
		str_replace(strVP, L"\n", L"\r\n");
		strInfo.append(strVP);
	}

#ifdef _DEBUG
	{
		std::vector<DisplayConfig_t> displayConfigs;

		bool ret = GetDisplayConfigs(displayConfigs);

		strInfo.append(L"\r\n");

		for (const auto& dc : displayConfigs) {
			double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
			strInfo += std::format(L"\r\n{} - {:.3f} Hz", dc.displayName, freq);

			if (dc.bitsPerChannel) { // if bitsPerChannel is not set then colorEncoding and other values are invalid
				const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
				if (colenc) {
					strInfo += std::format(L" {}", colenc);
				}
				strInfo += std::format(L" {}-bit", dc.bitsPerChannel);
			}

			const wchar_t* output = OutputTechnologyToString(dc.outputTechnology);
			if (output) {
				strInfo += std::format(L" {}", output);
			}
		}
	}
#endif

	SetDlgItemTextW(IDC_EDIT1, strInfo.c_str());

	return S_OK;
}

INT_PTR CVRInfoPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CLOSE) {
		// fixed Esc handling when EDITTEXT control has ES_MULTILINE property and is in focus
		return (LRESULT)1;
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}
