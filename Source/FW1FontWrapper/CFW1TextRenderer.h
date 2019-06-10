// CFW1TextRenderer.h

#ifndef IncludeGuard__FW1_CFW1TextRenderer
#define IncludeGuard__FW1_CFW1TextRenderer

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Converts a DWrite text layout to vertices
class CFW1TextRenderer : public CFW1Object<IFW1TextRenderer> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1DWriteTextRenderer
		virtual HRESULT STDMETHODCALLTYPE GetGlyphProvider(IFW1GlyphProvider **ppGlyphProvider);
		
		virtual HRESULT STDMETHODCALLTYPE DrawTextLayout(
			IDWriteTextLayout *pTextLayout,
			FLOAT OriginX,
			FLOAT OriginY,
			UINT32 Color,
			UINT Flags,
			IFW1TextGeometry *pTextGeometry
		);
	
	// Public functions
	public:
		CFW1TextRenderer();
		
		HRESULT initTextRenderer(
			IFW1Factory *pFW1Factory,
			IFW1GlyphProvider *pGlyphProvider
		);
	
	// Internal functions
	private:
		virtual ~CFW1TextRenderer();
		
		// IDWritePixelSnapping interface (called via proxy)
		HRESULT IsPixelSnappingDisabled(void *clientDrawingContext, BOOL *isDisabled);
		HRESULT GetCurrentTransform(void *clientDrawingContext, DWRITE_MATRIX *transform);
		HRESULT GetPixelsPerDip(void *clientDrawingContext, FLOAT *pixelsPerDip);
		
		// IDWriteTextRenderer interface (called via proxy)
		HRESULT DrawGlyphRun(
			void *clientDrawingContext,
			FLOAT baselineOriginX,
			FLOAT baselineOriginY,
			DWRITE_MEASURING_MODE measuringMode,
			const DWRITE_GLYPH_RUN *glyphRun,
			const DWRITE_GLYPH_RUN_DESCRIPTION *glyphRunDescription,
			IUnknown *clientDrawingEffect
		);
		HRESULT DrawUnderline(
			void *clientDrawingContext,
			FLOAT baselineOriginX,
			FLOAT baselineOriginY,
			const DWRITE_UNDERLINE *underline,
			IUnknown *clientDrawingEffect
		);
		HRESULT DrawStrikethrough(
			void *clientDrawingContext,
			FLOAT baselineOriginX,
			FLOAT baselineOriginY,
			const DWRITE_STRIKETHROUGH *strikethrough,
			IUnknown *clientDrawingEffect
		);
		HRESULT DrawInlineObject(
			void *clientDrawingContext,
			FLOAT originX,
			FLOAT originY,
			IDWriteInlineObject *inlineObject,
			BOOL isSideways,
			BOOL isRightToLeft,
			IUnknown *clientDrawingEffect
		);
	
	// Internal data
	private:
		IFW1GlyphProvider			*m_pGlyphProvider;
		
		UINT						m_currentFlags;
		UINT32						m_currentColor;
		
		const void					*m_cachedGlyphMap;
		IDWriteFontFace				*m_pCachedGlyphMapFontFace;
		FLOAT						m_cachedGlyphMapFontSize;
	
	
	// Proxy for IDWriteTextRenderer interface
	private:
		class CDWriteTextRendererProxy : public IDWriteTextRenderer {
			public:
				virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
					return m_realObject->QueryInterface(riid, ppvObject);
				}
				virtual ULONG STDMETHODCALLTYPE AddRef() {
					return m_realObject->AddRef();
				}
				virtual ULONG STDMETHODCALLTYPE Release() {
					return m_realObject->Release();
				}
				
				virtual HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *clientDrawingContext, BOOL *isDisabled) {
					return m_realObject->IsPixelSnappingDisabled(clientDrawingContext, isDisabled);
				}
				virtual HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *clientDrawingContext, DWRITE_MATRIX *transform) {
					return m_realObject->GetCurrentTransform(clientDrawingContext, transform);
				}
				virtual HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *clientDrawingContext, FLOAT *pixelsPerDip) {
					return m_realObject->GetPixelsPerDip(clientDrawingContext, pixelsPerDip);
				}
				
				virtual HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN *glyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION *glyphRunDescription, IUnknown *clientDrawingEffect) {
					return m_realObject->DrawGlyphRun(clientDrawingContext, baselineOriginX, baselineOriginY, measuringMode, glyphRun, glyphRunDescription, clientDrawingEffect);
				}
				virtual HRESULT STDMETHODCALLTYPE DrawUnderline(void *clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, const DWRITE_UNDERLINE *underline, IUnknown *clientDrawingEffect) {
					return m_realObject->DrawUnderline(clientDrawingContext, baselineOriginX, baselineOriginY, underline, clientDrawingEffect);
				}
				virtual HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY, const DWRITE_STRIKETHROUGH *strikethrough, IUnknown *clientDrawingEffect) {
					return m_realObject->DrawStrikethrough(clientDrawingContext, baselineOriginX, baselineOriginY, strikethrough, clientDrawingEffect);
				}
				virtual HRESULT STDMETHODCALLTYPE DrawInlineObject(void *clientDrawingContext, FLOAT originX, FLOAT originY, IDWriteInlineObject *inlineObject, BOOL isSideways, BOOL isRightToLeft, IUnknown *clientDrawingEffect) {
					return m_realObject->DrawInlineObject(clientDrawingContext, originX, originY, inlineObject, isSideways, isRightToLeft, clientDrawingEffect);
				}
			
			public:
				CDWriteTextRendererProxy(CFW1TextRenderer *realObject) : m_realObject(realObject) {}
			
			private:
				CDWriteTextRendererProxy(const CDWriteTextRendererProxy&);
				CDWriteTextRendererProxy& operator=(const CDWriteTextRendererProxy&);
			
			private:
				CFW1TextRenderer	*m_realObject;
		} *m_pDWriteTextRendererProxy;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1TextRenderer
