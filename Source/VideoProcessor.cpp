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

#include "DisplayConfig.h"
#include "Helper.h"
#include "IVideoRenderer.h"

#include "VideoProcessor.h"

HRESULT CVideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
{
	CheckPointer(pWidth, E_POINTER);
	CheckPointer(pHeight, E_POINTER);

	if (m_iRotation == 90 || m_iRotation == 270) {
		*pWidth  = m_srcRectHeight;
		*pHeight = m_srcRectWidth;
	} else {
		*pWidth  = m_srcRectWidth;
		*pHeight = m_srcRectHeight;
	}

	return S_OK;
}

HRESULT CVideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
{
	CheckPointer(plAspectX, E_POINTER);
	CheckPointer(plAspectY, E_POINTER);

	if (m_iRotation == 90 || m_iRotation == 270) {
		*plAspectX = m_srcAspectRatioY;
		*plAspectY = m_srcAspectRatioX;
	} else {
		*plAspectX = m_srcAspectRatioX;
		*plAspectY = m_srcAspectRatioY;
	}

	return S_OK;
}

void CVideoProcessor::SetTexFormat(int value)
{
	switch (value) {
	case TEXFMT_AUTOINT:
	case TEXFMT_8INT:
	case TEXFMT_10INT:
	case TEXFMT_16FLOAT:
		m_iTexFormat = value;
		break;
	default:
		DLog(L"CVideoProcessor::SetTexFormat() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
}

void CVideoProcessor::UpdateDiplayInfo()
{
	const HMONITOR hMon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	const HMONITOR hMonPrimary = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);

	MONITORINFOEXW mi = { sizeof(mi) };
	GetMonitorInfoW(hMon, (MONITORINFO*)&mi);
	m_dRefreshRate = GetRefreshRate(mi.szDevice);
	if (hMon == hMonPrimary) {
		m_bPrimaryDisplay = true;
		m_dRefreshRatePrimary = m_dRefreshRate;
	} else {
		m_bPrimaryDisplay = false;
		GetMonitorInfoW(hMonPrimary, (MONITORINFO*)&mi);
		m_dRefreshRatePrimary = GetRefreshRate(mi.szDevice);
	}
}
