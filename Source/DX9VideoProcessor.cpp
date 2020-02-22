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

#include "stdafx.h"
#include <d3d9.h>
#include <mfapi.h> // for MR_BUFFER_SERVICE
#include <mfidl.h>
#include <Mferror.h>
#include <dwmapi.h>
#include "Time.h"
#include "resource.h"
#include "VideoRenderer.h"
#include "../Include/Version.h"
#include "DX9VideoProcessor.h"
#include "Shaders.h"

static const ScalingShaderResId s_Upscaling9ResIDs[UPSCALE_COUNT] = {
	{0,                             0,                             L"Nearest-neighbor"  },
	{IDF_SHADER_INTERP_MITCHELL4_X, IDF_SHADER_INTERP_MITCHELL4_Y, L"Mitchell-Netravali"},
	{IDF_SHADER_INTERP_CATMULL4_X,  IDF_SHADER_INTERP_CATMULL4_Y , L"Catmull-Rom"       },
	{IDF_SHADER_INTERP_LANCZOS2_X,  IDF_SHADER_INTERP_LANCZOS2_Y , L"Lanczos2"          },
	{IDF_SHADER_INTERP_LANCZOS3_X,  IDF_SHADER_INTERP_LANCZOS3_Y , L"Lanczos3"          },
};

static const ScalingShaderResId s_Downscaling9ResIDs[DOWNSCALE_COUNT] = {
	{IDF_SHADER_CONVOL_BOX_X,       IDF_SHADER_CONVOL_BOX_Y,       L"Box"          },
	{IDF_SHADER_CONVOL_BILINEAR_X,  IDF_SHADER_CONVOL_BILINEAR_Y,  L"Bilinear"     },
	{IDF_SHADER_CONVOL_HAMMING_X,   IDF_SHADER_CONVOL_HAMMING_Y,   L"Hamming"      },
	{IDF_SHADER_CONVOL_BICUBIC05_X, IDF_SHADER_CONVOL_BICUBIC05_Y, L"Bicubic"      },
	{IDF_SHADER_CONVOL_BICUBIC15_X, IDF_SHADER_CONVOL_BICUBIC15_Y, L"Bicubic sharp"},
	{IDF_SHADER_CONVOL_LANCZOS_X,   IDF_SHADER_CONVOL_LANCZOS_Y,   L"Lanczos"      }
};

const UINT dither_size = 32;

#pragma pack(push, 1)
template<unsigned texcoords>
struct MYD3DVERTEX {
	DirectX::XMFLOAT4 Pos;
	DirectX::XMFLOAT2 Tex[texcoords];
};
#pragma pack(pop)

template<unsigned texcoords>
static HRESULT TextureBlt(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<texcoords> v[4], D3DTEXTUREFILTERTYPE filter)
{
	ASSERT(pD3DDev);

	DWORD FVF = 0;

	switch (texcoords) {
	case 1: FVF = D3DFVF_TEX1; break;
	case 2: FVF = D3DFVF_TEX2; break;
	case 3: FVF = D3DFVF_TEX3; break;
	case 4: FVF = D3DFVF_TEX4; break;
	case 5: FVF = D3DFVF_TEX5; break;
	case 6: FVF = D3DFVF_TEX6; break;
	case 7: FVF = D3DFVF_TEX7; break;
	case 8: FVF = D3DFVF_TEX8; break;
	default:
		return E_FAIL;
	}

	HRESULT hr;

	hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);

	for (unsigned i = 0; i < texcoords; i++) {
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MAGFILTER, filter);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MINFILTER, filter);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

		hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	}

	hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | FVF);
	hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0]));

	for (unsigned i = 0; i < texcoords; i++) {
		pD3DDev->SetTexture(i, nullptr);
	}

	return S_OK;
}

HRESULT AlphaBlt(IDirect3DDevice9* pD3DDev, RECT* pSrc, RECT* pDst, IDirect3DTexture9* pTexture)
{
	ASSERT(pD3DDev);
	ASSERT(pSrc);
	ASSERT(pDst);

	CRect src(*pSrc), dst(*pDst);

	HRESULT hr;

	D3DSURFACE_DESC desc;
	if (FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	const float dx = 1.0f / desc.Width;
	const float dy = 1.0f / desc.Height;

	MYD3DVERTEX<1> Vertices[] = {
		{ {(float)dst.left  - 0.5f, (float)dst.top    - 0.5f, 0.5f, 2.0f}, {{(float)src.left  * dx, (float)src.top    * dy}} },
		{ {(float)dst.right - 0.5f, (float)dst.top    - 0.5f, 0.5f, 2.0f}, {{(float)src.right * dx, (float)src.top    * dy}} },
		{ {(float)dst.left  - 0.5f, (float)dst.bottom - 0.5f, 0.5f, 2.0f}, {{(float)src.left  * dx, (float)src.bottom * dy}} },
		{ {(float)dst.right - 0.5f, (float)dst.bottom - 0.5f, 0.5f, 2.0f}, {{(float)src.right * dx, (float)src.bottom * dy}} },
	};

	hr = pD3DDev->SetTexture(0, pTexture);

	// GetRenderState fails for devices created with D3DCREATE_PUREDEVICE
	// so we need to provide default values in case GetRenderState fails
	DWORD abe, sb, db;
	if (FAILED(pD3DDev->GetRenderState(D3DRS_ALPHABLENDENABLE, &abe)))
		abe = FALSE;
	if (FAILED(pD3DDev->GetRenderState(D3DRS_SRCBLEND, &sb)))
		sb = D3DBLEND_ONE;
	if (FAILED(pD3DDev->GetRenderState(D3DRS_DESTBLEND, &db)))
		db = D3DBLEND_ZERO;

	hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
	hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	hr = pD3DDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); // pre-multiplied src and ...
	hr = pD3DDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA); // ... inverse alpha channel for dst

	hr = pD3DDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	hr = pD3DDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	hr = pD3DDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

	hr = pD3DDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	hr = pD3DDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	hr = pD3DDev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	hr = pD3DDev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	hr = pD3DDev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, Vertices, sizeof(Vertices[0]));

	pD3DDev->SetTexture(0, nullptr);

	pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, abe);
	pD3DDev->SetRenderState(D3DRS_SRCBLEND, sb);
	pD3DDev->SetRenderState(D3DRS_DESTBLEND, db);

	return S_OK;
}

// CDX9VideoProcessor

CDX9VideoProcessor::CDX9VideoProcessor(CMpcVideoRenderer* pFilter)
	: m_pFilter(pFilter)
	, m_Font3D(L"Consolas", 14)
{
	HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_pD3DEx);
	if (!m_pD3DEx) {
		return;
	}

	DXVA2CreateDirect3DDeviceManager9(&m_nResetTocken, &m_pD3DDeviceManager);
	if (!m_pD3DDeviceManager) {
		m_pD3DEx.Release();
	}

	// set default ProcAmp ranges and values
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);
	SetDefaultDXVA2ProcAmpValues(m_DXVA2ProcAmpValues);
}

CDX9VideoProcessor::~CDX9VideoProcessor()
{
	ReleaseDevice();

	m_pD3DDeviceManager.Release();
	m_nResetTocken = 0;

	m_pD3DEx.Release();
}

HRESULT CDX9VideoProcessor::Init(const HWND hwnd, bool* pChangeDevice)
{
	DLog(L"CDX9VideoProcessor::Init()");

	CheckPointer(m_pD3DEx, E_FAIL);

	m_hWnd = hwnd;
	const UINT currentAdapter = GetAdapter(m_hWnd, m_pD3DEx);
	bool bTryToReset = (currentAdapter == m_nCurrentAdapter) && m_pD3DDevEx;
	if (!bTryToReset) {
		ReleaseDevice();
		m_nCurrentAdapter = currentAdapter;
	}

	D3DADAPTER_IDENTIFIER9 AdapID9 = {};
	if (S_OK == m_pD3DEx->GetAdapterIdentifier(m_nCurrentAdapter, 0, &AdapID9)) {
		m_VendorId = AdapID9.VendorId;
		m_strAdapterDescription.Format(L"%S (%04X:%04X)", AdapID9.Description, AdapID9.VendorId, AdapID9.DeviceId);
		DLog(L"Graphics adapter: %s", m_strAdapterDescription);
	}

	ZeroMemory(&m_DisplayMode, sizeof(D3DDISPLAYMODEEX));
	m_DisplayMode.Size = sizeof(D3DDISPLAYMODEEX);
	HRESULT hr = m_pD3DEx->GetAdapterDisplayModeEx(m_nCurrentAdapter, &m_DisplayMode, nullptr);
	DLog(L"Display Mode: %ux%u, %u%c", m_DisplayMode.Width, m_DisplayMode.Height, m_DisplayMode.RefreshRate, (m_DisplayMode.ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED) ? 'i' : 'p');

#ifdef _DEBUG
	D3DCAPS9 DevCaps = {};
	if (S_OK == m_pD3DEx->GetDeviceCaps(m_nCurrentAdapter, D3DDEVTYPE_HAL, &DevCaps)) {
		CStringW dbgstr = L"DeviceCaps:";
		dbgstr.AppendFormat(L"\n  MaxTextureWidth                 : %u", DevCaps.MaxTextureWidth);
		dbgstr.AppendFormat(L"\n  MaxTextureHeight                : %u", DevCaps.MaxTextureHeight);
		dbgstr.AppendFormat(L"\n  PresentationInterval IMMEDIATE  : %s", (DevCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE) ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\n  PresentationInterval ONE        : %s", (DevCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE) ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\n  Caps READ_SCANLINE              : %s", (DevCaps.Caps & D3DCAPS_READ_SCANLINE) ? L"supported" : L"NOT supported");
		dbgstr.AppendFormat(L"\n  PixelShaderVersion              : %u.%u", D3DSHADER_VERSION_MAJOR(DevCaps.PixelShaderVersion), D3DSHADER_VERSION_MINOR(DevCaps.PixelShaderVersion));
		dbgstr.AppendFormat(L"\n  MaxPixelShader30InstructionSlots: %u", DevCaps.MaxPixelShader30InstructionSlots);
		DLog(dbgstr);
	}
#endif

	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));
	m_d3dpp.BackBufferWidth  = std::max(1, m_windowRect.Width());
	m_d3dpp.BackBufferHeight = std::max(1, m_windowRect.Height());
	m_d3dpp.Windowed = TRUE;
	m_d3dpp.hDeviceWindow = m_hWnd;
	m_d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
	m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
	if (m_iSwapEffect == SWAPEFFECT_Discard) {
		m_d3dpp.BackBufferWidth  = ALIGN(m_d3dpp.BackBufferWidth, 128);
		m_d3dpp.BackBufferHeight = ALIGN(m_d3dpp.BackBufferHeight, 128);
		m_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		m_d3dpp.BackBufferCount = 1;
	} else {
		m_d3dpp.SwapEffect = IsWindows7OrGreater() ? D3DSWAPEFFECT_FLIPEX : D3DSWAPEFFECT_FLIP;
		m_d3dpp.BackBufferCount = 3;
	}

	if (bTryToReset) {
		bTryToReset = SUCCEEDED(hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr));
		DLog(L"    => ResetEx() : %s", HR2Str(hr));
	}

	if (!bTryToReset) {
		ReleaseDevice();
		hr = m_pD3DEx->CreateDeviceEx(
			m_nCurrentAdapter, D3DDEVTYPE_HAL, m_hWnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_ENABLE_PRESENTSTATS,
			&m_d3dpp, nullptr, &m_pD3DDevEx);
		DLog(L"    => CreateDeviceEx() : %s", HR2Str(hr));
	}

	if (FAILED(hr)) {
		return hr;
	}
	if (!m_pD3DDevEx) {
		return E_FAIL;
	}

	while (hr == D3DERR_DEVICELOST) {
		DLog(L"    => D3DERR_DEVICELOST. Trying to Reset.");
		hr = m_pD3DDevEx->CheckDeviceState(m_hWnd);
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		DLog(L"    => D3DERR_DEVICENOTRESET");
		hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr);
	}

	if (S_OK == hr && !bTryToReset) {
		hr = m_pD3DDeviceManager->ResetDevice(m_pD3DDevEx, m_nResetTocken);
	}

	if (pChangeDevice) {
		*pChangeDevice = !bTryToReset;
	}

	hr = m_DXVA2VP.InitVideoService(m_pD3DDevEx, m_VendorId);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::Init() : m_DXVA2VP.InitVideoService() failed with error %s", HR2Str(hr));
		return FALSE;
	}

	if (m_pFilter->m_inputMT.IsValid()) {
		if (!InitMediaType(&m_pFilter->m_inputMT)) { // restore DXVA2VideoProcessor after m_DXVA2VP.InitVideoService()
			ReleaseDevice();
			return E_FAIL;
		}
	}

	if (m_pFilter->m_pSubCallBack) {
		m_pFilter->m_pSubCallBack->SetDevice(m_pD3DDevEx);
	}

	HRESULT hr2 = m_TexStats.Create(m_pD3DDevEx, D3DFMT_A8R8G8B8, STATS_W, STATS_H, D3DUSAGE_RENDERTARGET);
	if (S_OK == hr2) {
		hr2 = m_Font3D.InitDeviceObjects(m_pD3DDevEx);
	}
	if (S_OK == hr2) {
		hr2 = m_Rect3D.InitDeviceObjects(m_pD3DDevEx);
	}

	HRESULT hr3 = m_TexDither.Create(m_pD3DDevEx, D3DFMT_A16B16G16R16F, dither_size, dither_size, D3DUSAGE_DYNAMIC);
	if (S_OK == hr3) {
		LPVOID data;
		DWORD size;
		hr3 = GetDataFromResource(data, size, IDF_DITHER_32X32_FLOAT16);
		if (S_OK == hr3) {
			D3DLOCKED_RECT lockedRect;
			hr3 = m_TexDither.pTexture->LockRect(0, &lockedRect, nullptr, D3DLOCK_DISCARD);
			if (S_OK == hr3) {
				uint16_t* src = (uint16_t*)data;
				BYTE* dst = (BYTE*)lockedRect.pBits;
				for (UINT y = 0; y < dither_size; y++) {
					uint16_t* pUInt16 = reinterpret_cast<uint16_t*>(dst);
					for (UINT x = 0; x < dither_size; x++) {
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
						*pUInt16++ = src[x];
					}
					src += dither_size;
					dst += lockedRect.Pitch;
				}
				hr3 = m_TexDither.pTexture->UnlockRect(0);
			}
		}
		if (S_OK == hr3) {
			m_pPSFinalPass.Release();
			hr3 = CreatePShaderFromResource(&m_pPSFinalPass, IDF_SHADER_FINAL_PASS);
		}

		if (FAILED(hr3)) {
			m_TexDither.Release();
		}
	}

	return hr;
}

bool CDX9VideoProcessor::Initialized()
{
	return (m_pD3DDevEx.p != nullptr);
}

void CDX9VideoProcessor::ReleaseVP()
{
	DLog(L"CDX9VideoProcessor::ReleaseVP()");

	m_pFilter->ResetStreamingTimes2();
	m_RenderStats.Reset();

	m_DXVA2VP.ReleaseVideoProcessor();

	m_TexSrcVideo.Release();
	m_TexDxvaOutput.Release();
	m_TexConvertOutput.Release();
	m_TexResize.Release();
	m_TexsPostScale.Release();

	m_srcParams      = {};
	m_srcDXVA2Format = D3DFMT_UNKNOWN;
	m_pConvertFn     = nullptr;
	m_srcWidth       = 0;
	m_srcHeight      = 0;
}

void CDX9VideoProcessor::ReleaseDevice()
{
	DLog(L"CDX9VideoProcessor::ReleaseDevice()");

	ReleaseVP();

	m_TexDither.Release();
	m_TexStats.Release();

	m_DXVA2VP.ReleaseVideoService();

	ClearPostScaleShaders();
	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();

	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();
	m_strShaderX = nullptr;
	m_strShaderY = nullptr;
	m_pPSFinalPass.Release();

	m_Font3D.InvalidateDeviceObjects();
	m_Rect3D.InvalidateDeviceObjects();

	m_pD3DDevEx.Release();
}

HRESULT CDX9VideoProcessor::InitializeDXVA2VP(const FmtConvParams_t& params, const UINT width, const UINT height)
{
	const auto& dxva2format = params.DXVA2Format;

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() started with input surface: %s, %u x %u", D3DFormatToString(dxva2format), width, height);

	if (m_VendorId != PCIV_INTEL && (dxva2format == D3DFMT_X8R8G8B8 || dxva2format == D3DFMT_A8R8G8B8)) {
		m_DXVA2VP.ReleaseVideoProcessor();
		DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() : RGB input is not supported");
		return E_FAIL;
	}

	m_DXVA2OutputFmt = m_InternalTexFmt;
	HRESULT hr = m_DXVA2VP.InitVideoProcessor(dxva2format, width, height, m_srcExFmt, m_bInterlaced, m_DXVA2OutputFmt);
	if (FAILED(hr)) {
		return hr;
	}

	m_srcWidth       = width;
	m_srcHeight      = height;
	m_srcParams      = params;
	m_srcDXVA2Format = dxva2format;
	m_pConvertFn     = GetCopyFunction(params);

	m_DXVA2VP.GetProcAmpRanges(m_DXVA2ProcAmpRanges);
	m_DXVA2VP.SetProcAmpValues(m_DXVA2ProcAmpValues);

	DLog(L"CDX9VideoProcessor::InitializeDXVA2VP() completed successfully");

	return S_OK;
}

HRESULT CDX9VideoProcessor::InitializeTexVP(const FmtConvParams_t& params, const UINT width, const UINT height)
{
	const auto& d3dformat = params.D3DFormat;

	DLog(L"CDX9VideoProcessor::InitializeTexVP() started with input surface: %s, %u x %u", D3DFormatToString(d3dformat), width, height);

	UINT texW = (params.cformat == CF_YUY2) ? width / 2 : width;

	HRESULT hr = m_TexSrcVideo.CreateEx(m_pD3DDevEx, d3dformat, params.pDX9Planes, texW, height, D3DUSAGE_DYNAMIC);
	if (FAILED(hr)) {
		DLog(L"CDX9VideoProcessor::InitializeTexVP() : failed m_TexSrcVideo.Create()");
		return hr;
	}

	// fill the surface in black, to avoid the "green screen"
	m_pD3DDevEx->ColorFill(m_TexSrcVideo.pSurface, nullptr, D3DCOLOR_XYUV(0, 128, 128));

	m_srcWidth       = width;
	m_srcHeight      = height;
	m_srcParams      = params;
	m_srcDXVA2Format = d3dformat;
	m_pConvertFn     = GetCopyFunction(params);

	// set default ProcAmp ranges
	SetDefaultDXVA2ProcAmpRanges(m_DXVA2ProcAmpRanges);

	HRESULT hr2 = UpdateChromaScalingShader();
	if (FAILED(hr2)) {
		ASSERT(0);
		UINT resid = 0;
		if (params.cformat == CF_YUY2) {
			resid = IDF_SHADER_CONVERT_YUY2;
		}
		else if (params.pDX9Planes) {
			if (params.pDX9Planes->FmtPlane3) {
				resid = IDF_SHADER_CONVERT_PLANAR;
			} else {
				resid = IDF_SHADER_CONVERT_BIPLANAR;
			}
		}
		else {
			resid = IDF_SHADER_CONVERT_COLOR;
		}
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSConvertColor, resid));
	}

	DLog(L"CDX9VideoProcessor::InitializeTexVP() completed successfully");

	return S_OK;
}

void CDX9VideoProcessor::UpdatFrameProperties()
{
	m_srcPitch = m_srcWidth * m_srcParams.Packsize;
	m_srcLines = m_srcHeight * m_srcParams.PitchCoeff / 2;
}

HRESULT CDX9VideoProcessor::CreatePShaderFromResource(IDirect3DPixelShader9** ppPixelShader, UINT resid)
{
	if (!m_pD3DDevEx || !ppPixelShader) {
		return E_POINTER;
	}

	static const HMODULE hModule = (HMODULE)&__ImageBase;

	HRSRC hrsrc = FindResourceW(hModule, MAKEINTRESOURCEW(resid), L"FILE");
	if (!hrsrc) {
		return E_INVALIDARG;
	}
	HGLOBAL hGlobal = LoadResource(hModule, hrsrc);
	if (!hGlobal) {
		return E_FAIL;
	}
	DWORD size = SizeofResource(hModule, hrsrc);
	if (size < 4) {
		return E_FAIL;
	}

	return m_pD3DDevEx->CreatePixelShader((const DWORD*)LockResource(hGlobal), ppPixelShader);
}

void CDX9VideoProcessor::SetShaderConvertColorParams()
{
	mp_csp_params csp_params;
	set_colorspace(m_srcExFmt, csp_params.color);
	csp_params.brightness = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Brightness) / 255;
	csp_params.contrast   = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Contrast);
	csp_params.hue        = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Hue) / 180 * acos(-1);
	csp_params.saturation = DXVA2FixedToFloat(m_DXVA2ProcAmpValues.Saturation);
	csp_params.gray       = m_srcParams.CSType == CS_GRAY;

	m_PSConvColorData.bEnable = m_srcParams.CSType == CS_YUV || fabs(csp_params.brightness) > 1e-4f || fabs(csp_params.contrast - 1.0f) > 1e-4f;

	mp_cmat cmatrix;
	mp_get_csp_matrix(&csp_params, &cmatrix);

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m_PSConvColorData.fConstants[i][j] = cmatrix.m[i][j];
		}
		m_PSConvColorData.fConstants[i][3] = 0.0f;
	}
	for (int j = 0; j < 3; j++) {
		m_PSConvColorData.fConstants[3][j] = cmatrix.c[j];
	}

	if (m_srcParams.cformat == CF_Y410 || m_srcParams.cformat == CF_Y416) {
		for (int i = 0; i < 3; i++) {
			std::swap(m_PSConvColorData.fConstants[i][0], m_PSConvColorData.fConstants[i][1]);
		}
	}
	else if (m_srcParams.cformat == CF_NV12) {
		for (int i = 0; i < 3; i++) {
			std::swap(m_PSConvColorData.fConstants[i][2], m_PSConvColorData.fConstants[i][3]);
		}
	}
}

void CDX9VideoProcessor::UpdateRenderRects()
{
	m_srcRenderRect = m_srcRect;
	m_dstRenderRect = m_videoRect;
	ClipToSurface(m_windowRect.Width(), m_windowRect.Height(), m_srcRenderRect, m_dstRenderRect);

	{
		const int w2 = m_dstRenderRect.Width();
		const int h2 = m_dstRenderRect.Height();
		const int k = m_bInterpolateAt50pct ? 2 : 1;
		int w1, h1;
		if (m_iRotation == 90 || m_iRotation == 270) {
			w1 = m_srcRenderRect.Height();
			h1 = m_srcRenderRect.Width();
		} else {
			w1 = m_srcRenderRect.Width();
			h1 = m_srcRenderRect.Height();
		}
		m_strShaderX = (w1 == w2) ? nullptr
			: (w1 > k * w2)
			? s_Downscaling9ResIDs[m_iDownscaling].description
			: s_Upscaling9ResIDs[m_iUpscaling].description;
		m_strShaderY = (h1 == h2) ? nullptr
			: (h1 > k * h2)
			? s_Downscaling9ResIDs[m_iDownscaling].description
			: s_Upscaling9ResIDs[m_iUpscaling].description;
	}
}

BOOL CDX9VideoProcessor::VerifyMediaType(const CMediaType* pmt)
{
	const auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	if (FmtConvParams.DXVA2Format == D3DFMT_UNKNOWN && FmtConvParams.D3DFormat == D3DFMT_UNKNOWN) {
		return FALSE;
	}

	const BITMAPINFOHEADER* pBIH = GetBIHfromVIHs(pmt);
	if (!pBIH) {
		return FALSE;
	}

	if (pBIH->biWidth <= 0 || !pBIH->biHeight || (!pBIH->biSizeImage && pBIH->biCompression != BI_RGB)) {
		return FALSE;
	}

	if (FmtConvParams.Subsampling == 420 && ((pBIH->biWidth & 1) || (pBIH->biHeight & 1))) {
		return FALSE;
	}
	if (FmtConvParams.Subsampling == 422 && (pBIH->biWidth & 1)) {
		return FALSE;
	}

	return TRUE;
}

BOOL CDX9VideoProcessor::GetAlignmentSize(const CMediaType& mt, SIZE& Size)
{
	if (InitMediaType(&mt)) {
		const auto& FmtParams = GetFmtConvParams(mt.subtype);

		if (FmtParams.cformat == CF_RGB24) {
			Size.cx = ALIGN(Size.cx, 4);
		}
		else if (FmtParams.cformat == CF_RGB48 || FmtParams.cformat == CF_B48R) {
			Size.cx = ALIGN(Size.cx, 2);
		}
		else if (FmtParams.cformat == CF_ARGB64 || FmtParams.cformat == CF_B64A) {
			// nothing
		}
		else {
			CComPtr<IDirect3DSurface9> pSurface;
			if (m_DXVA2VP.IsReady()) {
				pSurface = m_DXVA2VP.GetInputSurface();
			} else {
				pSurface = m_TexSrcVideo.pSurface;
			}

			if (!pSurface) {
				return FALSE;
			}

			INT Pitch = 0;
			D3DLOCKED_RECT lr;
			if (SUCCEEDED(pSurface->LockRect(&lr, nullptr, D3DLOCK_NOSYSLOCK))) { // don't use D3DLOCK_DISCARD here on AMD card
				Pitch = lr.Pitch;
				pSurface->UnlockRect();
			}

			if (!Pitch) {
				return FALSE;
			}

			Size.cx = Pitch / FmtParams.Packsize;
		}

		if (FmtParams.cformat == CF_RGB24 || FmtParams.cformat == CF_XRGB32 || FmtParams.cformat == CF_ARGB32) {
			Size.cy = -abs(Size.cy); // only for biCompression == BI_RGB
		} else {
			Size.cy = abs(Size.cy); // need additional checks
		}

		return TRUE;
	}

	return FALSE;
}

BOOL CDX9VideoProcessor::InitMediaType(const CMediaType* pmt)
{
	DLog(L"CDX9VideoProcessor::InitMediaType()");

	if (!VerifyMediaType(pmt)) {
		return FALSE;
	}

	ReleaseVP();

	auto FmtConvParams = GetFmtConvParams(pmt->subtype);
	bool disableDXVA2 = false;
	switch (FmtConvParams.cformat) {
	case CF_NV12: disableDXVA2 = !m_VPFormats.bNV12; break;
	case CF_P010:
	case CF_P016: disableDXVA2 = !m_VPFormats.bP01x;  break;
	case CF_YUY2: disableDXVA2 = !m_VPFormats.bYUY2;  break;
	default:      disableDXVA2 = !m_VPFormats.bOther; break;
	}
	if (disableDXVA2) {
		FmtConvParams.DXVA2Format = D3DFMT_UNKNOWN;
	}
	const GUID SubType = pmt->subtype;
	const BITMAPINFOHEADER* pBIH = nullptr;
	m_decExFmt.value = 0;

	if (pmt->formattype == FORMAT_VideoInfo2) {
		const VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
		pBIH = &vih2->bmiHeader;
		m_srcRect = vih2->rcSource;
		m_trgRect = vih2->rcTarget;
		m_srcAspectRatioX = vih2->dwPictAspectRatioX;
		m_srcAspectRatioY = vih2->dwPictAspectRatioY;
		if (FmtConvParams.CSType == CS_YUV && (vih2->dwControlFlags & (AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT))) {
			m_decExFmt.value = vih2->dwControlFlags;
			m_decExFmt.SampleFormat = AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT; // ignore other flags
		}
		m_bInterlaced = (vih2->dwInterlaceFlags & AMINTERLACE_IsInterlaced);
	}
	else if (pmt->formattype == FORMAT_VideoInfo) {
		const VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->pbFormat;
		pBIH = &vih->bmiHeader;
		m_srcRect = vih->rcSource;
		m_trgRect = vih->rcTarget;
		m_srcAspectRatioX = 0;
		m_srcAspectRatioY = 0;
		m_bInterlaced = 0;
	}
	else {
		return FALSE;
	}

	UINT biWidth  = pBIH->biWidth;
	UINT biHeight = labs(pBIH->biHeight);
	if (pmt->FormatLength() == 112 + sizeof(VR_Extradata)) {
		const VR_Extradata* vrextra = reinterpret_cast<VR_Extradata*>(pmt->pbFormat + 112);
		if (vrextra->QueryWidth == pBIH->biWidth && vrextra->QueryHeight == pBIH->biHeight && vrextra->Compression == pBIH->biCompression) {
			biWidth  = vrextra->FrameWidth;
			biHeight = abs(vrextra->FrameHeight);
		}
	}

	UINT biSizeImage = pBIH->biSizeImage;
	if (pBIH->biSizeImage == 0 && pBIH->biCompression == BI_RGB) { // biSizeImage may be zero for BI_RGB bitmaps
		biSizeImage = biWidth * biHeight * pBIH->biBitCount / 8;
	}

	m_srcExFmt = SpecifyExtendedFormat(m_decExFmt, FmtConvParams, m_srcRectWidth, m_srcRectHeight);

	if (m_srcRect.IsRectNull()) {
		m_srcRect.SetRect(0, 0, biWidth, biHeight);
	}
	if (m_trgRect.IsRectNull()) {
		m_trgRect.SetRect(0, 0, biWidth, biHeight);
	}

	m_srcRectWidth  = m_srcRect.Width();
	m_srcRectHeight = m_srcRect.Height();

	if (!m_srcAspectRatioX || !m_srcAspectRatioY) {
		const auto gcd = std::gcd(m_srcRectWidth, m_srcRectHeight);
		m_srcAspectRatioX = m_srcRectWidth / gcd;
		m_srcAspectRatioY = m_srcRectHeight / gcd;
	}

	m_srcLines = biHeight * FmtConvParams.PitchCoeff / 2;
	m_srcPitch = biSizeImage / m_srcLines;

	if (FmtConvParams.cformat != CF_Y8 && FmtConvParams.cformat != CF_Y800) {
		m_srcPitch &= ~1u;
	}
	if (SubType == MEDIASUBTYPE_NV12 && biSizeImage % 4) {
		DLog(L"CDX9VideoProcessor::InitMediaType() applied unsafe hack for NV12. m_srcPitch: %d -> %d", m_srcPitch, ALIGN(m_srcPitch, 4));
		m_srcPitch = ALIGN(m_srcPitch, 4);
	}
	if (pBIH->biCompression == BI_RGB && pBIH->biHeight > 0) {
		m_srcPitch = -m_srcPitch;
	}

	UpdateUpscalingShaders();
	UpdateDownscalingShaders();

	m_pPSCorrection.Release();
	m_pPSConvertColor.Release();
	m_PSConvColorData.bEnable = false;

	switch (m_iTexFormat) {
	case TEXFMT_AUTOINT:
		m_InternalTexFmt = (FmtConvParams.CDepth > 8) ? D3DFMT_A2R10G10B10 : D3DFMT_X8R8G8B8;
		break;
	case TEXFMT_8INT:    m_InternalTexFmt = D3DFMT_X8R8G8B8;      break;
	case TEXFMT_10INT:   m_InternalTexFmt = D3DFMT_A2R10G10B10;   break;
	case TEXFMT_16FLOAT: m_InternalTexFmt = D3DFMT_A16B16G16R16F; break;
	default:
		ASSERT(FALSE);
	}

	// DXVA2 Video Processor
	if (FmtConvParams.DXVA2Format != D3DFMT_UNKNOWN && S_OK == InitializeDXVA2VP(FmtConvParams, biWidth, biHeight)) {
		if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_2084) {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_SHADER_CORRECTION_ST2084));
		}
		else if (m_srcExFmt.VideoTransferFunction == VIDEOTRANSFUNC_HLG) {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_SHADER_CORRECTION_HLG));
		}
		else if (m_srcExFmt.VideoTransferMatrix == VIDEOTRANSFERMATRIX_YCgCo) {
			EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pPSCorrection, IDF_SHADER_CORRECTION_YCGCO));
		}
		DLogIf(m_pPSCorrection, L"CDX9VideoProcessor::InitMediaType() m_pPSCorrection created");

		UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
		UpdateStatsStatic();

		m_pFilter->m_inputMT = *pmt;

		return TRUE;
	}

	ReleaseVP();

	// Tex Video Processor
	if (FmtConvParams.D3DFormat != D3DFMT_UNKNOWN && S_OK == InitializeTexVP(FmtConvParams, biWidth, biHeight)) {
		SetShaderConvertColorParams();
		UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
		UpdateStatsStatic();

		m_pFilter->m_inputMT = *pmt;

		return TRUE;
	}

	return FALSE;
}

void CDX9VideoProcessor::Start()
{
	m_rtStart = 0;
}

HRESULT CDX9VideoProcessor::ProcessSample(IMediaSample* pSample)
{
	REFERENCE_TIME rtStart, rtEnd;
	pSample->GetTime(&rtStart, &rtEnd);

	m_rtStart = rtStart;

	const REFERENCE_TIME rtFrameDur = m_pFilter->m_FrameStats.GetAverageFrameDuration();
	rtEnd = rtStart + rtFrameDur;
	CRefTime rtClock(rtStart);

	HRESULT hr = CopySample(pSample);
	if (FAILED(hr)) {
		m_RenderStats.failed++;
		return hr;
	}

	// always Render(1) a frame after CopySample()
	hr = Render(1);
	m_pFilter->m_DrawStats.Add(GetPreciseTick());
	if (m_pFilter->m_filterState == State_Running) {
		m_pFilter->StreamTime(rtClock);
	}

	m_RenderStats.syncoffset = rtClock - rtStart;

	if (SecondFramePossible()) {
		if (rtEnd < rtClock) {
			m_RenderStats.dropped2++;
			return S_FALSE; // skip frame
		}

		hr = Render(2);
		m_pFilter->m_DrawStats.Add(GetPreciseTick());
		if (m_pFilter->m_filterState == State_Running) {
			m_pFilter->StreamTime(rtClock);
		}

		rtStart += rtFrameDur / 2;
		m_RenderStats.syncoffset = rtClock - rtStart;
	}

	return hr;
}

HRESULT CDX9VideoProcessor::CopySample(IMediaSample* pSample)
{
	uint64_t tick = GetPreciseTick();

	// Get frame type
	m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame; // Progressive
	if (m_bInterlaced) {
		if (CComQIPtr<IMediaSample2> pMS2 = pSample) {
			AM_SAMPLE2_PROPERTIES props;
			if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
				m_CurrentSampleFmt = DXVA2_SampleFieldInterleavedOddFirst;      // Bottom-field first
				if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE) {
					m_CurrentSampleFmt = DXVA2_SampleProgressiveFrame;          // Progressive
				}
				else if (props.dwTypeSpecificFlags & AM_VIDEO_FLAG_FIELD1FIRST) {
					m_CurrentSampleFmt = DXVA2_SampleFieldInterleavedEvenFirst; // Top-field first
				}
			}
		}
	}

	HRESULT hr = S_OK;
	m_FieldDrawn = 0;

	if (CComQIPtr<IMFGetService> pService = pSample) {
		m_iSrcFromGPU = 9;

		CComPtr<IDirect3DSurface9> pSurface;
		if (SUCCEEDED(pService->GetService(MR_BUFFER_SERVICE, IID_PPV_ARGS(&pSurface)))) {
			D3DSURFACE_DESC desc;
			hr = pSurface->GetDesc(&desc);
			if (FAILED(hr) || desc.Format != m_srcDXVA2Format) {
				return E_FAIL;
			}
			if (desc.Width != m_srcWidth || desc.Height != m_srcHeight) {
				if (m_DXVA2VP.IsReady()) {
					hr = InitializeDXVA2VP(m_srcParams, desc.Width, desc.Height);
				} else {
					hr = InitializeTexVP(m_srcParams, desc.Width, desc.Height);
				}
				if (FAILED(hr)) {
					return hr;
				}
				UpdatFrameProperties();
			}

			if (m_DXVA2VP.IsReady()) {
				const REFERENCE_TIME start_100ns = m_pFilter->m_FrameStats.GetFrames() * 170000i64;
				const REFERENCE_TIME end_100ns = start_100ns + 170000i64;

				if (m_DXVA2VP.GetNumRefSamples() > 1) {
					IDirect3DSurface9* pDXVA2VPSurface = m_DXVA2VP.GetNextInputSurface(start_100ns, end_100ns, m_CurrentSampleFmt);
					hr = m_pD3DDevEx->StretchRect(pSurface, nullptr, pDXVA2VPSurface, nullptr, D3DTEXF_NONE);
				} else {
					m_DXVA2VP.SetInputSurface(pSurface, start_100ns, end_100ns, m_CurrentSampleFmt);
				}
			}
			else if (m_TexSrcVideo.Plane2.pSurface) {
				D3DLOCKED_RECT lr_src;
				hr = pSurface->LockRect(&lr_src, nullptr, D3DLOCK_READONLY);
				if (S_OK == hr) {
					D3DLOCKED_RECT lr;
					hr = m_TexSrcVideo.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
					if (S_OK == hr) {
						CopyFrameAsIs(m_srcHeight, (BYTE*)lr.pBits, lr.Pitch, (const BYTE*)lr_src.pBits, lr_src.Pitch);
						hr = m_TexSrcVideo.pSurface->UnlockRect();
					}
					hr = m_TexSrcVideo.Plane2.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
					if (S_OK == hr) {
						CopyFrameAsIs(m_srcHeight/m_srcParams.pDX9Planes->div_chroma_h, (BYTE*)lr.pBits, lr.Pitch, (const BYTE*)lr_src.pBits + lr_src.Pitch * m_srcHeight, lr_src.Pitch);
						hr = m_TexSrcVideo.Plane2.pSurface->UnlockRect();
					}
					hr = pSurface->UnlockRect();
				}
			}
		}
	}
	else {
		m_iSrcFromGPU = 0;

		BYTE* data = nullptr;
		const long size = pSample->GetActualDataLength();
		if (size > 0 && S_OK == pSample->GetPointer(&data)) {
			if (m_srcParams.cformat == CF_NONE) {
				return E_FAIL;
			}

			D3DLOCKED_RECT lr;

			if (m_DXVA2VP.IsReady()) {
				const REFERENCE_TIME start_100ns = m_pFilter->m_FrameStats.GetFrames() * 170000i64;
				const REFERENCE_TIME end_100ns = start_100ns + 170000i64;

				IDirect3DSurface9* pDXVA2VPSurface = m_DXVA2VP.GetNextInputSurface(start_100ns, end_100ns, m_CurrentSampleFmt);

				hr = pDXVA2VPSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
				if (S_OK == hr) {
					ASSERT(m_pConvertFn);
					const BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcLines) : data;
					m_pConvertFn(m_srcLines, (BYTE*)lr.pBits, lr.Pitch, src, m_srcPitch);
					hr = pDXVA2VPSurface->UnlockRect();
				}
			} else {
				if (m_TexSrcVideo.Plane2.pSurface) {
					hr = m_TexSrcVideo.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
					if (S_OK == hr) {
						CopyFrameAsIs(m_srcHeight, (BYTE*)lr.pBits, lr.Pitch, data, m_srcPitch);
						hr = m_TexSrcVideo.pSurface->UnlockRect();

						hr = m_TexSrcVideo.Plane2.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
						if (S_OK == hr) {
							const UINT cromaH = m_srcHeight / m_srcParams.pDX9Planes->div_chroma_h;
							const UINT cromaPitch = (m_TexSrcVideo.Plane3.pSurface) ? m_srcPitch / m_srcParams.pDX9Planes->div_chroma_w : m_srcPitch;
							data += m_srcPitch * m_srcHeight;
							CopyFrameAsIs(cromaH, (BYTE*)lr.pBits, lr.Pitch, data, cromaPitch);
							hr = m_TexSrcVideo.Plane2.pSurface->UnlockRect();

							if (m_TexSrcVideo.Plane3.pSurface) {
								hr = m_TexSrcVideo.Plane3.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK);
								if (S_OK == hr) {
									data += cromaPitch * cromaH;
									CopyFrameAsIs(cromaH, (BYTE*)lr.pBits, lr.Pitch, data, cromaPitch);
									hr = m_TexSrcVideo.Plane3.pSurface->UnlockRect();
								}
							}
						}
					}
				}
				else {
					hr = m_TexSrcVideo.pSurface->LockRect(&lr, nullptr, D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK);
					if (S_OK == hr) {
						ASSERT(m_pConvertFn);
						const BYTE* src = (m_srcPitch < 0) ? data + m_srcPitch * (1 - (int)m_srcLines) : data;
						m_pConvertFn(m_srcLines, (BYTE*)lr.pBits, lr.Pitch, src, m_srcPitch);
						hr = m_TexSrcVideo.pSurface->UnlockRect();
					}
				}
			}
		}
	}

	m_RenderStats.copyticks = GetPreciseTick() - tick;

	return hr;
}

HRESULT CDX9VideoProcessor::Render(int field)
{
	uint64_t tick1 = GetPreciseTick();

	if (field) {
		m_FieldDrawn = field;
	}

	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	if (!m_videoRect.IsRectEmpty()) {
		hr = Process(pBackBuffer, m_srcRenderRect, m_dstRenderRect, m_FieldDrawn == 2);
	}

	hr = m_pD3DDevEx->EndScene();

	uint64_t tick2 = GetPreciseTick();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	if (m_pFilter->m_pSubCallBack) {
		CRect rDstVid(m_videoRect);
		const auto rtStart = m_pFilter->m_rtStartTime + m_rtStart;

		if (CComQIPtr<ISubRenderCallback4> pSubCallBack4 = m_pFilter->m_pSubCallBack) {
			pSubCallBack4->RenderEx3(rtStart, 0, 0, rDstVid, rDstVid, rSrcPri);
		} else {
			m_pFilter->m_pSubCallBack->Render(rtStart, rDstVid.left, rDstVid.top, rDstVid.right, rDstVid.bottom, rSrcPri.Width(), rSrcPri.Height());
		}
	}

 	if (m_bShowStats) {
		uint64_t tick3 = GetPreciseTick();
		hr = DrawStats(pBackBuffer);
		m_RenderStats.renderticks = tick2 - tick1;
		m_RenderStats.substicks   = tick3 - tick2;
		m_RenderStats.statsticks  = GetPreciseTick() - tick3;
 	}

#if 0
	{ // Tearing test
		static int nTearingPos = 0;

		const SIZE szWindow = m_windowRect.Size();
		RECT rcTearing;

		rcTearing.left = nTearingPos;
		rcTearing.top = 0;
		rcTearing.right = rcTearing.left + 4;
		rcTearing.bottom = szWindow.cy;
		m_pD3DDevEx->ColorFill(pBackBuffer, &rcTearing, D3DCOLOR_XRGB(255, 0, 0));

		rcTearing.left = (rcTearing.right + 15) % szWindow.cx;
		rcTearing.right = rcTearing.left + 4;
		m_pD3DDevEx->ColorFill(pBackBuffer, &rcTearing, D3DCOLOR_XRGB(255, 0, 0));

		nTearingPos = (nTearingPos + 7) % szWindow.cx;
	}
#endif

	if (m_d3dpp.SwapEffect == D3DSWAPEFFECT_DISCARD) {
		hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);
	} else {
		hr = m_pD3DDevEx->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
	}

	return hr;
}

HRESULT CDX9VideoProcessor::FillBlack()
{
	HRESULT hr = m_pD3DDevEx->BeginScene();

	CComPtr<IDirect3DSurface9> pBackBuffer;
	hr = m_pD3DDevEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);

	hr = m_pD3DDevEx->SetRenderTarget(0, pBackBuffer);
	m_pD3DDevEx->ColorFill(pBackBuffer, nullptr, 0);

	hr = m_pD3DDevEx->EndScene();

	const CRect rSrcPri(CPoint(0, 0), m_windowRect.Size());
	const CRect rDstPri(m_windowRect);

	if (m_d3dpp.SwapEffect == D3DSWAPEFFECT_DISCARD) {
		hr = m_pD3DDevEx->PresentEx(rSrcPri, rDstPri, nullptr, nullptr, 0);
	} else {
		hr = m_pD3DDevEx->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
	}

	return hr;
}

void CDX9VideoProcessor::SetVideoRect(const CRect& videoRect)
{
	m_videoRect = videoRect;
	UpdateRenderRects();
	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
}

HRESULT CDX9VideoProcessor::SetWindowRect(const CRect& windowRect)
{
	m_windowRect = windowRect;
	UpdateRenderRects();

	if (m_pD3DDevEx && !m_windowRect.IsRectEmpty()) {
		UINT backBufW = m_windowRect.Width();
		UINT backBufH = m_windowRect.Height();
		if (m_d3dpp.SwapEffect == D3DSWAPEFFECT_DISCARD) {
			backBufW = ALIGN(backBufW, 128);
			backBufH = ALIGN(backBufH, 128);
		}
		if (backBufW != m_d3dpp.BackBufferWidth || backBufH != m_d3dpp.BackBufferHeight) {
			m_d3dpp.BackBufferWidth  = backBufW;
			m_d3dpp.BackBufferHeight = backBufH;
			HRESULT hr = m_pD3DDevEx->ResetEx(&m_d3dpp, nullptr);
			DLogIf(FAILED(hr), L"CDX9VideoProcessor::SetWindowRect() : ResetEx() failed with error %s", HR2Str(hr));
		}
	}

	return S_OK;
}

HRESULT CDX9VideoProcessor::GetVideoSize(long *pWidth, long *pHeight)
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

HRESULT CDX9VideoProcessor::GetAspectRatio(long *plAspectX, long *plAspectY)
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

HRESULT CDX9VideoProcessor::GetCurentImage(long *pDIBImage)
{
	CRect srcRect(m_srcRect);
	int w, h;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w = srcRect.Height();
		h = srcRect.Width();
	} else {
		w = srcRect.Width();
		h = srcRect.Height();
	}
	CRect dstRect(0, 0, w, h);

	BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)pDIBImage;
	memset(pBIH, 0, sizeof(BITMAPINFOHEADER));
	pBIH->biSize      = sizeof(BITMAPINFOHEADER);
	pBIH->biWidth     = w;
	pBIH->biHeight    = h;
	pBIH->biPlanes    = 1;
	pBIH->biBitCount  = 32;
	pBIH->biSizeImage = DIBSIZE(*pBIH);

	UINT dst_pitch = pBIH->biSizeImage / h;

	HRESULT hr = S_OK;
	CComPtr<IDirect3DSurface9> pRGB32Surface;
	hr = m_pD3DDevEx->CreateRenderTarget(w, h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &pRGB32Surface, nullptr);
	if (FAILED(hr)) {
		return hr;
	}

	if (m_DXVA2VP.IsReady() && (m_pPSCorrection || m_bVPScaling)) {
		UpdateTexures(w, h);
	}

	hr = Process(pRGB32Surface, srcRect, dstRect, 0);

	if (m_DXVA2VP.IsReady() && (m_pPSCorrection || m_bVPScaling)) {
		UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
	}

	if (FAILED(hr)) {
		return hr;
	}

	D3DLOCKED_RECT lr;
	hr = pRGB32Surface->LockRect(&lr, nullptr, D3DLOCK_READONLY);
	if (S_OK == hr) {
		CopyFrameAsIs(h, (BYTE*)(pBIH + 1), dst_pitch, (BYTE*)lr.pBits + lr.Pitch * (h - 1), -lr.Pitch);
		hr = pRGB32Surface->UnlockRect();
	}

	return hr;
}

HRESULT CDX9VideoProcessor::GetVPInfo(CStringW& str)
{
	str = L"DirectX 9";
	str.AppendFormat(L"\nGraphics adapter: %s", m_strAdapterDescription);

	str.Append(L"\nVideoProcessor  : ");
	if (m_DXVA2VP.IsReady()) {
		GUID DXVA2VPGuid;
		DXVA2_VideoProcessorCaps DXVA2VPcaps;
		m_DXVA2VP.GetVPParams(DXVA2VPGuid, DXVA2VPcaps);

		str.AppendFormat(L"DXVA2 %s", DXVA2VPDeviceToString(DXVA2VPGuid));

		UINT dt = DXVA2VPcaps.DeinterlaceTechnology;
		str.Append(L"\nDeinterlaceTech.:");
		if (dt & DXVA2_DeinterlaceTech_Mask) {
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
			str.AppendFormat(L"\nReferenceSamples: Backward %u, Forward %u", DXVA2VPcaps.NumBackwardRefSamples, DXVA2VPcaps.NumForwardRefSamples);
		} else {
			str.Append(L" none");
		}
	} else {
		str.Append(L"Shaders");
	}

	str.AppendFormat(L"\nDisplay Mode    : %u x %u, %u", m_DisplayMode.Width, m_DisplayMode.Height, m_DisplayMode.RefreshRate);
	if (m_DisplayMode.ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED) {
		str.AppendChar('i');
	}
	str.Append(L" Hz");

	if (m_pPostScaleShaders.size()) {
		str.Append(L"\n\nPost scale pixel shaders:");
		for (const auto& pshader : m_pPostScaleShaders) {
			str.AppendFormat(L"\n  %s", pshader.name);
		}
	}

#ifdef _DEBUG
	str.Append(L"\n\nDEBUG info:");
	str.AppendFormat(L"\nSource rect   : %d,%d,%d,%d - %dx%d", m_srcRect.left, m_srcRect.top, m_srcRect.right, m_srcRect.bottom, m_srcRect.Width(), m_srcRect.Height());
	str.AppendFormat(L"\nTarget rect   : %d,%d,%d,%d - %dx%d", m_trgRect.left, m_trgRect.top, m_trgRect.right, m_trgRect.bottom, m_trgRect.Width(), m_trgRect.Height());
	str.AppendFormat(L"\nVideo rect    : %d,%d,%d,%d - %dx%d", m_videoRect.left, m_videoRect.top, m_videoRect.right, m_videoRect.bottom, m_videoRect.Width(), m_videoRect.Height());
	str.AppendFormat(L"\nWindow rect   : %d,%d,%d,%d - %dx%d", m_windowRect.left, m_windowRect.top, m_windowRect.right, m_windowRect.bottom, m_windowRect.Width(), m_windowRect.Height());
	str.AppendFormat(L"\nSrcRender rect: %d,%d,%d,%d - %dx%d", m_srcRenderRect.left, m_srcRenderRect.top, m_srcRenderRect.right, m_srcRenderRect.bottom, m_srcRenderRect.Width(), m_srcRenderRect.Height());
	str.AppendFormat(L"\nDstRender rect: %d,%d,%d,%d - %dx%d", m_dstRenderRect.left, m_dstRenderRect.top, m_dstRenderRect.right, m_dstRenderRect.bottom, m_dstRenderRect.Width(), m_dstRenderRect.Height());
#endif

	return S_OK;
}

void CDX9VideoProcessor::SetTexFormat(int value)
{
	switch (value) {
	case TEXFMT_AUTOINT:
	case TEXFMT_8INT:
	case TEXFMT_10INT:
	case TEXFMT_16FLOAT:
		m_iTexFormat = value;
		break;
	default:
		DLog(L"CDX9VideoProcessor::SetTexFormat() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
}

void CDX9VideoProcessor::SetVPEnableFmts(const VPEnableFormats_t& VPFormats)
{
	m_VPFormats = VPFormats;
}

void CDX9VideoProcessor::SetVPScaling(bool value)
{
	m_bVPScaling = value;
	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
}

void CDX9VideoProcessor::SetChromaScaling(int value)
{
	if (value < 0 || value >= CHROMA_COUNT) {
		DLog(L"CDX9VideoProcessor::SetChromaScaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iChromaScaling = value;

	if (m_pD3DDevEx) {
		EXECUTE_ASSERT(S_OK == UpdateChromaScalingShader());
		UpdateStatsStatic();
	}
}

void CDX9VideoProcessor::SetUpscaling(int value)
{
	if (value < 0 || value >= UPSCALE_COUNT) {
		DLog(L"CDX9VideoProcessor::SetUpscaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}
	m_iUpscaling = value;

	if (m_pD3DDevEx) {
		UpdateUpscalingShaders();
	}
};

void CDX9VideoProcessor::SetDownscaling(int value)
{
	if (value < 0 || value >= DOWNSCALE_COUNT) {
		DLog(L"CDX9VideoProcessor::SetDownscaling() unknown value %d", value);
		ASSERT(FALSE);
		return;
	}

	m_iDownscaling = value;

	if (m_pD3DDevEx) {
		UpdateDownscalingShaders();
	}
};

void CDX9VideoProcessor::SetRotation(int value)
{
	m_iRotation = value;
	UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
}

void CDX9VideoProcessor::Flush()
{
	if (m_DXVA2VP.IsReady()) {
		if (m_iSrcFromGPU && m_DXVA2VP.GetNumRefSamples() == 1) {
			m_DXVA2VP.ClearInputSurfaces(m_srcExFmt);
		} else {
			m_DXVA2VP.CleanSamplesData();
		}
	}
}

void CDX9VideoProcessor::ClearPostScaleShaders()
{
	for (auto& pScreenShader : m_pPostScaleShaders) {
		pScreenShader.shader.Release();
	}
	m_pPostScaleShaders.clear();
	DLog(L"CDX9VideoProcessor::ClearPostScaleShaders().");
}

HRESULT CDX9VideoProcessor::AddPostScaleShader(const CStringW& name, const CStringA& srcCode)
{
	HRESULT hr = S_OK;

	if (m_pD3DDevEx) {
		ID3DBlob* pShaderCode = nullptr;
		hr = CompileShader(srcCode, nullptr, "ps_3_0", &pShaderCode);
		if (S_OK == hr) {
			m_pPostScaleShaders.emplace_back();
			hr = m_pD3DDevEx->CreatePixelShader((const DWORD*)pShaderCode->GetBufferPointer(), &m_pPostScaleShaders.back().shader);
			if (S_OK == hr) {
				m_pPostScaleShaders.back().name = name;
				if (m_pPostScaleShaders.size() <= 2) {
					UpdateTexures(m_videoRect.Width(), m_videoRect.Height());
				}
				DLog(L"CDX9VideoProcessor::AddPostScaleShader() : \"%s\" pixel shader added successfully.", name);
			} else {
				DLog(L"CDX9VideoProcessor::AddPostScaleShader() : create pixel shader \"%s\" FAILED!", name);
				m_pPostScaleShaders.pop_back();
			}
			pShaderCode->Release();
		}
	}

	return hr;
}

void CDX9VideoProcessor::UpdateTexures(int w, int h)
{
	if (!m_srcWidth || !m_srcHeight) {
		return;
	}

	// TODO: try making w and h a multiple of 128.
	HRESULT hr = S_OK;

	if (m_DXVA2VP.IsReady()) {
		if (m_bVPScaling) {
			if (m_iRotation == 90 || m_iRotation == 270) {
				hr = m_TexDxvaOutput.CheckCreate(m_pD3DDevEx, m_DXVA2OutputFmt, h, w, D3DUSAGE_RENDERTARGET);
			} else {
				hr = m_TexDxvaOutput.CheckCreate(m_pD3DDevEx, m_DXVA2OutputFmt, w, h, D3DUSAGE_RENDERTARGET);
			}
		} else {
			hr = m_TexDxvaOutput.CheckCreate(m_pD3DDevEx, m_DXVA2OutputFmt, m_srcWidth, m_srcHeight, D3DUSAGE_RENDERTARGET);
		}
		m_TexConvertOutput.Release();
	}
	else {
		m_TexDxvaOutput.Release();
		hr = m_TexConvertOutput.CheckCreate(m_pD3DDevEx, m_InternalTexFmt, m_srcWidth, m_srcHeight, D3DUSAGE_RENDERTARGET);
	}

	m_bFinalPass = (m_InternalTexFmt != D3DFMT_X8R8G8B8 && m_TexDither.pTexture && m_pPSFinalPass);

	UINT numPostScaleShaders = m_pPostScaleShaders.size();
	if (m_pPSCorrection) {
		numPostScaleShaders++;
	}
	if (m_bFinalPass) {
		numPostScaleShaders++;
	}
	hr = m_TexsPostScale.CheckCreate(m_pD3DDevEx, m_InternalTexFmt, w, h, numPostScaleShaders);
}

void CDX9VideoProcessor::UpdateUpscalingShaders()
{
	m_pShaderUpscaleX.Release();
	m_pShaderUpscaleY.Release();

	if (m_iUpscaling != UPSCALE_Nearest) {
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleX, s_Upscaling9ResIDs[m_iUpscaling].shaderX));
		EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderUpscaleY, s_Upscaling9ResIDs[m_iUpscaling].shaderY));
	}
}

void CDX9VideoProcessor::UpdateDownscalingShaders()
{
	m_pShaderDownscaleX.Release();
	m_pShaderDownscaleY.Release();

	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleX, s_Downscaling9ResIDs[m_iDownscaling].shaderX));
	EXECUTE_ASSERT(S_OK == CreatePShaderFromResource(&m_pShaderDownscaleY, s_Downscaling9ResIDs[m_iDownscaling].shaderY));
}

HRESULT CDX9VideoProcessor::UpdateChromaScalingShader()
{
	m_pPSConvertColor.Release();
	HRESULT hr = S_OK;

	if (m_TexSrcVideo.pTexture) {
		ID3DBlob* pShaderCode = nullptr;
		hr = GetShaderConvertColor(false, m_TexSrcVideo.Width, m_TexSrcVideo.Height, m_srcParams, m_srcExFmt, m_iChromaScaling, &pShaderCode);
		if (S_OK == hr) {
			hr = m_pD3DDevEx->CreatePixelShader((const DWORD*)pShaderCode->GetBufferPointer(), &m_pPSConvertColor);
			pShaderCode->Release();
		}
	}

	return hr;
}

HRESULT CDX9VideoProcessor::DxvaVPPass(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second)
{
	HRESULT hr = m_DXVA2VP.SetRectangles(srcRect, dstRect);

	hr = m_DXVA2VP.Process(pRenderTarget, m_CurrentSampleFmt, second);
	DLogIf(FAILED(hr), L"CDX9VideoProcessor::DxvaVPPass() : VideoProcessBlt() failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX9VideoProcessor::ConvertColorPass(IDirect3DSurface9* pRenderTarget)
{
	HRESULT hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);

	float w = (float)m_TexSrcVideo.Width;
	float h = (float)m_TexSrcVideo.Height;
	float sx = 0.0f;
	float sy = 0.0f;

	if (m_srcParams.cformat == CF_YUY2) {
		w *= 2;
	}
	else if (m_iChromaScaling == CHROMA_Bilinear) {
		if (m_srcParams.Subsampling == 420) {
			switch (m_srcExFmt.VideoChromaSubsampling) {
			case DXVA2_VideoChromaSubsampling_Cosited:
				sx = 0.5f / w;
				sy = 0.5f / h;
				break;
			case DXVA2_VideoChromaSubsampling_MPEG1:
				//nothing;
				break;
			case DXVA2_VideoChromaSubsampling_MPEG2:
			default:
				sx = 0.5f / w;
			}
		}
		else if (m_srcParams.Subsampling == 422) {
			sx = 0.5f / w;
		}
	}

	w -= 0.5f;
	h -= 0.5f;

	MYD3DVERTEX<2> v[] = {
		{ {-0.5f, -0.5f, 0.5f, 2.0f}, {{0, 0}, {0+sx, 0+sy}} },
		{ {    w, -0.5f, 0.5f, 2.0f}, {{1, 0}, {1+sx, 0+sy}} },
		{ {-0.5f,     h, 0.5f, 2.0f}, {{0, 1}, {0+sx, 1+sy}} },
		{ {    w,     h, 0.5f, 2.0f}, {{1, 1}, {1+sx, 1+sy}} },
	};

	hr = m_pD3DDevEx->SetPixelShaderConstantF(0, (float*)m_PSConvColorData.fConstants, std::size(m_PSConvColorData.fConstants));
	hr = m_pD3DDevEx->SetPixelShader(m_pPSConvertColor);

	hr = m_pD3DDevEx->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_LIGHTING, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_ZENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	hr = m_pD3DDevEx->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);

	hr = m_pD3DDevEx->SetTexture(0, m_TexSrcVideo.pTexture);
	DWORD FVF = D3DFVF_TEX1;
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	if (m_TexSrcVideo.Plane2.pTexture) {
		DWORD dwMagFilter = (m_srcParams.Subsampling == 444) ? D3DTEXF_POINT : D3DTEXF_LINEAR;

		hr = m_pD3DDevEx->SetTexture(1, m_TexSrcVideo.Plane2.pTexture);
		FVF = D3DFVF_TEX2;
		hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MAGFILTER, dwMagFilter);
		hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		if (m_TexSrcVideo.Plane3.pTexture) {
			hr = m_pD3DDevEx->SetTexture(2, m_TexSrcVideo.Plane3.pTexture);
			FVF = D3DFVF_TEX3;
			hr = m_pD3DDevEx->SetSamplerState(2, D3DSAMP_MAGFILTER, dwMagFilter);
			hr = m_pD3DDevEx->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			hr = m_pD3DDevEx->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			hr = m_pD3DDevEx->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			hr = m_pD3DDevEx->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		}
	}

	hr = m_pD3DDevEx->SetFVF(D3DFVF_XYZRHW | FVF);
	hr = m_pD3DDevEx->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0]));

	m_pD3DDevEx->SetPixelShader(nullptr);

	m_pD3DDevEx->SetTexture(0, nullptr);
	m_pD3DDevEx->SetTexture(1, nullptr);
	m_pD3DDevEx->SetTexture(2, nullptr);

	return hr;

}

HRESULT CDX9VideoProcessor::ResizeShaderPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect)
{
	HRESULT hr = S_OK;
	const int w2 = dstRect.Width();
	const int h2 = dstRect.Height();
	const int k = m_bInterpolateAt50pct ? 2 : 1;

	int w1, h1;
	IDirect3DPixelShader9* resizerX;
	IDirect3DPixelShader9* resizerY;
	if (m_iRotation == 90 || m_iRotation == 270) {
		w1 = srcRect.Height();
		h1 = srcRect.Width();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p; // use Y scaling here
		if (resizerX) {
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p;
		} else {
			resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleX.p : m_pShaderUpscaleX.p; // use X scaling here
		}
	} else {
		w1 = srcRect.Width();
		h1 = srcRect.Height();
		resizerX = (w1 == w2) ? nullptr : (w1 > k * w2) ? m_pShaderDownscaleX.p : m_pShaderUpscaleX.p;
		resizerY = (h1 == h2) ? nullptr : (h1 > k * h2) ? m_pShaderDownscaleY.p : m_pShaderUpscaleY.p;
	}

	if (resizerX && resizerY) {
		// two pass resize

		// check intermediate texture
		const UINT texWidth = w2;
		const UINT texHeight = h1;

		if (m_TexResize.pTexture) {
			if (texWidth != m_TexResize.Width || texHeight != m_TexResize.Height) {
				m_TexResize.Release(); // need new texture
			}
		}

		if (!m_TexResize.pTexture) {
			// use only float textures here
			hr = m_TexResize.Create(m_pD3DDevEx, D3DFMT_A16B16G16R16F,texWidth, texHeight, D3DUSAGE_RENDERTARGET);
			if (FAILED(hr)) {
				DLog(L"CDX9VideoProcessor::ProcessTex() : m_TexResize.Create() failed with error %s", HR2Str(hr));
				return TextureCopyRect(pTexture, srcRect, dstRect, D3DTEXF_LINEAR, m_iRotation);
			}
		}

		CRect resizeRect(0, 0, m_TexResize.Width, m_TexResize.Height);

		// resize width
		hr = m_pD3DDevEx->SetRenderTarget(0, m_TexResize.pSurface);
		hr = TextureResizeShader(pTexture, srcRect, resizeRect, resizerX, m_iRotation);

		// resize height
		hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);
		hr = TextureResizeShader(m_TexResize.pTexture, resizeRect, dstRect, resizerY, 0);
	}
	else {
		hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);

		if (resizerX) {
			// one pass resize for width
			hr = TextureResizeShader(pTexture, srcRect, dstRect, resizerX, m_iRotation);
		}
		else if (resizerY) {
			// one pass resize for height
			hr = TextureResizeShader(pTexture, srcRect, dstRect, resizerY, m_iRotation);
		}
		else {
			// no resize
			hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);
			hr = TextureCopyRect(pTexture, srcRect, dstRect, D3DTEXF_POINT, m_iRotation);
		}
	}

	DLogIf(FAILED(hr), L"CDX9VideoProcessor::ResizeShaderPass() : failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX9VideoProcessor::FinalPass(IDirect3DTexture9* pTexture, IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect)
{
	HRESULT hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	const float w = (float)desc.Width;
	const float h = (float)desc.Height;
	const float dx = 1.0f / w;
	const float dy = 1.0f / h;

	MYD3DVERTEX<1> v[] = {
		{ {(float)dstRect.left  - 0.5f, (float)dstRect.top    - 0.5f, 0.5f, 2.0f}, {{srcRect.left  * dx, srcRect.top    * dy}} },
		{ {(float)dstRect.right - 0.5f, (float)dstRect.top    - 0.5f, 0.5f, 2.0f}, {{srcRect.right * dx, srcRect.top    * dy}} },
		{ {(float)dstRect.left  - 0.5f, (float)dstRect.bottom - 0.5f, 0.5f, 2.0f}, {{srcRect.left  * dx, srcRect.bottom * dy}} },
		{ {(float)dstRect.right - 0.5f, (float)dstRect.bottom - 0.5f, 0.5f, 2.0f}, {{srcRect.right * dx, srcRect.bottom * dy}} },
	};

	hr = m_pD3DDevEx->SetPixelShader(m_pPSFinalPass);

	// Set sampler: image
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	hr = m_pD3DDevEx->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	hr = m_pD3DDevEx->SetTexture(0, pTexture);

	// Set sampler: ditherMap
	hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	hr = m_pD3DDevEx->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

	hr = m_pD3DDevEx->SetTexture(1, m_TexDither.pTexture);

	// Set constants
	float fConstData[][4] = {
		{w / dither_size, h / dither_size, 0.0f, 0.0f}
	};
	hr = m_pD3DDevEx->SetPixelShaderConstantF(0, (float*)fConstData, _countof(fConstData));

	hr = TextureBlt(m_pD3DDevEx, v, D3DTEXF_POINT);

	m_pD3DDevEx->SetTexture(1, nullptr);

	return hr;
}

HRESULT CDX9VideoProcessor::Process(IDirect3DSurface9* pRenderTarget, const CRect& srcRect, const CRect& dstRect, const bool second)
{
	HRESULT hr = S_OK;

	CRect rSrc = srcRect;
	IDirect3DTexture9* pInputTexture = nullptr;

	if (m_DXVA2VP.IsReady()) {
		if (m_bVPScaling) {
			RECT rect = { 0, 0, m_TexDxvaOutput.Width, m_TexDxvaOutput.Height };
			hr = DxvaVPPass(m_TexDxvaOutput.pSurface, rSrc, rect, second);
			rSrc = rect;
		} else {
			hr = DxvaVPPass(m_TexDxvaOutput.pSurface, rSrc, rSrc, second);
		}

		pInputTexture = m_TexDxvaOutput.pTexture;
	}
	else if (m_PSConvColorData.bEnable) {
		ConvertColorPass(m_TexConvertOutput.pSurface);
		pInputTexture = m_TexConvertOutput.pTexture;
	}
	else {
		pInputTexture = m_TexSrcVideo.pTexture;
	}

	if (m_pPSCorrection || m_pPostScaleShaders.size() || m_bFinalPass) {
		Tex_t* Tex = m_TexsPostScale.GetFirstTex();
		RECT rect = { 0, 0, Tex->Width, Tex->Height };

		hr = ResizeShaderPass(pInputTexture, Tex->pSurface, rSrc, rect);

		rSrc = rect;

		if (m_pPSCorrection) {
			hr = m_pD3DDevEx->SetPixelShader(m_pPSCorrection);
		}

		if (m_pPostScaleShaders.size()) {
			if (m_pPSCorrection) {
				pInputTexture = Tex->pTexture;
				Tex = m_TexsPostScale.GetNextTex();
				hr = m_pD3DDevEx->SetRenderTarget(0, Tex->pSurface);
				hr = TextureCopyRect(pInputTexture, rect, rect, D3DTEXF_POINT, 0);
			}

			if (m_pPostScaleShaders.size()) {
				static __int64 counter = 0;
				static long start = GetTickCount();

				long stop = GetTickCount();
				long diff = stop - start;
				if (diff >= 10 * 60 * 1000) {
					start = stop;    // reset after 10 min (ps float has its limits in both range and accuracy)
				}
				float fConstData[][4] = {
					{(float)Tex->Width, (float)Tex->Height, (float)(counter++), (float)diff / 1000},
					{1.0f / Tex->Width, 1.0f / Tex->Height, 0, 0},
				};
				hr = m_pD3DDevEx->SetPixelShaderConstantF(0, (float*)fConstData, _countof(fConstData));

				for (UINT idx = 0; idx < m_pPostScaleShaders.size() - 1; idx++) {
					pInputTexture = Tex->pTexture;
					Tex = m_TexsPostScale.GetNextTex();
					hr = m_pD3DDevEx->SetPixelShader(m_pPostScaleShaders[idx].shader);
					hr = m_pD3DDevEx->SetRenderTarget(0, Tex->pSurface);
					hr = TextureCopyRect(pInputTexture, rect, rect, D3DTEXF_POINT, 0);
				}
				hr = m_pD3DDevEx->SetPixelShader(m_pPostScaleShaders.back().shader);
			}
		}

		if (m_bFinalPass) {
			if (m_pPSCorrection || m_pPostScaleShaders.size()) {
				pInputTexture = Tex->pTexture;
				Tex = m_TexsPostScale.GetNextTex();
				hr = m_pD3DDevEx->SetRenderTarget(0, Tex->pSurface);
				hr = TextureCopyRect(pInputTexture, rect, rect, D3DTEXF_POINT, 0);
			}

			hr = FinalPass(Tex->pTexture, pRenderTarget, rect, dstRect);
		}
		else {
			hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);
			hr = TextureCopyRect(Tex->pTexture, rect, dstRect, D3DTEXF_POINT, 0);
		}
		m_pD3DDevEx->SetPixelShader(nullptr);
	}
	else {
		hr = ResizeShaderPass(pInputTexture, pRenderTarget, rSrc, dstRect);
	}

	DLogIf(FAILED(hr), L"CDX9VideoProcessor::Process() : failed with error %s", HR2Str(hr));

	return hr;
}

HRESULT CDX9VideoProcessor::TextureCopy(IDirect3DTexture9* pTexture)
{
	HRESULT hr;

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	float w = (float)desc.Width - 0.5f;
	float h = (float)desc.Height - 0.5f;

	MYD3DVERTEX<1> v[] = {
		{ {-0.5f, -0.5f, 0.5f, 2.0f}, {{0, 0}} },
		{ {    w, -0.5f, 0.5f, 2.0f}, {{1, 0}} },
		{ {-0.5f,     h, 0.5f, 2.0f}, {{0, 1}} },
		{ {    w,     h, 0.5f, 2.0f}, {{1, 1}} },
	};

	hr = m_pD3DDevEx->SetTexture(0, pTexture);

	return TextureBlt(m_pD3DDevEx, v, D3DTEXF_POINT);
}


HRESULT CDX9VideoProcessor::TextureCopyRect(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect, D3DTEXTUREFILTERTYPE filter, const int iRotation)
{
	HRESULT hr;

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	const float dx = 1.0f / desc.Width;
	const float dy = 1.0f / desc.Height;

	POINT points[4];
	switch (iRotation) {
	case 90:
		points[0] = { dstRect.right, dstRect.top    };
		points[1] = { dstRect.right, dstRect.bottom };
		points[2] = { dstRect.left,  dstRect.top    };
		points[3] = { dstRect.left,  dstRect.bottom };
		break;
	case 180:
		points[0] = { dstRect.right, dstRect.bottom };
		points[1] = { dstRect.left,  dstRect.bottom };
		points[2] = { dstRect.right, dstRect.top    };
		points[3] = { dstRect.left,  dstRect.top    };
		break;
	case 270:
		points[0] = { dstRect.left,  dstRect.bottom };
		points[1] = { dstRect.left,  dstRect.top    };
		points[2] = { dstRect.right, dstRect.bottom };
		points[3] = { dstRect.right, dstRect.top    };
		break;
	default:
		points[0] = { dstRect.left,  dstRect.top    };
		points[1] = { dstRect.right, dstRect.top    };
		points[2] = { dstRect.left,  dstRect.bottom };
		points[3] = { dstRect.right, dstRect.bottom };
		break;
	}

	MYD3DVERTEX<1> v[] = {
		{ {(float)points[0].x - 0.5f, (float)points[0].y - 0.5f, 0.5f, 2.0f}, {{srcRect.left  * dx, srcRect.top    * dy}} },
		{ {(float)points[1].x - 0.5f, (float)points[1].y - 0.5f, 0.5f, 2.0f}, {{srcRect.right * dx, srcRect.top    * dy}} },
		{ {(float)points[2].x - 0.5f, (float)points[2].y - 0.5f, 0.5f, 2.0f}, {{srcRect.left  * dx, srcRect.bottom * dy}} },
		{ {(float)points[3].x - 0.5f, (float)points[3].y - 0.5f, 0.5f, 2.0f}, {{srcRect.right * dx, srcRect.bottom * dy}} },
	};

	hr = m_pD3DDevEx->SetTexture(0, pTexture);
	hr = TextureBlt(m_pD3DDevEx, v, filter);

	return hr;
}

HRESULT CDX9VideoProcessor::TextureResizeShader(IDirect3DTexture9* pTexture, const CRect& srcRect, const CRect& dstRect, IDirect3DPixelShader9* pShader, const int iRotation)
{
	HRESULT hr = S_OK;

	D3DSURFACE_DESC desc;
	if (!pTexture || FAILED(pTexture->GetLevelDesc(0, &desc))) {
		return E_FAIL;
	}

	const float dx = 1.0f / desc.Width;
	const float dy = 1.0f / desc.Height;

	float scale_x = (float)srcRect.Width();
	float scale_y = (float)srcRect.Height();
	if (iRotation == 90 || iRotation == 270) {
		scale_x /= dstRect.Height();
		scale_y /= dstRect.Width();
	} else {
		scale_x /= dstRect.Width();
		scale_y /= dstRect.Height();
	}

	//const float steps_x = floor(scale_x + 0.5f);
	//const float steps_y = floor(scale_y + 0.5f);

	const float tx0 = (float)srcRect.left - 0.5f;
	const float ty0 = (float)srcRect.top - 0.5f;
	const float tx1 = (float)srcRect.right - 0.5f;
	const float ty1 = (float)srcRect.bottom - 0.5f;

	POINT points[4];
	switch (iRotation) {
	case 90:
		points[0] = { dstRect.right, dstRect.top };
		points[1] = { dstRect.right, dstRect.bottom };
		points[2] = { dstRect.left,  dstRect.top };
		points[3] = { dstRect.left,  dstRect.bottom };
		break;
	case 180:
		points[0] = { dstRect.right, dstRect.bottom };
		points[1] = { dstRect.left,  dstRect.bottom };
		points[2] = { dstRect.right, dstRect.top    };
		points[3] = { dstRect.left,  dstRect.top };
		break;
	case 270:
		points[0] = { dstRect.left,  dstRect.bottom };
		points[1] = { dstRect.left,  dstRect.top    };
		points[2] = { dstRect.right, dstRect.bottom };
		points[3] = { dstRect.right, dstRect.top    };
		break;
	default:
		points[0] = { dstRect.left,  dstRect.top    };
		points[1] = { dstRect.right, dstRect.top    };
		points[2] = { dstRect.left,  dstRect.bottom };
		points[3] = { dstRect.right, dstRect.bottom };
		break;
	}

	MYD3DVERTEX<1> v[] = {
		{ {(float)points[0].x - 0.5f, (float)points[0].y - 0.5f, 0.5f, 2.0f}, {{tx0, ty0}} },
		{ {(float)points[1].x - 0.5f, (float)points[1].y - 0.5f, 0.5f, 2.0f}, {{tx1, ty0}} },
		{ {(float)points[2].x - 0.5f, (float)points[2].y - 0.5f, 0.5f, 2.0f}, {{tx0, ty1}} },
		{ {(float)points[3].x - 0.5f, (float)points[3].y - 0.5f, 0.5f, 2.0f}, {{tx1, ty1}} },
	};

	float fConstData[][4] = {
		{ dx, dy, 0, 0 },
		{ scale_x, scale_y, 0, 0 },
	};
	hr = m_pD3DDevEx->SetPixelShaderConstantF(0, (float*)fConstData, std::size(fConstData));
	hr = m_pD3DDevEx->SetPixelShader(pShader);

	hr = m_pD3DDevEx->SetTexture(0, pTexture);
	hr = TextureBlt(m_pD3DDevEx, v, D3DTEXF_POINT);
	m_pD3DDevEx->SetPixelShader(nullptr);

	return hr;
}

void CDX9VideoProcessor::UpdateStatsStatic()
{
	if (m_srcParams.cformat) {
		m_strStatsStatic1.Format(L"MPC VR v%S, Direct3D 9Ex", MPCVR_VERSION_STR);
		m_strStatsStatic1.AppendFormat(L"\nGraph. Adapter: %s", m_strAdapterDescription);

		m_strStatsStatic2.Format(L"%S %ux%u", m_srcParams.str, m_srcRectWidth, m_srcRectHeight);
		if (m_srcParams.CSType == CS_YUV) {
			LPCSTR strs[6] = {};
			GetExtendedFormatString(strs, m_srcExFmt, m_srcParams.CSType);
			m_strStatsStatic2.AppendFormat(L"\n  Range: %hS", strs[1]);
			if (m_decExFmt.NominalRange == DXVA2_NominalRange_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Matrix: %hS", strs[2]);
			if (m_decExFmt.VideoTransferMatrix == DXVA2_VideoTransferMatrix_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Lighting: %hS", strs[3]);
			if (m_decExFmt.VideoLighting == DXVA2_VideoLighting_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L"\n  Primaries: %hS", strs[4]);
			if (m_decExFmt.VideoPrimaries == DXVA2_VideoPrimaries_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			m_strStatsStatic2.AppendFormat(L", Function: %hS", strs[5]);
			if (m_decExFmt.VideoTransferFunction == DXVA2_VideoTransFunc_Unknown) {
				m_strStatsStatic2.AppendChar('*');
			};
			if (m_srcParams.Subsampling == 420) {
				m_strStatsStatic2.AppendFormat(L"\n  ChromaLocation: %hS", strs[0]);
				if (m_decExFmt.VideoChromaSubsampling == DXVA2_VideoChromaSubsampling_Unknown) {
					m_strStatsStatic2.AppendChar('*');
				};
			}
		}
		m_strStatsStatic2.Append(L"\nVideoProcessor: ");
		if (m_DXVA2VP.IsReady()) {
			m_strStatsStatic2.AppendFormat(L"DXVA2 VP, output to %s", D3DFormatToString(m_DXVA2OutputFmt));
		} else {
			m_strStatsStatic2.Append(L"Shaders");
			if (m_srcParams.Subsampling == 420 || m_srcParams.Subsampling == 422) {
				m_strStatsStatic2.AppendFormat(L"\nChroma Scaling: %s", (m_iChromaScaling == CHROMA_CatmullRom) ? L"Catmull-Rom" : L"Bilinear");
			}
		}
		m_strStatsStatic2.AppendFormat(L"\nInternalFormat: %s", D3DFormatToString(m_InternalTexFmt));

		if (m_d3dpp.SwapEffect) {
			m_strStatsStatic3.SetString(L"\nPresentation  : ");
			switch (m_d3dpp.SwapEffect) {
			case D3DSWAPEFFECT_DISCARD:
				m_strStatsStatic3.Append(L"Discard");
				break;
			case D3DSWAPEFFECT_FLIP:
				m_strStatsStatic3.Append(L"Flip");
				break;
			case D3DSWAPEFFECT_COPY:
				m_strStatsStatic3.Append(L"Copy");
				break;
			case D3DSWAPEFFECT_OVERLAY:
				m_strStatsStatic3.Append(L"Overlay");
				break;
			case D3DSWAPEFFECT_FLIPEX:
				m_strStatsStatic3.Append(L"FlipEx");
				break;
			}
		}
	} else {
		m_strStatsStatic1 = L"Error";
		m_strStatsStatic2.Empty();
		m_strStatsStatic3.Empty();
	}
}

HRESULT CDX9VideoProcessor::DrawStats(IDirect3DSurface9* pRenderTarget)
{
	if (m_windowRect.IsRectEmpty()) {
		return E_ABORT;
	}

	CStringW str = m_strStatsStatic1;
	str.AppendFormat(L"\nFrame rate    : %7.03f", m_pFilter->m_FrameStats.GetAverageFps());
	if (m_CurrentSampleFmt >= DXVA2_SampleFieldInterleavedEvenFirst && m_CurrentSampleFmt <= DXVA2_SampleFieldSingleOdd) {
		str.AppendChar(L'i');
	}
	str.AppendFormat(L",%7.03f", m_pFilter->m_DrawStats.GetAverageFps());
	str.Append(L"\nInput format  :");
	if (m_iSrcFromGPU == 9) {
		str.Append(L" D3DFMT_");
	}
	str.Append(m_strStatsStatic2);

	const int srcW = m_srcRenderRect.Width();
	const int srcH = m_srcRenderRect.Height();
	const int dstW = m_dstRenderRect.Width();
	const int dstH = m_dstRenderRect.Height();
	if (m_iRotation) {
		str.AppendFormat(L"\nScaling       : %dx%d r%d°> %dx%d", srcW, srcH, m_iRotation, dstW, dstH);
	} else {
		str.AppendFormat(L"\nScaling       : %dx%d -> %dx%d", srcW, srcH, dstW, dstH);
	}
	if (srcW != dstW || srcH != dstH) {
		if (m_DXVA2VP.IsReady() && m_bVPScaling) {
			str.Append(L" DXVA2");
		}
		else {
			if (m_strShaderX) {
				str.AppendFormat(L" %s", m_strShaderX);
				if (m_strShaderY && m_strShaderY != m_strShaderX) {
					str.AppendFormat(L"/%s", m_strShaderY);
				}
			} else if (m_strShaderY) {
				str.AppendFormat(L" %s", m_strShaderY);
			}
		}
	}
	str.Append(m_strStatsStatic3);

	str.AppendFormat(L"\nFrames: %5u, skipped: %u/%u, failed: %u",
		m_pFilter->m_FrameStats.GetFrames(), m_pFilter->m_DrawStats.m_dropped, m_RenderStats.dropped2, m_RenderStats.failed);
	str.AppendFormat(L"\nTimes(ms): Copy%3llu, Render%3llu, Subs%3llu, Stats%3llu",
		m_RenderStats.copyticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.renderticks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.substicks * 1000 / GetPreciseTicksPerSecondI(),
		m_RenderStats.statsticks * 1000 / GetPreciseTicksPerSecondI());
#if 0
	str.AppendFormat(L"\n1:%6.03f, 2:%6.03f, 3:%6.03f, 4:%6.03f, 5:%6.03f, 6:%6.03f ms",
		m_RenderStats.t1 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t2 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t3 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t4 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t5 * 1000.0 / GetPreciseTicksPerSecondI(),
		m_RenderStats.t6 * 1000.0 / GetPreciseTicksPerSecondI());
#else
	str.AppendFormat(L"\nSync offset   : %+3lld ms", (m_RenderStats.syncoffset + 5000) / 10000);
#endif

	HRESULT hr = S_OK;
	hr = m_pD3DDevEx->SetRenderTarget(0, m_TexStats.pSurface);

	hr = m_pD3DDevEx->ColorFill(m_TexStats.pSurface, nullptr, D3DCOLOR_ARGB(192, 0, 0, 0));

	hr = m_pD3DDevEx->BeginScene();
	hr = m_Font3D.Draw2DText(5, 5, D3DCOLOR_XRGB(255, 255, 255), str);
	static int col = STATS_W;
	if (--col < 0) {
		col = STATS_W;
	}
	m_Rect3D.Set({ col, STATS_H - 11, col + 5, STATS_H - 1 }, D3DCOLOR_XRGB(128, 255, 128));
	m_Rect3D.Draw(m_pD3DDevEx);

	hr = m_pD3DDevEx->SetRenderTarget(0, pRenderTarget);

	hr = AlphaBlt(m_pD3DDevEx, CRect(0, 0, STATS_W, STATS_H), CRect(STATS_X, STATS_Y, STATS_X + STATS_W, STATS_X + STATS_H), m_TexStats.pTexture);
	m_pD3DDevEx->EndScene();

	return hr;
}

// IUnknown
STDMETHODIMP CDX9VideoProcessor::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv) {
		return E_POINTER;
	}
	if (riid == IID_IUnknown) {
		*ppv = static_cast<IUnknown*>(static_cast<IMFVideoProcessor*>(this));
	}
	else if (riid == IID_IMFVideoProcessor) {
		*ppv = static_cast<IMFVideoProcessor*>(this);
	}
	else {
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) CDX9VideoProcessor::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}

STDMETHODIMP_(ULONG) CDX9VideoProcessor::Release()
{
	ULONG uCount = InterlockedDecrement(&m_nRefCount);
	if (uCount == 0) {
		delete this;
	}
	// For thread safety, return a temporary variable.
	return uCount;
}

// IMFVideoProcessor

STDMETHODIMP CDX9VideoProcessor::GetProcAmpRange(DWORD dwProperty, DXVA2_ValueRange *pPropRange)
{
	CheckPointer(pPropRange, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	switch (dwProperty) {
	case DXVA2_ProcAmp_Brightness: memcpy(pPropRange, &m_DXVA2ProcAmpRanges[0], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Contrast:   memcpy(pPropRange, &m_DXVA2ProcAmpRanges[1], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Hue:        memcpy(pPropRange, &m_DXVA2ProcAmpRanges[2], sizeof(DXVA2_ValueRange)); break;
	case DXVA2_ProcAmp_Saturation: memcpy(pPropRange, &m_DXVA2ProcAmpRanges[3], sizeof(DXVA2_ValueRange)); break;
	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::GetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *Values)
{
	CheckPointer(Values, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags&DXVA2_ProcAmp_Brightness) { Values->Brightness = m_DXVA2ProcAmpValues.Brightness; }
	if (dwFlags&DXVA2_ProcAmp_Contrast)   { Values->Contrast   = m_DXVA2ProcAmpValues.Contrast; }
	if (dwFlags&DXVA2_ProcAmp_Hue)        { Values->Hue        = m_DXVA2ProcAmpValues.Hue; }
	if (dwFlags&DXVA2_ProcAmp_Saturation) { Values->Saturation = m_DXVA2ProcAmpValues.Saturation; }

	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::SetProcAmpValues(DWORD dwFlags, DXVA2_ProcAmpValues *pValues)
{
	CheckPointer(pValues, E_POINTER);
	if (m_srcParams.cformat == CF_NONE) {
		return MF_E_TRANSFORM_TYPE_NOT_SET;
	}

	if (dwFlags&DXVA2_ProcAmp_Mask) {
		CAutoLock cRendererLock(&m_pFilter->m_RendererLock);

		if (dwFlags&DXVA2_ProcAmp_Brightness) {
			m_DXVA2ProcAmpValues.Brightness.ll = std::clamp(pValues->Brightness.ll, m_DXVA2ProcAmpRanges[0].MinValue.ll, m_DXVA2ProcAmpRanges[0].MaxValue.ll);
		}
		if (dwFlags&DXVA2_ProcAmp_Contrast) {
			m_DXVA2ProcAmpValues.Contrast.ll = std::clamp(pValues->Contrast.ll, m_DXVA2ProcAmpRanges[1].MinValue.ll, m_DXVA2ProcAmpRanges[1].MaxValue.ll);
		}
		if (dwFlags&DXVA2_ProcAmp_Hue) {
			m_DXVA2ProcAmpValues.Hue.ll = std::clamp(pValues->Hue.ll, m_DXVA2ProcAmpRanges[2].MinValue.ll, m_DXVA2ProcAmpRanges[2].MaxValue.ll);
		}
		if (dwFlags&DXVA2_ProcAmp_Saturation) {
			m_DXVA2ProcAmpValues.Saturation.ll = std::clamp(pValues->Saturation.ll, m_DXVA2ProcAmpRanges[3].MinValue.ll, m_DXVA2ProcAmpRanges[3].MaxValue.ll);
		}

		m_DXVA2VP.SetProcAmpValues(m_DXVA2ProcAmpValues);
		if (!m_DXVA2VP.IsReady()) {
			SetShaderConvertColorParams();
		}
	}

	return S_OK;
}

STDMETHODIMP CDX9VideoProcessor::GetBackgroundColor(COLORREF *lpClrBkg)
{
	CheckPointer(lpClrBkg, E_POINTER);
	*lpClrBkg = RGB(0, 0, 0);
	return S_OK;
}
