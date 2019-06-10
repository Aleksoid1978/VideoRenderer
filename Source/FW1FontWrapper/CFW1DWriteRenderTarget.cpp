// CFW1DWriteRenderTarget.cpp

#include "FW1Precompiled.h"

#include "CFW1DWriteRenderTarget.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1DWriteRenderTarget::CFW1DWriteRenderTarget() :
	m_pRenderTarget(NULL),
	m_hDC(NULL),
	m_hBlackBrush(NULL),
	m_bmWidthBytes(0),
	m_bmBytesPixel(0),
	m_renderTargetWidth(0),
	m_renderTargetHeight(0)
{
}


// Destruct
CFW1DWriteRenderTarget::~CFW1DWriteRenderTarget() {
	if(m_hBlackBrush != NULL)
		DeleteObject(m_hBlackBrush);
	
	SAFE_RELEASE(m_pRenderTarget);
	
	for(RenderingParamsMap::iterator it = m_renderingParams.begin(); it != m_renderingParams.end(); ++it)
		it->second->Release();
}


// Init
HRESULT CFW1DWriteRenderTarget::initRenderTarget(
	IFW1Factory *pFW1Factory,
	IDWriteFactory *pDWriteFactory,
	UINT renderTargetWidth,
	UINT renderTargetHeight
) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	if(pDWriteFactory == NULL)
		return E_INVALIDARG;
	
	m_renderTargetWidth = 384;
	if(renderTargetWidth > 0)
		m_renderTargetWidth = renderTargetWidth;
	
	m_renderTargetHeight = 384;
	if(renderTargetHeight > 0)
		m_renderTargetHeight = renderTargetHeight;
	
	// Create render target
	hResult = createRenderTarget(pDWriteFactory);
	
	if(SUCCEEDED(hResult))
		hResult = S_OK;
	
	return hResult;
}


// Create render target
HRESULT CFW1DWriteRenderTarget::createRenderTarget(IDWriteFactory *pDWriteFactory) {
	IDWriteGdiInterop *pGDIInterop;
	HRESULT hResult = pDWriteFactory->GetGdiInterop(&pGDIInterop);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to get GDI interop";
	}
	else {
		IDWriteBitmapRenderTarget *pRenderTarget;
		hResult = pGDIInterop->CreateBitmapRenderTarget(
			NULL,
			m_renderTargetWidth,
			m_renderTargetHeight,
			&pRenderTarget
		);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create bitmap render target";
		}
		else {
			hResult = pRenderTarget->SetPixelsPerDip(1.0f);
			hResult = S_OK;
			
			HDC hDC = pRenderTarget->GetMemoryDC();
			if(hDC == NULL) {
				m_lastError = L"Failed to get render target DC";
				hResult = E_FAIL;
			}
			else {
				HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
				if(hBrush == NULL) {
					m_lastError = L"Failed to create brush";
					hResult = E_FAIL;
				}
				else {
					HBITMAP hBitmap = static_cast<HBITMAP>(GetCurrentObject(hDC, OBJ_BITMAP));
					if(hBitmap == NULL) {
						m_lastError = L"GetCurrentObject failed";
						hResult = E_FAIL;
					}
					else {
						DIBSECTION dib;
						int iResult = GetObject(hBitmap, sizeof(dib), &dib);
						if(iResult < sizeof(dib)) {
							m_lastError = L"GetObject failed";
							hResult = E_FAIL;
						}
						else {
							// Store render target resources and info
							m_pRenderTarget = pRenderTarget;
							
							m_hDC = hDC;
							m_hBlackBrush = hBrush;
							
							m_bmBits = dib.dsBm.bmBits;
							m_bmWidthBytes = static_cast<UINT>(dib.dsBm.bmWidthBytes);
							m_bmBytesPixel = static_cast<UINT>(dib.dsBm.bmBitsPixel) / 8;
							
							hResult = S_OK;
						}
					}
					
					if(FAILED(hResult))
						DeleteObject(hBrush);
				}
			}
			
			if(FAILED(hResult))
				pRenderTarget->Release();
		}
		
		pGDIInterop->Release();
	}
	
	// Create rendering params for all accepted rendering modes
	if(SUCCEEDED(hResult)) {
		const UINT renderingModeCount = 2;
		DWRITE_RENDERING_MODE renderingModes[renderingModeCount] = {
			DWRITE_RENDERING_MODE_DEFAULT,
			DWRITE_RENDERING_MODE_ALIASED
		};
		
		for(UINT i=0; i < renderingModeCount; ++i) {
			DWRITE_RENDERING_MODE renderingMode = renderingModes[i];
			IDWriteRenderingParams *pRenderingParams;
			
			hResult = pDWriteFactory->CreateCustomRenderingParams(
				1.0f,
				0.0f,
				0.0f,
				DWRITE_PIXEL_GEOMETRY_FLAT,
				renderingMode,
				&pRenderingParams
			);
			if(SUCCEEDED(hResult))
				m_renderingParams.insert(std::make_pair(renderingMode, pRenderingParams));
		}
		
		if(m_renderingParams.empty()) {
			m_lastError = L"Failed to create rendering params";
			hResult = E_FAIL;
		}
		else
			hResult = S_OK;
	}
	
	return hResult;
}


// Init glyph data
void CFW1DWriteRenderTarget::initGlyphData(
	const DWRITE_FONT_METRICS *fontMetrics,
	const DWRITE_GLYPH_METRICS *glyphMetrics,
	FLOAT fontSize,
	DWGlyphData *outGlyphData
) {
	// Calculate pixel-space coordinates
	FLOAT fscale = fontSize / static_cast<FLOAT>(fontMetrics->designUnitsPerEm);
	
	FLOAT l = static_cast<FLOAT>(glyphMetrics->leftSideBearing) * fscale;
	FLOAT t = static_cast<FLOAT>(glyphMetrics->topSideBearing) * fscale;
	
	FLOAT r = static_cast<FLOAT>(glyphMetrics->rightSideBearing) * fscale;
	FLOAT b = static_cast<FLOAT>(glyphMetrics->bottomSideBearing) * fscale;
	
	FLOAT v = static_cast<FLOAT>(glyphMetrics->verticalOriginY) * fscale;
	
	FLOAT aw = static_cast<FLOAT>(glyphMetrics->advanceWidth) * fscale;
	FLOAT ah = static_cast<FLOAT>(glyphMetrics->advanceHeight) * fscale;
	
	// Set up glyph data
	outGlyphData->offsetX = floor(l);
	outGlyphData->offsetY = floor(t) - floor(v);
	outGlyphData->maxWidth = static_cast<LONG>(aw - r - l + 2.0f);
	outGlyphData->maxHeight = static_cast<LONG>(ah - b - t + 2.0f);
}


}// namespace FW1FontWrapper
