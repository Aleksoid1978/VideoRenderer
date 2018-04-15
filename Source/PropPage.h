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

#include "BaseClasses\streams.h"
#include "IVideoRenderer.h"

class __declspec(uuid("DA46D181-07D6-441D-B314-019AEB10148A"))
	CVRMainPPage : public CBasePropertyPage, public CWindow
{
	HFONT m_hMonoFont = nullptr;
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;

	bool m_bUseD3D11    = false;
	bool m_bDeintDouble = false;
	bool m_bAllow10Bit  = false;
	bool m_bShowStats   = false;

public:
	CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRMainPPage();

private:
	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;
};
