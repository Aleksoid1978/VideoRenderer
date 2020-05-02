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

#pragma once

class CMpcVideoRenderer;

class CVideoProcessor
{
protected:
	long m_nRefCount = 1;
	CMpcVideoRenderer* m_pFilter = nullptr;

	// settings
	bool m_bShowStats          = false;
	int  m_iTexFormat          = TEXFMT_AUTOINT;
	VPEnableFormats_t m_VPFormats = {true, true, true, true};
	bool m_bDeintDouble        = true;
	bool m_bVPScaling          = true;
	int  m_iChromaScaling      = CHROMA_Bilinear;
	int  m_iUpscaling          = UPSCALE_CatmullRom; // interpolation
	int  m_iDownscaling        = DOWNSCALE_Hamming;  // convolution
	bool m_bInterpolateAt50pct = true;
	bool m_bUseDither          = true;
	int  m_iSwapEffect         = SWAPEFFECT_Discard;

	// Input parameters
	FmtConvParams_t m_srcParams = {};
	CopyFrameDataFn m_pConvertFn = nullptr;
	UINT  m_srcWidth        = 0;
	UINT  m_srcHeight       = 0;
	UINT  m_srcRectWidth    = 0;
	UINT  m_srcRectHeight   = 0;
	int   m_srcPitch        = 0;
	UINT  m_srcLines        = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	CRect m_srcRect;
	DXVA2_ExtendedFormat m_decExFmt = {};
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool  m_bInterlaced = false;
	REFERENCE_TIME m_rtAvgTimePerFrame = 0;

	CRect m_videoRect;
	CRect m_windowRect;
	CRect m_renderRect;

	int m_iRotation = 0;
	bool m_bFinalPass = false;

	DXVA2_ValueRange m_DXVA2ProcAmpRanges[4] = {};
	DXVA2_ProcAmpValues m_DXVA2ProcAmpValues = {};

	HWND m_hWnd = nullptr;
	UINT m_nCurrentAdapter; // set it in subclasses
	DWORD m_VendorId = 0;
	CStringW m_strAdapterDescription;

	bool   m_bPrimaryDisplay     = false;
	double m_dRefreshRate        = 0.0;
	double m_dRefreshRatePrimary = 0.0;

	REFERENCE_TIME m_rtStart = 0;
	int m_FieldDrawn = 0;

	// AlphaBitmap
	bool     m_bAlphaBitmapEnable = false;
	RECT     m_AlphaBitmapRectSrc = {};
	MFVideoNormalizedRect m_AlphaBitmapNRectDest = {};

	// Statistics
	CRenderStats m_RenderStats;
	CStringW m_strStatsStatic1;
	CStringW m_strStatsStatic2;
	CStringW m_strStatsStatic3;
	CStringW m_strStatsStatic4;
	int m_iSrcFromGPU = 0;
	const wchar_t* m_strShaderX = nullptr;
	const wchar_t* m_strShaderY = nullptr;

	// Graph of a function
	CMovingAverage<int> m_Syncs = CMovingAverage<int>(120);
	const int m_Xstep  = 4;
	const int m_Yscale = 2;
	int m_Xstart = 0;
	int m_Yaxis  = 0;

public:
	CVideoProcessor(CMpcVideoRenderer* pFilter) : m_pFilter(pFilter) {}
};
