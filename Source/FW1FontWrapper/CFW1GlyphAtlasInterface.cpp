// CFW1GlyphAtlasInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphAtlas.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1GlyphAtlas::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1GlyphAtlas))) {
		*ppvObject = static_cast<IFW1GlyphAtlas*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Get the D3D11 device used by this atlas
HRESULT STDMETHODCALLTYPE CFW1GlyphAtlas::GetDevice(ID3D11Device **ppDevice) {
	if(ppDevice == NULL)
		return E_INVALIDARG;
	
	m_pDevice->AddRef();
	*ppDevice = m_pDevice;
	
	return S_OK;
}


// Get total glyph count in atlas
UINT STDMETHODCALLTYPE CFW1GlyphAtlas::GetTotalGlyphCount() {
	UINT total = 0;
	
	for(UINT i=0; i < m_sheetCount; ++i) {
		FW1_GLYPHSHEETDESC desc;
		m_glyphSheets[i]->GetDesc(&desc);
		
		total += desc.GlyphCount;
	}
	
	return total;
}


// Get sheet count
UINT STDMETHODCALLTYPE CFW1GlyphAtlas::GetSheetCount() {
	return m_sheetCount;
}


// Get sheet
HRESULT STDMETHODCALLTYPE CFW1GlyphAtlas::GetSheet(UINT SheetIndex, IFW1GlyphSheet **ppGlyphSheet) {
	if(ppGlyphSheet == NULL)
		return E_INVALIDARG;
	
	if(SheetIndex < m_sheetCount) {
		*ppGlyphSheet = m_glyphSheets[SheetIndex];
		
		return S_OK;
	}
	
	*ppGlyphSheet = NULL;
	
	return E_INVALIDARG;
}

// Get texture coordinates
const FW1_GLYPHCOORDS* STDMETHODCALLTYPE CFW1GlyphAtlas::GetGlyphCoords(UINT SheetIndex) {
	if(SheetIndex < m_sheetCount)
		return m_glyphSheets[SheetIndex]->GetGlyphCoords();
	
	return 0;
}


// Set sheet shader resources
HRESULT STDMETHODCALLTYPE CFW1GlyphAtlas::BindSheet(ID3D11DeviceContext *pContext, UINT SheetIndex, UINT Flags) {
	if(SheetIndex < m_sheetCount)
		return m_glyphSheets[SheetIndex]->BindSheet(pContext, Flags);
	
	return E_INVALIDARG;
}


// Insert texture into atlas
UINT STDMETHODCALLTYPE CFW1GlyphAtlas::InsertGlyph(
	const FW1_GLYPHMETRICS *pGlyphMetrics,
	const void *pGlyphData,
	UINT RowPitch,
	UINT PixelStride
) {
	UINT glyphIndex = 0xffffffff;
	UINT sheetIndex = 0;
	
	// Get open sheet range
	EnterCriticalSection(&m_glyphSheetsCriticalSection);
	UINT start = m_currentSheetIndex;
	UINT end = m_sheetCount;
	LeaveCriticalSection(&m_glyphSheetsCriticalSection);
	
	// Attempt to insert glyph
	for(UINT i=start; i < end; ++i) {
		IFW1GlyphSheet *pGlyphSheet = m_glyphSheets[i];
		
		glyphIndex = pGlyphSheet->InsertGlyph(pGlyphMetrics, pGlyphData, RowPitch, PixelStride);
		if(glyphIndex != 0xffffffff) {
			sheetIndex = i;
			break;
		}
	}
	
	// Try to create a new glyph sheet on failure
	if(glyphIndex == 0xffffffff && m_sheetCount < m_maxSheetCount) {
		IFW1GlyphSheet *pGlyphSheet;
		if(SUCCEEDED(createGlyphSheet(&pGlyphSheet))) {
			glyphIndex = pGlyphSheet->InsertGlyph(pGlyphMetrics, pGlyphData, RowPitch, PixelStride);
			
			UINT newSheetIndex = InsertSheet(pGlyphSheet);
			if(newSheetIndex != 0xffffffff)
				sheetIndex = newSheetIndex;
			else
				glyphIndex = 0xffffffff;
			
			pGlyphSheet->Release();
		}
	}
	
	if(glyphIndex == 0xffffffff)
		return 0xffffffff;
	
	return (sheetIndex << 16) | glyphIndex;
}


// Insert glyph sheets
UINT STDMETHODCALLTYPE CFW1GlyphAtlas::InsertSheet(IFW1GlyphSheet *pGlyphSheet) {
	if(pGlyphSheet == NULL)
		return 0xffffffff;
	
	UINT sheetIndex = 0xffffffff;
	
	EnterCriticalSection(&m_glyphSheetsCriticalSection);
	if(m_sheetCount < m_maxSheetCount) {
		pGlyphSheet->AddRef();
		
		sheetIndex = m_sheetCount;
		
		m_glyphSheets[sheetIndex] = pGlyphSheet;
		
		_WriteBarrier();
		MemoryBarrier();
		
		++m_sheetCount;
		
		// Restrict the number of open sheets
		UINT numActiveSheets = 4;
		
		if(m_sheetCount > m_currentSheetIndex + numActiveSheets) {
			m_glyphSheets[m_currentSheetIndex]->CloseSheet();
			
			++m_currentSheetIndex;
		}
	}
	LeaveCriticalSection(&m_glyphSheetsCriticalSection);
	
	return sheetIndex;
}


// Flush all sheets with possible new glyphs
void STDMETHODCALLTYPE CFW1GlyphAtlas::Flush(ID3D11DeviceContext *pContext) {
	UINT first = 0;
	UINT end = 0;
	
	EnterCriticalSection(&m_glyphSheetsCriticalSection);
	
	first = m_flushedSheetIndex;
	end = m_sheetCount;
	
	m_flushedSheetIndex = m_currentSheetIndex;
	
	LeaveCriticalSection(&m_glyphSheetsCriticalSection);
	
	for(UINT i=first; i < end; ++i)
		m_glyphSheets[i]->Flush(pContext);
}


}// namespace FW1FontWrapper
