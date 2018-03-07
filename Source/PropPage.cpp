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
#include <atlstr.h>
#include "Helper.h"

#include "PropPage.h"

// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(NAME("MainProp"), lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == NULL) return E_POINTER;

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	D3DFORMAT Format = D3DFMT_UNKNOWN;
	UINT Width = 0;
	UINT Height = 0;

	CStringW str(L"  Input");
	str.AppendFormat(L"\r\nFormat: %s", D3DFormatToString(Format));
	str.AppendFormat(L"\r\nWidth : %u", Width);
	str.AppendFormat(L"\r\nHeight: %u", Height);

	SetDlgItemTextW(m_hwnd, IDC_EDIT1, str);

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	/*switch (uMsg) {
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_DEFAULT) {
			// User clicked the 'Revert to Default' button.
			SetDirty();
			return (LRESULT)1;
		}
		break;
	}*/

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	return S_OK;
}
