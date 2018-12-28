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
#include <ntverp.h>
#include <DXGI1_2.h>
#include <dxva2api.h>
#include <strmif.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d2d1.h>
#include <dwrite.h>
#include <evr9.h> // for IMFVideoProcessor
#include "IVideoRenderer.h"
#include "Helper.h"
#include "FrameStats.h"

class CDX11VideoProcessor
	: public IMFVideoProcessor
{
private:
	long m_nRefCount = 1;
	CBaseRenderer * m_pFilter = nullptr;

	bool m_bDeintDouble = false;
	bool m_bShowStats = false;

	HMODULE m_hD3D11Lib = nullptr;
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pImmediateContext;
	CComPtr<ID3D11VideoContext> m_pVideoContext;
#if VER_PRODUCTBUILD >= 10000
	CComPtr<ID3D11VideoContext1> m_pVideoContext1;
#endif
	CComPtr<ID3D11VideoDevice> m_pVideoDevice;
	CComPtr<ID3D11VideoProcessor> m_pVideoProcessor;
	CComPtr<ID3D11VideoProcessorEnumerator> m_pVideoProcessorEnum;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D_Decode;
	CComPtr<ID3D11Texture2D> m_pSrcTexture2D_RGB;
	CComPtr<IDXGIFactory2> m_pDXGIFactory2;
	CComPtr<IDXGISwapChain1> m_pDXGISwapChain1;

	CComPtr<IDirect3DTexture9> m_pSrcTexture9;
	CComPtr<IDirect3DSurface9> m_pSrcSurface9;
	HANDLE m_sharedHandle = nullptr;
	bool m_bCanUseSharedHandle = true;

	CMediaType m_mt;
	D3DFORMAT m_srcD3DFormat = D3DFMT_UNKNOWN;
	DXGI_FORMAT m_srcDXGIFormat = DXGI_FORMAT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	UINT m_srcPitch = 0;
	typedef void(*CopyFrameDataFn)(const UINT height, BYTE* dst, UINT dst_pitch, BYTE* src, UINT src_pitch);
	CopyFrameDataFn m_pConvertFn = nullptr;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;
	CRect m_srcRect;
	CRect m_trgRect;
	DXGI_FORMAT m_VPOutputFmt = DXGI_FORMAT_B8G8R8X8_UNORM;

	D3D11_VIDEO_FRAME_FORMAT m_SampleFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
	int m_FieldDrawn = 0;

	DXGI_FORMAT m_D3D11_Src_Format = DXGI_FORMAT_UNKNOWN;
	UINT m_D3D11_Src_Width = 0;
	UINT m_D3D11_Src_Height = 0;

	D3D11_VIDEO_PROCESSOR_CAPS m_VPCaps = {};
	D3D11_VIDEO_PROCESSOR_FILTER_RANGE m_VPFilterRange[4] = {};
	struct {
		BOOL Enabled;
		int Level;
	} m_VPFilterSettings[4] = {};

	CRect m_videoRect;
	CRect m_windowRect;

	HWND m_hWnd = nullptr;

	DWORD m_VendorId = 0;
	CString m_strAdapterDescription;

	CComPtr<ID2D1Factory> m_pD2DFactory;
	CComPtr<IDWriteFactory> m_pDWriteFactory;
	CComPtr<IDWriteTextFormat> m_pTextFormat;
	CComPtr<ID2D1RenderTarget> m_pD2D1RenderTarget;
	CComPtr<ID2D1SolidColorBrush> m_pD2DBrush;
	CComPtr<ID2D1SolidColorBrush> m_pD2DBrushBlack;

	CFrameStats m_FrameStats;
	CFrameStats m_DrawnFrameStats; //TODO: use CRenderStats
	CRenderStats m_RenderStats;

public:
	CDX11VideoProcessor(CBaseRenderer* pFilter);
	~CDX11VideoProcessor();

	HRESULT Init(const int iSurfaceFmt);
	void ClearD3D11();

	HRESULT SetDevice(ID3D11Device *pDevice, ID3D11DeviceContext *pContext);
	HRESULT InitSwapChain(const HWND hwnd, UINT width = 0, UINT height = 0);

	BOOL VerifyMediaType(const CMediaType* pmt);
	BOOL InitMediaType(const CMediaType* pmt);
	HRESULT Initialize(const UINT width, const UINT height, const DXGI_FORMAT dxgiFormat);
	void Start();

	HRESULT ProcessSample(IMediaSample* pSample);
	HRESULT CopySample(IMediaSample* pSample);
	// Render: 1 - render first fied or progressive frame, 2 - render second fied, 0 or other - forced repeat of render.
	HRESULT Render(int field);
	HRESULT FillBlack();
	void StopInputBuffer() {}
	bool SecondFramePossible() { return m_bDeintDouble && m_SampleFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; }

	void SetVideoRect(const CRect& videoRect) { m_videoRect = videoRect; }
	void SetWindowRect(const CRect& windowRect) { m_windowRect = windowRect; }

	HRESULT GetVideoSize(long *pWidth, long *pHeight);
	HRESULT GetAspectRatio(long *plAspectX, long *plAspectY);
	HRESULT GetFrameInfo(VRFrameInfo* pFrameInfo);
	HRESULT GetAdapterDecription(CStringW& str);

	bool GetDeintDouble() { return m_bDeintDouble; }
	void SetDeintDouble(bool value) { m_bDeintDouble = value; };
	bool GetShowStats() { return m_bShowStats; }
	void SetShowStats(bool value) { m_bShowStats = value; };

private:
	HRESULT ProcessDX11(ID3D11Texture2D* pRenderTarget, const bool second);
	HRESULT DrawStats();

public:
	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IMFVideoProcessor
	STDMETHODIMP GetAvailableVideoProcessorModes(UINT *lpdwNumProcessingModes, GUID **ppVideoProcessingModes) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorCaps(LPGUID lpVideoProcessorMode, DXVA2_VideoProcessorCaps *lpVideoProcessorCaps) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP SetVideoProcessorMode(LPGUID lpMode) { return E_NOTIMPL; }
	STDMETHODIMP GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange);
	STDMETHODIMP GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values);
	STDMETHODIMP SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues);
	STDMETHODIMP GetFilteringRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange) { return E_NOTIMPL; }
	STDMETHODIMP GetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP SetFilteringValue(DWORD dwProperty, DXVA2_Fixed32 *pValue) { return E_NOTIMPL; }
	STDMETHODIMP GetBackgroundColor(COLORREF *lpClrBkg);
	STDMETHODIMP SetBackgroundColor(COLORREF ClrBkg) { return E_NOTIMPL; }
};
