// CFW1DWriteRenderTargetInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1DWriteRenderTarget.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1DWriteRenderTarget::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1DWriteRenderTarget))) {
		*ppvObject = static_cast<IFW1DWriteRenderTarget*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Draw glyph to temporary storage
HRESULT STDMETHODCALLTYPE CFW1DWriteRenderTarget::DrawGlyphTemp(
	IDWriteFontFace *pFontFace,
	UINT16 GlyphIndex,
	FLOAT FontSize,
	DWRITE_RENDERING_MODE RenderingMode,
	DWRITE_MEASURING_MODE MeasuringMode,
	FW1_GLYPHIMAGEDATA *pOutData
) {
	// Font metrics
	DWRITE_FONT_METRICS fontMetrics;
	pFontFace->GetMetrics(&fontMetrics);
	
	// Glyph metrics
	DWRITE_GLYPH_METRICS glyphMetrics;
	HRESULT hResult = pFontFace->GetDesignGlyphMetrics(&GlyphIndex, 1, &glyphMetrics, FALSE);
	if(FAILED(hResult))
		return hResult;
	
	// Calculate pixel measurements
	DWGlyphData dwGlyphData;
	initGlyphData(&fontMetrics, &glyphMetrics, FontSize, &dwGlyphData);
	
	// Set up drawing
	FLOAT glyphAdvance = 0.0f;
	DWRITE_GLYPH_OFFSET glyphOffset = {0.0f, 0.0f};
	
	DWRITE_GLYPH_RUN glyphRun;
	ZeroMemory(&glyphRun, sizeof(glyphRun));
	glyphRun.fontFace = pFontFace;
	glyphRun.fontEmSize = FontSize;
	glyphRun.glyphCount = 1;
	glyphRun.glyphIndices = &GlyphIndex;
	glyphRun.glyphAdvances = &glyphAdvance;
	glyphRun.glyphOffsets = &glyphOffset;
	
	// Clear background
	RECT rect;
	SetRect(&rect, 0, 0, 2+dwGlyphData.maxWidth+5, 2+dwGlyphData.maxHeight+5);
	int iRet = FillRect(m_hDC, &rect, m_hBlackBrush);
	if(iRet == 0) {
	}
	
	// Rendering mode
	IDWriteRenderingParams *pRenderingParams;
	
	RenderingParamsMap::iterator it = m_renderingParams.find(RenderingMode);
	if(it != m_renderingParams.end())
		pRenderingParams = it->second;
	else
		pRenderingParams = m_renderingParams.begin()->second;
	
	// Draw
	hResult = m_pRenderTarget->DrawGlyphRun(
		2.0f - dwGlyphData.offsetX,
		2.0f - dwGlyphData.offsetY,
		MeasuringMode,
		&glyphRun,
		pRenderingParams,
		RGB(255, 255, 255),
		&rect
	);
	if(FAILED(hResult))
		return hResult;
	
	// Clip to valid render target to avoid buffer overruns in case the glyph was too large
	rect.left = std::max(rect.left, 0L);
	rect.top = std::max(rect.top, 0L);
	rect.right = std::min(static_cast<LONG>(m_renderTargetWidth), rect.right);
	rect.bottom = std::min(static_cast<LONG>(m_renderTargetHeight), rect.bottom);
	
	// Return glyph data
	pOutData->Metrics.OffsetX = dwGlyphData.offsetX + static_cast<FLOAT>(rect.left) - 2.0f;
	pOutData->Metrics.OffsetY = dwGlyphData.offsetY + static_cast<FLOAT>(rect.top) - 2.0f;
	pOutData->Metrics.Width = static_cast<UINT>(rect.right - rect.left);
	pOutData->Metrics.Height = static_cast<UINT>(rect.bottom - rect.top);
	pOutData->pGlyphPixels =
		static_cast<const char*>(m_bmBits)
		+ rect.top * m_bmWidthBytes
		+ rect.left * m_bmBytesPixel;
	pOutData->RowPitch = m_bmWidthBytes;
	pOutData->PixelStride = m_bmBytesPixel;
	
	return S_OK;
}


}// namespace FW1FontWrapper
