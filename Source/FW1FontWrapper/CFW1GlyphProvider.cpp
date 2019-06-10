// CFW1GlyphProvider.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphProvider.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1GlyphProvider::CFW1GlyphProvider() :
	m_pGlyphAtlas(NULL),
	
	m_pDWriteFactory(NULL),
	m_maxGlyphWidth(0),
	m_maxGlyphHeight(0),
	
	m_pFontCollection(NULL)
{
	InitializeCriticalSection(&m_renderTargetsCriticalSection);
	InitializeCriticalSection(&m_glyphMapsCriticalSection);
	InitializeCriticalSection(&m_fontsCriticalSection);
	InitializeCriticalSection(&m_insertGlyphCriticalSection);
}


// Destruct
CFW1GlyphProvider::~CFW1GlyphProvider() {
	SAFE_RELEASE(m_pGlyphAtlas);
	
	SAFE_RELEASE(m_pDWriteFactory);
	
	while(!m_glyphRenderTargets.empty()) {
		m_glyphRenderTargets.top()->Release();
		m_glyphRenderTargets.pop();
	}
	
	SAFE_RELEASE(m_pFontCollection);
	for(size_t i=0; i < m_fonts.size(); ++i)
		SAFE_RELEASE(m_fonts[i].pFontFace);
	
	for(FontMap::iterator it = m_fontMap.begin(); it != m_fontMap.end(); ++it) {
		GlyphMap *glyphMap = (*it).second;
		
		delete[] glyphMap->glyphs;
		delete glyphMap;
	}
	
	DeleteCriticalSection(&m_renderTargetsCriticalSection);
	DeleteCriticalSection(&m_glyphMapsCriticalSection);
	DeleteCriticalSection(&m_fontsCriticalSection);
	DeleteCriticalSection(&m_insertGlyphCriticalSection);
}


// Init glyph provider
HRESULT CFW1GlyphProvider::initGlyphProvider(
	IFW1Factory *pFW1Factory,
	IFW1GlyphAtlas *pGlyphAtlas,
	IDWriteFactory *pDWriteFactory,
	IDWriteFontCollection *pFontCollection,
	UINT maxGlyphWidth,
	UINT maxGlyphHeight
) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	if(pGlyphAtlas == NULL || pDWriteFactory == NULL || pFontCollection == NULL)
		return E_INVALIDARG;
	
	pGlyphAtlas->AddRef();
	m_pGlyphAtlas = pGlyphAtlas;
	
	pDWriteFactory->AddRef();
	m_pDWriteFactory = pDWriteFactory;
	
	m_maxGlyphWidth = 384;
	if(maxGlyphWidth > 0 && maxGlyphWidth <= 8192)
		m_maxGlyphWidth = maxGlyphWidth;
	m_maxGlyphHeight = 384;
	if(maxGlyphHeight > 0 && maxGlyphHeight <= 8192)
		m_maxGlyphHeight = maxGlyphHeight;
	
	pFontCollection->AddRef();
	m_pFontCollection = pFontCollection;
	
	return S_OK;
}


// Get font index from DWrite font-face
UINT CFW1GlyphProvider::getFontIndexFromFontFace(IDWriteFontFace *pFontFace) {
	UINT fontIndex = 0xffffffff;
	
	EnterCriticalSection(&m_fontsCriticalSection);
	
	// Search for a matching fontface by pointer
	for(size_t i=0; i < m_fonts.size(); ++i) {
		const FontInfo &fontInfo = m_fonts[i];
		
		if(pFontFace == fontInfo.pFontFace) {
			fontIndex = static_cast<UINT>(i);
			break;
		}
	}
	
	LeaveCriticalSection(&m_fontsCriticalSection);
	
	// Get font-face name
	if(fontIndex == 0xffffffff) {
		std::wstring uniqueName = getUniqueNameFromFontFace(pFontFace);
		if(uniqueName.size() > 0) {
			IDWriteFontFace *pOldFontFace = NULL;
			
			pFontFace->AddRef();
			
			EnterCriticalSection(&m_fontsCriticalSection);
			
			// Search for a matching fontface by name
			for(size_t i=0; i < m_fonts.size(); ++i) {
				FontInfo &fontInfo = m_fonts[i];
				
				if(fontInfo.uniqueName == uniqueName) {
					pOldFontFace = fontInfo.pFontFace;
					fontInfo.pFontFace = pFontFace;
					fontIndex = static_cast<UINT>(i);
					break;
				}
			}
			
			// Add new font
			if(fontIndex == 0xffffffff) {
				FontInfo fontInfo;
				
				fontInfo.pFontFace = pFontFace;
				fontInfo.uniqueName = uniqueName;
				
				fontIndex = static_cast<UINT>(m_fonts.size());
				m_fonts.push_back(fontInfo);
			}
			
			LeaveCriticalSection(&m_fontsCriticalSection);
			
			SAFE_RELEASE(pOldFontFace);
		}
		else
			fontIndex = 0;
	}
	
	return fontIndex;
}


// Get unique name for a DWrite font-face
std::wstring CFW1GlyphProvider::getUniqueNameFromFontFace(IDWriteFontFace *pFontFace) {
	std::wstring uniqueName;
	
	IDWriteFont *pFont;
	HRESULT hResult = m_pFontCollection->GetFontFromFontFace(pFontFace, &pFont);
	if(SUCCEEDED(hResult)) {
		// Family name
		IDWriteFontFamily *pFontFamily;
		hResult = pFont->GetFontFamily(&pFontFamily);
		if(SUCCEEDED(hResult)) {
			IDWriteLocalizedStrings *pFamilyNames;
			hResult = pFontFamily->GetFamilyNames(&pFamilyNames);
			if(SUCCEEDED(hResult)) {
				UINT32 index;
				BOOL exists;
				hResult = pFamilyNames->FindLocaleName(L"en-us", &index, &exists);
				if(FAILED(hResult) || !exists)
					index = 0;
					
				if(pFamilyNames->GetCount() > index) {
					UINT32 length;
					hResult = pFamilyNames->GetStringLength(index, &length);
					if(SUCCEEDED(hResult)) {
						std::vector<WCHAR> str(length+1);
						
						hResult = pFamilyNames->GetString(index, &str[0], length+1);
						if(SUCCEEDED(hResult))
							uniqueName = &str[0];
					}
				}
				
				pFamilyNames->Release();
			}
			
			pFontFamily->Release();
		}
		
		// Face name
		IDWriteLocalizedStrings *pFaceNames;
		hResult = pFont->GetFaceNames(&pFaceNames);
		if(SUCCEEDED(hResult)) {
			UINT32 index;
			BOOL exists;
			hResult = pFaceNames->FindLocaleName(L"en-us", &index, &exists);
			if(FAILED(hResult) || !exists)
				index = 0;
					
			if(pFaceNames->GetCount() > index) {
				UINT32 length;
				hResult = pFaceNames->GetStringLength(index, &length);
				if(SUCCEEDED(hResult)) {
					std::vector<WCHAR> str(length+1);
						
					hResult = pFaceNames->GetString(index, &str[0], length+1);
					if(SUCCEEDED(hResult))
						uniqueName.append(&str[0]);
				}
			}
				
			pFaceNames->Release();
		}
		
		// Simulations
		if(uniqueName.size() > 0) {
			DWRITE_FONT_SIMULATIONS simulations = pFontFace->GetSimulations();
			if(simulations == DWRITE_FONT_SIMULATIONS_BOLD)
				uniqueName += L"SimBold";
			else if(simulations == DWRITE_FONT_SIMULATIONS_OBLIQUE)
				uniqueName += L"SimOblique";
		}
	}
	
	return uniqueName;
}


// Render and insert new glyph into a glyph-map
UINT CFW1GlyphProvider::insertNewGlyph(GlyphMap *glyphMap, UINT16 glyphIndex, IDWriteFontFace *pFontFace) {
	UINT glyphAtlasId = 0xffffffff;
	
	// Get a render target
	IFW1DWriteRenderTarget *pRenderTarget = NULL;
	
	EnterCriticalSection(&m_renderTargetsCriticalSection);
	
	if(!m_glyphRenderTargets.empty()) {
		pRenderTarget = m_glyphRenderTargets.top();
		m_glyphRenderTargets.pop();
	}
	
	LeaveCriticalSection(&m_renderTargetsCriticalSection);
	
	if(pRenderTarget == NULL) {
		IFW1DWriteRenderTarget *pNewRenderTarget;
		HRESULT hResult = m_pFW1Factory->CreateDWriteRenderTarget(
			m_pDWriteFactory,
			m_maxGlyphWidth,
			m_maxGlyphHeight,
			&pNewRenderTarget
		);
		if(FAILED(hResult)) {
		}
		else {
			pRenderTarget = pNewRenderTarget;
		}
	}
	
	if(pRenderTarget != NULL) {
		// Draw the glyph image
		DWRITE_RENDERING_MODE renderingMode = DWRITE_RENDERING_MODE_DEFAULT;
		DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL;
		if((glyphMap->fontFlags & FW1_ALIASED) != 0) {
			renderingMode = DWRITE_RENDERING_MODE_ALIASED;
			measuringMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
		}
		
		FW1_GLYPHIMAGEDATA glyphData;
		HRESULT hResult = pRenderTarget->DrawGlyphTemp(
			pFontFace,
			glyphIndex,
			glyphMap->fontSize,
			renderingMode,
			measuringMode,
			&glyphData
		);
		if(FAILED(hResult)) {
		}
		else {
			// Insert into the atlas and the glyph-map
			EnterCriticalSection(&m_insertGlyphCriticalSection);
			
			glyphAtlasId = glyphMap->glyphs[glyphIndex];
			if(glyphAtlasId == 0xffffffff) {
				glyphAtlasId = m_pGlyphAtlas->InsertGlyph(
					&glyphData.Metrics,
					glyphData.pGlyphPixels,
					glyphData.RowPitch,
					glyphData.PixelStride
				);
				if(glyphAtlasId != 0xffffffff)
					glyphMap->glyphs[glyphIndex] = glyphAtlasId;
			}
			
			LeaveCriticalSection(&m_insertGlyphCriticalSection);
		}
		
		// Keep the render target for future use
		EnterCriticalSection(&m_renderTargetsCriticalSection);
		m_glyphRenderTargets.push(pRenderTarget);
		LeaveCriticalSection(&m_renderTargetsCriticalSection);
	}
	
	return glyphAtlasId;
}


}// namespace FW1FontWrapper
