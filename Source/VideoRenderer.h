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
#include <d3d9.h>
#include <mfidl.h>
#include <dxva2api.h>
#include <mutex>
#include "IVideoRenderer.h"
#include "D3D11VideoProcessor.h"


const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P010},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
};

struct VideoSurface {
	REFERENCE_TIME Start = 0;
	REFERENCE_TIME End = 0;
	CComPtr<IDirect3DSurface9> pSrcSurface;
	DXVA2_SampleFormat SampleFormat = DXVA2_SampleUnknown;
};

class VideoSurfaceBuffer
{
	std::vector<VideoSurface> m_Surfaces;
	unsigned m_LastPos = 0;

public:
	unsigned Size() const {
		return (unsigned)m_Surfaces.size();
	}
	bool Empty() const {
		return m_Surfaces.empty();
	}
	void Clear() {
		m_Surfaces.clear();
	}
	void Resize(const unsigned size) {
		Clear();
		m_Surfaces.resize(size);
		m_LastPos = Size() - 1;
	}
	VideoSurface& Get() {
		return m_Surfaces[m_LastPos];
	}
	VideoSurface& GetAt(const unsigned pos) {
		unsigned InternalPos = (m_LastPos + 1 + pos) % Size();
		return m_Surfaces[InternalPos];
	}
	void Next() {
		m_LastPos++;
		if (m_LastPos >= Size()) {
			m_LastPos = 0;
		}
	}
};

class CVideoRendererInputPin;

class __declspec(uuid("71F080AA-8661-4093-B15E-4F6903E77D0A"))
	CMpcVideoRenderer
	: public CBaseRenderer
	, public IMFGetService
	, public IBasicVideo2
	, public IVideoWindow
	, public ISpecifyPropertyPages
	, public IVideoRenderer
{
private:
	friend class CVideoRendererInputPin;

	bool m_bOptionUseD3D11 = false;

	bool m_bUsedD3D11 = false; // current state

	DXVA2_SampleFormat m_SampleFormat = DXVA2_SampleProgressiveFrame;

	CMediaType m_mt;
	D3DFORMAT m_srcD3DFormat = D3DFMT_UNKNOWN;
	UINT m_srcWidth = 0;
	UINT m_srcHeight = 0;
	DWORD m_srcAspectRatioX = 0;
	DWORD m_srcAspectRatioY = 0;
	DXVA2_ExtendedFormat m_srcExFmt = {};
	bool m_bInterlaced = false;
	RECT m_srcRect = {};
	RECT m_trgRect = {};
	INT  m_srcPitch = 0;
	VideoSurfaceBuffer m_SrcSamples;

	CRect m_nativeVideoRect;
	CRect m_videoRect;
	CRect m_windowRect;

	HWND m_hWnd = nullptr;
	UINT m_CurrentAdapter = D3DADAPTER_DEFAULT;

	HMODULE m_hD3D9Lib = nullptr;
	CComPtr<IDirect3D9Ex>       m_pD3DEx;
	CComPtr<IDirect3DDevice9Ex> m_pD3DDevEx;
	DWORD m_VendorId = 0;
	CString m_strAdapterDescription;

	D3DDISPLAYMODEEX m_DisplayMode = { sizeof(D3DDISPLAYMODEEX) };
	D3DPRESENT_PARAMETERS m_d3dpp = {};

	HMODULE m_hDxva2Lib = nullptr;

	// DXVA2 VideoProcessor
	CComPtr<IDirectXVideoProcessorService> m_pDXVA2_VPService;
	CComPtr<IDirectXVideoProcessor> m_pDXVA2_VP;
	GUID m_DXVA2VPGuid = GUID_NULL;
	DXVA2_VideoProcessorCaps m_DXVA2VPcaps = {};
	DXVA2_Fixed32 m_DXVA2ProcAmpValues[4] = {};
	std::vector<DXVA2_VideoSample> m_DXVA2Samples;
	DWORD m_frame = 0;

	D3DFORMAT m_DXVA2_VP_Format = D3DFMT_UNKNOWN;
	UINT m_DXVA2_VP_Width = 0;
	UINT m_DXVA2_VP_Height = 0;

	// D3D11 VideoProcessor
	CD3D11VideoProcessor m_D3D11_VP;

	CComPtr<IDirect3DDeviceManager9> m_pD3DDeviceManager;
	UINT                             m_nResetTocken = 0;
	HANDLE                           m_hDevice = nullptr;

	std::mutex m_mutex;
	
	FILTER_STATE m_filterState = State_Stopped;

public:
	CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr);
	~CMpcVideoRenderer();

private:
	HRESULT InitDirect3D9();

	BOOL InitMediaType(const CMediaType* pmt);

	BOOL InitVideoProc(const UINT width, const UINT height, const D3DFORMAT d3dformat);
	BOOL InitializeDXVA2VP(const UINT width, const UINT height, const D3DFORMAT d3dformat);
	BOOL CreateDXVA2VPDevice(const GUID devguid, const DXVA2_VideoDesc& videodesc);

	HRESULT CopySample(IMediaSample* pSample);
	HRESULT Render();
	HRESULT ProcessDXVA2(IDirect3DSurface9* pRenderTarget);

public:
	// CBaseRenderer
	HRESULT CheckMediaType(const CMediaType *pmt) override;
	HRESULT SetMediaType(const CMediaType *pmt) override;
	HRESULT DoRenderSample(IMediaSample* pSample) override;

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IMediaFilter
	STDMETHODIMP Run(REFERENCE_TIME rtStart) override;
	STDMETHODIMP Stop() override;

	// IMFGetService
	STDMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID *ppvObject);

	// IDispatch
	STDMETHODIMP GetTypeInfoCount(UINT* pctinfo) { return E_NOTIMPL; }
	STDMETHODIMP GetTypeInfo(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID riid, OLECHAR** rgszNames, UINT cNames, LCID lcid, DISPID* rgdispid) { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID dispidMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr) { return E_NOTIMPL; }

	// IBasicVideo
	STDMETHODIMP get_AvgTimePerFrame(REFTIME *pAvgTimePerFrame) { return E_NOTIMPL; }
	STDMETHODIMP get_BitRate(long *pBitRate) { return E_NOTIMPL; }
	STDMETHODIMP get_BitErrorRate(long *pBitErrorRate) { return E_NOTIMPL; }
	STDMETHODIMP get_VideoWidth(long *pVideoWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_VideoHeight(long *pVideoHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceLeft(long SourceLeft) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceLeft(long *pSourceLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceWidth(long SourceWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceWidth(long *pSourceWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceTop(long SourceTop) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceTop(long *pSourceTop) { return E_NOTIMPL; }
	STDMETHODIMP put_SourceHeight(long SourceHeight) { return E_NOTIMPL; }
	STDMETHODIMP get_SourceHeight(long *pSourceHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationLeft(long DestinationLeft) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationLeft(long *pDestinationLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationWidth(long DestinationWidth) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationWidth(long *pDestinationWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationTop(long DestinationTop) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationTop(long *pDestinationTop) { return E_NOTIMPL; }
	STDMETHODIMP put_DestinationHeight(long DestinationHeight) { return E_NOTIMPL; }
	STDMETHODIMP get_DestinationHeight(long *pDestinationHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetSourcePosition(long Left, long Top, long Width, long Height) { return E_NOTIMPL; }
	STDMETHODIMP GetSourcePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetDefaultSourcePosition(void) { return E_NOTIMPL; }
	STDMETHODIMP SetDestinationPosition(long Left, long Top, long Width, long Height);
	STDMETHODIMP GetDestinationPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP SetDefaultDestinationPosition(void) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoSize(long *pWidth, long *pHeight);
	STDMETHODIMP GetVideoPaletteEntries(long StartIndex, long Entries, long *pRetrieved, long *pPalette) { return E_NOTIMPL; }
	STDMETHODIMP GetCurrentImage(long *pBufferSize, long *pDIBImage) { return E_NOTIMPL; }
	STDMETHODIMP IsUsingDefaultSource(void) { return E_NOTIMPL; }
	STDMETHODIMP IsUsingDefaultDestination(void) { return E_NOTIMPL; }

	// IBasicVideo2
	STDMETHODIMP GetPreferredAspectRatio(long *plAspectX, long *plAspectY);

	// IVideoWindow
	STDMETHODIMP put_Caption(BSTR strCaption) { return E_NOTIMPL; }
	STDMETHODIMP get_Caption(BSTR *strCaption) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowStyle(long WindowStyle) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowStyle(long *WindowStyle) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowStyleEx(long WindowStyleEx) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowStyleEx(long *WindowStyleEx) { return E_NOTIMPL; }
	STDMETHODIMP put_AutoShow(long AutoShow) { return E_NOTIMPL; }
	STDMETHODIMP get_AutoShow(long *AutoShow) { return E_NOTIMPL; }
	STDMETHODIMP put_WindowState(long WindowState) { return E_NOTIMPL; }
	STDMETHODIMP get_WindowState(long *WindowState) { return E_NOTIMPL; }
	STDMETHODIMP put_BackgroundPalette(long BackgroundPalette) { return E_NOTIMPL; }
	STDMETHODIMP get_BackgroundPalette(long *pBackgroundPalette) { return E_NOTIMPL; }
	STDMETHODIMP put_Visible(long Visible) { return E_NOTIMPL; }
	STDMETHODIMP get_Visible(long *pVisible) { return E_NOTIMPL; }
	STDMETHODIMP put_Left(long Left) { return E_NOTIMPL; }
	STDMETHODIMP get_Left(long *pLeft) { return E_NOTIMPL; }
	STDMETHODIMP put_Width(long Width) { return E_NOTIMPL; }
	STDMETHODIMP get_Width(long *pWidth) { return E_NOTIMPL; }
	STDMETHODIMP put_Top(long Top) { return E_NOTIMPL; }
	STDMETHODIMP get_Top(long *pTop) { return E_NOTIMPL; }
	STDMETHODIMP put_Height(long Height) { return E_NOTIMPL; }
	STDMETHODIMP get_Height(long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP put_Owner(OAHWND Owner);
	STDMETHODIMP get_Owner(OAHWND *Owner);
	STDMETHODIMP put_MessageDrain(OAHWND Drain) { return E_NOTIMPL; }
	STDMETHODIMP get_MessageDrain(OAHWND *Drain) { return E_NOTIMPL; }
	STDMETHODIMP get_BorderColor(long *Color) { return E_NOTIMPL; }
	STDMETHODIMP put_BorderColor(long Color) { return E_NOTIMPL; }
	STDMETHODIMP get_FullScreenMode(long *FullScreenMode) { return E_NOTIMPL; }
	STDMETHODIMP put_FullScreenMode(long FullScreenMode) { return E_NOTIMPL; }
	STDMETHODIMP SetWindowForeground(long Focus) { return E_NOTIMPL; }
	STDMETHODIMP NotifyOwnerMessage(OAHWND hwnd, long uMsg, LONG_PTR wParam, LONG_PTR lParam) { return E_NOTIMPL; }
	STDMETHODIMP SetWindowPosition(long Left, long Top, long Width, long Height);
	STDMETHODIMP GetWindowPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetMinIdealImageSize(long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetMaxIdealImageSize(long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP GetRestorePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight) { return E_NOTIMPL; }
	STDMETHODIMP HideCursor(long HideCursor) { return E_NOTIMPL; }
	STDMETHODIMP IsCursorHidden(long *CursorHidden) { return E_NOTIMPL; }

	// ISpecifyPropertyPages
	STDMETHODIMP GetPages(CAUUID* pPages);

	// IVideoRenderer
	STDMETHODIMP get_String(int id, LPWSTR* pstr, int* chars);
	STDMETHODIMP get_Binary(int id, LPVOID* pbin, int* size);
	STDMETHODIMP get_FrameInfo(VRFrameInfo* pFrameInfo);
	STDMETHODIMP get_VPDeviceGuid(GUID* pVPDevGuid);

	STDMETHODIMP_(bool) GetOptionUseD3D11();
	STDMETHODIMP SetOptionUseD3D11(bool value);
	STDMETHODIMP SaveSettings();
};
