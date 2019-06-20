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
#include "Include/Version.h"
#include "PropPage.h"

void SetCursor(HWND m_hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(m_hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND m_hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(m_hWnd, nID), lpCursorName);
}

CStringW GetVersionStr()
{
	CStringW version;
#if MPCVR_RELEASE
	version.Format(L"v%S", MPCVR_VERSION_STR);
#else
	version.Format(L"v%S (git-%s-%s)", 
		MPCVR_VERSION_STR,
		_CRT_WIDE(_CRT_STRINGIZE(MPCVR_REV_DATE)),
		_CRT_WIDE(_CRT_STRINGIZE(MPCVR_REV_HASH))
	);
#endif
#ifdef _DEBUG
	version.Append(L" DEBUG");
#endif
	return version;
}

LPCWSTR GetNameAndVersion()
{
	static CStringW version = L"MPC Video Renderer " + GetVersionStr();

	return (LPCWSTR)version;
}

// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
}

CVRMainPPage::~CVRMainPPage()
{
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

	if (!IsWindows8OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}

	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11    ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, BST_CHECKED);
	GetDlgItem(IDC_CHECK4).EnableWindow(FALSE);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"8-bit Integer");
	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"10-bit Integer");
	SendDlgItemMessageW(IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"16-bit Floating Point (DX9 only)");
	SendDlgItemMessageW(IDC_COMBO1, CB_SETCURSEL, m_SetsPP.iSurfaceFmt, 0);

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");
	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

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
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (HIWORD(wParam) == CBN_SELCHANGE) {
			if (nID == IDC_COMBO1) {
				lValue = SendDlgItemMessageW(IDC_COMBO1, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSurfaceFmt) {
					m_SetsPP.iSurfaceFmt = lValue;
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
	CBasePropertyPage(L"StatsProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
}

CVRInfoPPage::~CVRInfoPPage()
{
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

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	CStringW str;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(str)) {
		str.Replace(L"\n", L"\r\n");
	}
	SetDlgItemTextW(IDC_EDIT1, str);

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	return S_OK;
}
