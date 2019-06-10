// CFW1TextRendererInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1TextRenderer.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1TextRenderer::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IDWritePixelSnapping))) {
		*ppvObject = static_cast<IDWritePixelSnapping*>(m_pDWriteTextRendererProxy);
		AddRef();
		return S_OK;
	}
	else if(IsEqualIID(riid, __uuidof(IDWriteTextRenderer))) {
		*ppvObject = static_cast<IDWriteTextRenderer*>(m_pDWriteTextRendererProxy);
		AddRef();
		return S_OK;
	}
	else if(IsEqualIID(riid, __uuidof(IFW1TextRenderer))) {
		*ppvObject = static_cast<IFW1TextRenderer*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// IDWritePixelSnapping method
HRESULT CFW1TextRenderer::IsPixelSnappingDisabled(
	void *clientDrawingContext,
	BOOL *isDisabled
) {
	clientDrawingContext;
	
	*isDisabled = FALSE;
	
	return S_OK;
}


// IDWritePixelSnapping method
HRESULT CFW1TextRenderer::GetCurrentTransform(
	void *clientDrawingContext,
	DWRITE_MATRIX *transform
) {
	clientDrawingContext;
	
	transform->dx = 0.0f;
	transform->dy = 0.0f;
	transform->m11 = 1.0f;
	transform->m12 = 0.0f;
	transform->m21 = 0.0f;
	transform->m22 = 1.0f;
	
	return S_OK;
}


// IDWritePixelSnapping method
HRESULT CFW1TextRenderer::GetPixelsPerDip(void *clientDrawingContext, FLOAT *pixelsPerDip) {
	clientDrawingContext;
	
	*pixelsPerDip = 96.0f;
	
	return S_OK;
}


// IDWriteTextRenderer method
// Convert a run of glyphs to vertices
HRESULT CFW1TextRenderer::DrawGlyphRun(
	void *clientDrawingContext,
	FLOAT baselineOriginX,
	FLOAT baselineOriginY,
	DWRITE_MEASURING_MODE measuringMode,
	const DWRITE_GLYPH_RUN *glyphRun,
	const DWRITE_GLYPH_RUN_DESCRIPTION *glyphRunDescription,
	IUnknown *clientDrawingEffect
) {
	glyphRunDescription;
	measuringMode;
	
	const UINT flags = m_currentFlags;
	
	// Get glyph map for the current font
	const void *glyphMap;
	if(glyphRun->fontFace == m_pCachedGlyphMapFontFace && glyphRun->fontEmSize == m_cachedGlyphMapFontSize)
		glyphMap = m_cachedGlyphMap;
	else {
		glyphMap = m_pGlyphProvider->GetGlyphMapFromFont(glyphRun->fontFace, glyphRun->fontEmSize, flags);
		
		// Cache the glyph map as it's likely to be used in subsequent glyph runs
		m_cachedGlyphMap = glyphMap;
		m_pCachedGlyphMapFontFace = glyphRun->fontFace;
		m_cachedGlyphMapFontSize = glyphRun->fontEmSize;
	}
	
	// Skip if not interested in the actual glyphs
	if((flags & FW1_ANALYZEONLY) != 0)
		return S_OK;
	
	if((flags & FW1_CACHEONLY) != 0) {
		// Only request the glyphs from the provider to have them drawn to the atlas
		for(UINT i=0; i < glyphRun->glyphCount; ++i) {
			UINT atlasId = m_pGlyphProvider->GetAtlasIdFromGlyphIndex(
				glyphMap,
				glyphRun->glyphIndices[i],
				glyphRun->fontFace,
				flags
			);
			atlasId;
		}
	}
	else {
		// Glyph vertex
		FW1_GLYPHVERTEX glyphVertex;
		glyphVertex.PositionY = floor(baselineOriginY + 0.5f);
		glyphVertex.GlyphColor = m_currentColor;
		
		float positionX = floor(baselineOriginX + 0.5f);
		
		// Optional drawing effect
		if(clientDrawingEffect != NULL) {
			IFW1ColorRGBA *pColor;
			HRESULT hResult = clientDrawingEffect->QueryInterface(&pColor);
			if(SUCCEEDED(hResult)) {
				glyphVertex.GlyphColor = pColor->GetColor32();
				pColor->Release();
			}
		}
		
		// Add a vertex for each glyph in the run
		IFW1TextGeometry *pTextGeometry = static_cast<IFW1TextGeometry*>(clientDrawingContext);
		if(pTextGeometry != NULL) {
			for(UINT i=0; i < glyphRun->glyphCount; ++i) {
				glyphVertex.GlyphIndex = m_pGlyphProvider->GetAtlasIdFromGlyphIndex(
					glyphMap,
					glyphRun->glyphIndices[i],
					glyphRun->fontFace,
					flags
				);
				
				if((glyphRun->bidiLevel & 0x1) != 0)
					positionX -= glyphRun->glyphAdvances[i];
				
				glyphVertex.PositionX = floor(positionX + 0.5f);
				pTextGeometry->AddGlyphVertex(&glyphVertex);
				
				if((glyphRun->bidiLevel & 0x1) == 0)
					positionX += glyphRun->glyphAdvances[i];
			}
		}
	}
	
	return S_OK;
}


// IDWriteTextRenderer method
HRESULT CFW1TextRenderer::DrawUnderline(
	void *clientDrawingContext,
	FLOAT baselineOriginX,
	FLOAT baselineOriginY,
	const DWRITE_UNDERLINE *underline,
	IUnknown *clientDrawingEffect
) {
	clientDrawingContext;
	baselineOriginX;
	baselineOriginY;
	underline;
	clientDrawingEffect;
	
	return E_NOTIMPL;
}


// IDWriteTextRenderer method
HRESULT CFW1TextRenderer::DrawStrikethrough(
	void *clientDrawingContext,
	FLOAT baselineOriginX,
	FLOAT baselineOriginY,
	const DWRITE_STRIKETHROUGH *strikethrough,
	IUnknown *clientDrawingEffect
) {
	clientDrawingContext;
	baselineOriginX;
	baselineOriginY;
	strikethrough;
	clientDrawingEffect;
	
	return E_NOTIMPL;
}


// IDWriteTextRenderer method
HRESULT CFW1TextRenderer::DrawInlineObject(
	void *clientDrawingContext,
	FLOAT originX,
	FLOAT originY,
	IDWriteInlineObject *inlineObject,
	BOOL isSideways,
	BOOL isRightToLeft,
	IUnknown *clientDrawingEffect
) {
	clientDrawingContext;
	originX;
	originY;
	inlineObject;
	isSideways;
	isRightToLeft;
	clientDrawingEffect;
	
	return E_NOTIMPL;
}


// Get glyph provider
HRESULT STDMETHODCALLTYPE CFW1TextRenderer::GetGlyphProvider(IFW1GlyphProvider **ppGlyphProvider) {
	if(ppGlyphProvider == NULL)
		return E_INVALIDARG;
	
	m_pGlyphProvider->AddRef();
	*ppGlyphProvider = m_pGlyphProvider;
	
	return S_OK;
}


// Draw a text layout
HRESULT STDMETHODCALLTYPE CFW1TextRenderer::DrawTextLayout(
	IDWriteTextLayout *pTextLayout,
	FLOAT OriginX,
	FLOAT OriginY,
	UINT32 Color,
	UINT Flags,
	IFW1TextGeometry *pTextGeometry
) {
	m_currentFlags = Flags;
	m_currentColor = Color;
	
	m_cachedGlyphMap = 0;
	m_pCachedGlyphMapFontFace = NULL;
	m_cachedGlyphMapFontSize = 0.0f;
	
	return pTextLayout->Draw(pTextGeometry, m_pDWriteTextRendererProxy, OriginX, OriginY);
}


}// namespace FW1FontWrapper
