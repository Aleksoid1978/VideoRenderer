// CFW1GlyphProvider.h

#ifndef IncludeGuard__FW1_CFW1GlyphProvider
#define IncludeGuard__FW1_CFW1GlyphProvider

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Fonts and glyphs-maps collection to match glyphs to images in a glyph-atlas
class CFW1GlyphProvider : public CFW1Object<IFW1GlyphProvider> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1GlyphProvider
		virtual HRESULT STDMETHODCALLTYPE GetGlyphAtlas(IFW1GlyphAtlas **ppGlyphAtlas);
		virtual HRESULT STDMETHODCALLTYPE GetDWriteFactory(IDWriteFactory **ppDWriteFactory);
		virtual HRESULT STDMETHODCALLTYPE GetDWriteFontCollection(IDWriteFontCollection **ppFontCollection);
		
		virtual const void* STDMETHODCALLTYPE GetGlyphMapFromFont(
			IDWriteFontFace *pFontFace,
			FLOAT FontSize,
			UINT FontFlags
		);
		virtual UINT STDMETHODCALLTYPE GetAtlasIdFromGlyphIndex(
			const void* pGlyphMap,
			UINT16 GlyphIndex,
			IDWriteFontFace *pFontFace,
			UINT FontFlags
		);
	
	// Public functions
	public:
		CFW1GlyphProvider();
		
		HRESULT initGlyphProvider(
			IFW1Factory *pFW1Factory,
			IFW1GlyphAtlas *pGlyphAtlas,
			IDWriteFactory *pDWriteFactory,
			IDWriteFontCollection *pFontCollection,
			UINT maxGlyphWidth,
			UINT maxGlyphHeight
		);
	
	// Internal types
	private:
		struct GlyphMap {
			FLOAT							fontSize;
			UINT							fontFlags;
			
			UINT							*glyphs;
			UINT							glyphCount;
		};
		
		struct FontInfo {
			IDWriteFontFace					*pFontFace;
			std::wstring					uniqueName;
		};
		
		typedef std::pair<UINT, std::pair<UINT, FLOAT> > FontId;
		typedef std::map<FontId, GlyphMap*> FontMap;
		
		FontId makeFontId(UINT fontIndex, UINT fontFlags, FLOAT fontSize) {
			UINT relevantFlags = (fontFlags & (FW1_ALIASED));
			return std::make_pair(fontIndex, std::make_pair(relevantFlags, fontSize));
		}
	
	// Internal functions
	private:
		virtual ~CFW1GlyphProvider();
		
		UINT getFontIndexFromFontFace(IDWriteFontFace *pFontFace);
		std::wstring getUniqueNameFromFontFace(IDWriteFontFace *pFontFace);
		
		UINT insertNewGlyph(GlyphMap *glyphMap, UINT16 glyphIndex, IDWriteFontFace *pFontFace);
	
	// Internal data
	private:
		IFW1GlyphAtlas						*m_pGlyphAtlas;
		
		IDWriteFactory						*m_pDWriteFactory;
		UINT								m_maxGlyphWidth;
		UINT								m_maxGlyphHeight;
		std::stack<IFW1DWriteRenderTarget*>	m_glyphRenderTargets;
		
		IDWriteFontCollection				*m_pFontCollection;
		std::vector<FontInfo>				m_fonts;
		
		FontMap								m_fontMap;
		
		CRITICAL_SECTION					m_renderTargetsCriticalSection;
		CRITICAL_SECTION					m_glyphMapsCriticalSection;
		CRITICAL_SECTION					m_fontsCriticalSection;
		CRITICAL_SECTION					m_insertGlyphCriticalSection;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1GlyphProvider
