// CFW1GlyphAtlas.h

#ifndef IncludeGuard__FW1_CFW1GlyphAtlas
#define IncludeGuard__FW1_CFW1GlyphAtlas

#include "CFW1Object.h"


namespace FW1FontWrapper {


class CFW1GlyphAtlas : public CFW1Object<IFW1GlyphAtlas> {
	// Interface
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1GlyphAtlas
		virtual HRESULT STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice);
		
		virtual UINT STDMETHODCALLTYPE GetTotalGlyphCount();
		virtual UINT STDMETHODCALLTYPE GetSheetCount();
		
		virtual HRESULT STDMETHODCALLTYPE GetSheet(UINT SheetIndex, IFW1GlyphSheet **ppGlyphSheet);
		
		virtual const FW1_GLYPHCOORDS* STDMETHODCALLTYPE GetGlyphCoords(UINT SheetIndex);
		virtual HRESULT STDMETHODCALLTYPE BindSheet(ID3D11DeviceContext *pContext, UINT SheetIndex, UINT Flags);
		
		virtual UINT STDMETHODCALLTYPE InsertGlyph(
			const FW1_GLYPHMETRICS *pGlyphMetrics,
			const void *pGlyphData,
			UINT RowPitch,
			UINT PixelStride
		);
		virtual UINT STDMETHODCALLTYPE InsertSheet(IFW1GlyphSheet *pGlyphSheet);
		virtual void STDMETHODCALLTYPE Flush(ID3D11DeviceContext *pContext);
	
	// Public functions
	public:
		CFW1GlyphAtlas();
		
		HRESULT initGlyphAtlas(
			IFW1Factory *pFW1Factory,
			ID3D11Device *pDevice,
			UINT sheetWidth,
			UINT sheetHeight,
			bool coordBuffer,
			bool allowOversizedTexture,
			UINT maxGlyphCountPerSheet,
			UINT mipLevelCount,
			UINT maxSheetCount
		);
	
	// Internal functions
	private:
		virtual ~CFW1GlyphAtlas();
		
		HRESULT createGlyphSheet(IFW1GlyphSheet **ppGlyphSheet);
	
	// Internal data
	private:
		ID3D11Device				*m_pDevice;
		UINT						m_sheetWidth;
		UINT						m_sheetHeight;
		bool						m_hardwareCoordBuffer;
		bool						m_allowOversizedGlyph;
		UINT						m_maxGlyphCount;
		UINT						m_mipLevelCount;
		
		IFW1GlyphSheet				**m_glyphSheets;
		UINT						m_sheetCount;
		UINT						m_maxSheetCount;
		UINT						m_currentSheetIndex;
		UINT						m_flushedSheetIndex;
		
		CRITICAL_SECTION			m_glyphSheetsCriticalSection;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1GlyphAtlas
