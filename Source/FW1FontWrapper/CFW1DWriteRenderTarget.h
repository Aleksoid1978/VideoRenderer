// CFW1DWriteRenderTarget.h

#ifndef IncludeGuard__FW1_CFW1DWriteRenderTarget
#define IncludeGuard__FW1_CFW1DWriteRenderTarget

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Render target that provides pixels of one glyph-image at a time
class CFW1DWriteRenderTarget : public CFW1Object<IFW1DWriteRenderTarget> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1DWriteRenderTarget
		virtual HRESULT STDMETHODCALLTYPE DrawGlyphTemp(
			IDWriteFontFace *pFontFace,
			UINT16 GlyphIndex,
			FLOAT FontSize,
			DWRITE_RENDERING_MODE RenderingMode,
			DWRITE_MEASURING_MODE MeasuringMode,
			FW1_GLYPHIMAGEDATA *pOutData
		);
	
	// Public functions
	public:
		CFW1DWriteRenderTarget();
		
		HRESULT initRenderTarget(
			IFW1Factory *pFW1Factory,
			IDWriteFactory *pDWriteFactory,
			UINT renderTargetWidth,
			UINT renderTargetHeight
		);
	
	// Internal types
	private:
		struct DWGlyphData {
			FLOAT					offsetX;
			FLOAT					offsetY;
			LONG					maxWidth;
			LONG					maxHeight;
		};
		
		typedef std::map<DWRITE_RENDERING_MODE, IDWriteRenderingParams*> RenderingParamsMap;
	
	// Internal functions
	private:
		virtual ~CFW1DWriteRenderTarget();
		
		HRESULT createRenderTarget(IDWriteFactory *pDWriteFactory);
		
		void initGlyphData(
			const DWRITE_FONT_METRICS *fontMetrics,
			const DWRITE_GLYPH_METRICS *glyphMetrics,
			FLOAT fontSize,
			DWGlyphData *outGlyphData
		);
	
	// Internal data
	private:
		std::wstring				m_lastError;
		
		IDWriteBitmapRenderTarget	*m_pRenderTarget;
		HDC							m_hDC;
		HBRUSH						m_hBlackBrush;
		LPVOID						m_bmBits;
		UINT						m_bmWidthBytes;
		UINT						m_bmBytesPixel;
		UINT						m_renderTargetWidth;
		UINT						m_renderTargetHeight;
		RenderingParamsMap			m_renderingParams;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1DWriteRenderTarget
