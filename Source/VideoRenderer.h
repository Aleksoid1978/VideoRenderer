/*
 * (C) 2018-2020 see Authors.txt
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

#include <mfidl.h>
#include <dxva2api.h>
#include <thread>
#include "renbase2.h"
#include "IVideoRenderer.h"
#include "DX9VideoProcessor.h"
#include "DX11VideoProcessor.h"
#include "../Include/ISubRender.h"
#include "../Include/FilterInterfacesImpl.h"


const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] = {
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NV12},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P010},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P016},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YUY2},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV16},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P210},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_P216},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_YV24},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_AYUV},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_Y410},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_Y416},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB24},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_ARGB32},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB48},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_ARGB64},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_b48r},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_b64a},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_Y8},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_Y800},
	{&MEDIATYPE_Video, &MEDIASUBTYPE_Y16},
};

class CVideoRendererInputPin;

class __declspec(uuid("71F080AA-8661-4093-B15E-4F6903E77D0A"))
	CMpcVideoRenderer
	: public CBaseVideoRenderer2
	, public IKsPropertySet
	, public IMFGetService
	, public IBasicVideo2
	, public IVideoWindow
	, public ISpecifyPropertyPages
	, public IVideoRenderer
	, public ISubRender
	, public CExFilterConfigImpl
{
private:
	friend class CVideoRendererInputPin;
	friend class CVideoProcessor;
	friend class CDX9VideoProcessor;
	friend class CDX11VideoProcessor;

	// Options
	Settings_t m_Sets;

	FILTER_STATE m_filterState = State_Stopped;
	bool m_bFlushing = false;
	bool m_bValidBuffer = false;

	HWND m_hWnd           = nullptr;
	HWND m_hWndWindow     = nullptr;
	HWND m_hWndParent     = nullptr;
	HWND m_hWndDrain      = nullptr;
	HWND m_hWndParentMain = nullptr;

	HMONITOR m_hMon = nullptr;
	bool m_bPrimaryDisplay = false;
	DisplayConfig_t m_DisplayConfig = {};

	int m_Stepping = 0;
	REFERENCE_TIME m_rtStartTime = 0;

	// VideoProcessor
	CVideoProcessor* m_VideoProcessor = nullptr;

	CMediaType m_inputMT;

	ISubRenderCallback* m_pSubCallBack = nullptr;

	CRect m_windowRect, m_videoRect;

	bool m_bForceRedrawing = true;

	bool m_bSubInvAlpha = false;
	bool m_bCheckSubInvAlpha = false;

	HRESULT Init(const bool bCreateWindow);

public:
	CMpcVideoRenderer(LPUNKNOWN pUnk, HRESULT* phr);
	~CMpcVideoRenderer();

	void NewSegment(REFERENCE_TIME startTime);
	long CalcImageSize(CMediaType& mt, bool redefine_mt);

	// CBaseRenderer
	HRESULT CheckMediaType(const CMediaType *pmt) override;
	HRESULT SetMediaType(const CMediaType *pmt) override;
	HRESULT DoRenderSample(IMediaSample* pSample) override;
	HRESULT Receive(IMediaSample* pMediaSample) override;

	HRESULT BeginFlush() override;
	HRESULT EndFlush() override;

	void UpdateDiplayInfo();
	void OnDisplayModeChange(const bool bReset = false);
	void OnWindowMove();

	DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

	// IMediaFilter
	STDMETHODIMP Run(REFERENCE_TIME rtStart) override;
	STDMETHODIMP Pause() override;
	STDMETHODIMP Stop() override;

	// IKsPropertySet
	STDMETHODIMP Set(REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength, LPVOID pPropertyData, ULONG DataLength);
	STDMETHODIMP Get(REFGUID PropSet, ULONG Id, LPVOID pInstanceData, ULONG InstanceLength, LPVOID pPropertyData, ULONG DataLength, ULONG* pBytesReturned);
	STDMETHODIMP QuerySupported(REFGUID PropSet, ULONG Id, ULONG* pTypeSupport);

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
	STDMETHODIMP GetSourcePosition(long *pLeft, long *pTop, long *pWidth, long *pHeight);
	STDMETHODIMP SetDefaultSourcePosition(void) { return E_NOTIMPL; }
	STDMETHODIMP SetDestinationPosition(long Left, long Top, long Width, long Height);
	STDMETHODIMP GetDestinationPosition(long *pLeft, long *pTop, long *pWidth, long *pHeight);
	STDMETHODIMP SetDefaultDestinationPosition(void) { return E_NOTIMPL; }
	STDMETHODIMP GetVideoSize(long *pWidth, long *pHeight);
	STDMETHODIMP GetVideoPaletteEntries(long StartIndex, long Entries, long *pRetrieved, long *pPalette) { return E_NOTIMPL; }
	STDMETHODIMP GetCurrentImage(long *pBufferSize, long *pDIBImage);
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
	STDMETHODIMP put_MessageDrain(OAHWND Drain);
	STDMETHODIMP get_MessageDrain(OAHWND* Drain);
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
	STDMETHODIMP GetVideoProcessorInfo(std::wstring& str);
	STDMETHODIMP_(bool) GetActive();

	STDMETHODIMP_(void) GetSettings(Settings_t& setings);
	STDMETHODIMP_(void) SetSettings(const Settings_t setings);

	STDMETHODIMP SaveSettings();

	// ISubRender
	STDMETHODIMP SetCallback(ISubRenderCallback* cb);

	// IExFilterConfig
	STDMETHODIMP GetBool(LPCSTR field, bool* value) override;
	STDMETHODIMP GetInt(LPCSTR field,  int*  value) override;
	STDMETHODIMP GetInt64(LPCSTR field, __int64* value) override;
	STDMETHODIMP GetBin(LPCSTR field, LPVOID* value, unsigned* size) override;

	STDMETHODIMP SetBool(LPCSTR field, bool value) override;
	STDMETHODIMP SetInt(LPCSTR field, int value) override;
	STDMETHODIMP SetBin(LPCSTR field, LPVOID value, int size) override;

	LRESULT OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void SwitchFullScreen();

	bool m_bIsFullscreen = false;

private:
	HRESULT Redraw();
	void DoAfterChangingDevice();
};
