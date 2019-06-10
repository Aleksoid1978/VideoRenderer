// CFW1GlyphProviderInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphProvider.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1GlyphProvider::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1GlyphProvider))) {
		*ppvObject = static_cast<IFW1GlyphProvider*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Get glyph atlas
HRESULT STDMETHODCALLTYPE CFW1GlyphProvider::GetGlyphAtlas(IFW1GlyphAtlas **ppGlyphAtlas) {
	if(ppGlyphAtlas == NULL)
		return E_INVALIDARG;
	
	m_pGlyphAtlas->AddRef();
	*ppGlyphAtlas = m_pGlyphAtlas;
	
	return S_OK;
}


// Get DWrite factory
HRESULT STDMETHODCALLTYPE CFW1GlyphProvider::GetDWriteFactory(IDWriteFactory **ppDWriteFactory) {
	if(ppDWriteFactory == NULL)
		return E_INVALIDARG;
	
	m_pDWriteFactory->AddRef();
	*ppDWriteFactory = m_pDWriteFactory;
	
	return S_OK;
}


// Get DWrite font collection
HRESULT STDMETHODCALLTYPE CFW1GlyphProvider::GetDWriteFontCollection(IDWriteFontCollection **ppFontCollection) {
	if(ppFontCollection == NULL)
		return E_INVALIDARG;
	
	m_pFontCollection->AddRef();
	*ppFontCollection = m_pFontCollection;
	
	return S_OK;
}


// Get glyph map
const void* STDMETHODCALLTYPE CFW1GlyphProvider::GetGlyphMapFromFont(
	IDWriteFontFace *pFontFace,
	FLOAT FontSize,
	UINT FontFlags
) {
	// Get font id
	UINT fontIndex = getFontIndexFromFontFace(pFontFace);
	FontId fontId = makeFontId(fontIndex, FontFlags, FontSize);
	
	const void *glyphMap = 0;
	
	// Get the glyph-map
	EnterCriticalSection(&m_glyphMapsCriticalSection);
	FontMap::iterator it = m_fontMap.find(fontId);
	if(it != m_fontMap.end())
		glyphMap = (*it).second;
	LeaveCriticalSection(&m_glyphMapsCriticalSection);
	
	if(glyphMap == 0 && (FontFlags & FW1_NONEWGLYPHS) == 0) {
		// Create a new glyph-map
		GlyphMap *newGlyphMap = new GlyphMap;
		newGlyphMap->fontSize = FontSize;
		newGlyphMap->fontFlags = FontFlags;
		newGlyphMap->glyphCount = pFontFace->GetGlyphCount();
		newGlyphMap->glyphs = new UINT[newGlyphMap->glyphCount];
		for(UINT i=0; i < newGlyphMap->glyphCount; ++i)
			newGlyphMap->glyphs[i] = 0xffffffff;
		
		bool needless = false;
		
		// Inert the new glyph-map and map the font-id to its index
		EnterCriticalSection(&m_glyphMapsCriticalSection);
		
		it = m_fontMap.find(fontId);
		if(it != m_fontMap.end()) {
			glyphMap = (*it).second;
			needless = true;
		}
		else {
			m_fontMap.insert(std::make_pair(fontId, newGlyphMap));
			glyphMap = newGlyphMap;
		}
		
		LeaveCriticalSection(&m_glyphMapsCriticalSection);
		
		if(needless) {// Simultaneous creation on two threads
			delete[] newGlyphMap->glyphs;
			delete newGlyphMap;
		}
		else {
			UINT glyphAtlasId = insertNewGlyph(newGlyphMap, 0, pFontFace);
			glyphAtlasId;
		}
	}
	
	return glyphMap;
}


// Get atlas id of a glyph
UINT STDMETHODCALLTYPE CFW1GlyphProvider::GetAtlasIdFromGlyphIndex(
	const void *pGlyphMap,
	UINT16 GlyphIndex,
	IDWriteFontFace *pFontFace,
	UINT FontFlags
) {
	GlyphMap *glyphMap = static_cast<GlyphMap*>(const_cast<void*>(pGlyphMap));
	
	if(glyphMap == 0)
		return 0;
	
	if(GlyphIndex >= glyphMap->glyphCount)
		return 0;
	
	// Get the atlas id for this glyph
	UINT glyphAtlasId = glyphMap->glyphs[GlyphIndex];
	if(glyphAtlasId == 0xffffffff && (FontFlags & FW1_NONEWGLYPHS) == 0)
		glyphAtlasId = insertNewGlyph(glyphMap, GlyphIndex, pFontFace);
	
	// Fall back to the font default-glyph or the atlas default-glyph on failure
	if(glyphAtlasId == 0xffffffff) {
		glyphAtlasId = glyphMap->glyphs[0];
		
		if((FontFlags & FW1_NONEWGLYPHS) == 0) {
			if(glyphAtlasId == 0xffffffff) {
				if(GlyphIndex == 0)
					glyphAtlasId = 0;
				else
					glyphAtlasId = GetAtlasIdFromGlyphIndex(pGlyphMap, 0, pFontFace, FontFlags);
			}
			
			EnterCriticalSection(&m_insertGlyphCriticalSection);
			if(glyphMap->glyphs[GlyphIndex] == 0xffffffff)
				glyphMap->glyphs[GlyphIndex] = glyphAtlasId;
			LeaveCriticalSection(&m_insertGlyphCriticalSection);
		}
		
		if(glyphAtlasId == 0xffffffff)
			glyphAtlasId = 0;
	}
	
	return glyphAtlasId;
}


}// namespace FW1FontWrapper
