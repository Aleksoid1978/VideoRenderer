// CFW1Factory.h

#ifndef IncludeGuard__FW1_CFW1Factory
#define IncludeGuard__FW1_CFW1Factory


namespace FW1FontWrapper {


// Factory that creates FW1 objects
class CFW1Factory : public IFW1Factory {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		virtual ULONG STDMETHODCALLTYPE AddRef();
		virtual ULONG STDMETHODCALLTYPE Release();
		
		// IFW1Factory
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			ID3D11Device *pDevice,
			LPCWSTR pszFontFamily,
			IFW1FontWrapper **ppFontWrapper
		);
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			ID3D11Device *pDevice,
			IDWriteFactory *pDWriteFactory,
			const FW1_FONTWRAPPERCREATEPARAMS *pCreateParams,
			IFW1FontWrapper **ppFontWrapper
		);
		virtual HRESULT STDMETHODCALLTYPE CreateFontWrapper(
			ID3D11Device *pDevice,
			IFW1GlyphAtlas *pGlyphAtlas,
			IFW1GlyphProvider *pGlyphProvider,
			IFW1GlyphVertexDrawer *pGlyphVertexDrawer,
			IFW1GlyphRenderStates *pGlyphRenderStates,
			IDWriteFactory *pDWriteFactory,
			const FW1_DWRITEFONTPARAMS *pDefaultFontParams,
			IFW1FontWrapper **ppFontWrapper
		);
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphVertexDrawer(
			ID3D11Device *pDevice,
			UINT VertexBufferSize,
			IFW1GlyphVertexDrawer **ppGlyphVertexDrawer
		);
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphRenderStates(
			ID3D11Device *pDevice,
			BOOL DisableGeometryShader,
			BOOL AnisotropicFiltering,
			IFW1GlyphRenderStates **ppGlyphRenderStates
		);
		virtual HRESULT STDMETHODCALLTYPE CreateTextRenderer(
			IFW1GlyphProvider *pGlyphProvider,
			IFW1TextRenderer **ppTextRenderer
		);
		virtual HRESULT STDMETHODCALLTYPE CreateTextGeometry(
			IFW1TextGeometry **ppTextGeometry
		);
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphProvider(
			IFW1GlyphAtlas *pGlyphAtlas,
			IDWriteFactory *pDWriteFactory,
			IDWriteFontCollection *pFontCollection,
			UINT MaxGlyphWidth,
			UINT MaxGlyphHeight,
			IFW1GlyphProvider **ppGlyphProvider
		);
		virtual HRESULT STDMETHODCALLTYPE CreateDWriteRenderTarget(
			IDWriteFactory *pDWriteFactory,
			UINT RenderTargetWidth,
			UINT RenderTargetHeight,
			IFW1DWriteRenderTarget **ppRenderTarget
		);
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphAtlas(
			ID3D11Device *pDevice,
			UINT GlyphSheetWidth,
			UINT GlyphSheetHeight,
			BOOL HardwareCoordBuffer,
			BOOL AllowOversizedGlyph,
			UINT MaxGlyphCountPerSheet,
			UINT MipLevels,
			UINT MaxGlyphSheetCount,
			IFW1GlyphAtlas **ppGlyphAtlas
		);
		virtual HRESULT STDMETHODCALLTYPE CreateGlyphSheet(
			ID3D11Device *pDevice,
			UINT GlyphSheetWidth,
			UINT GlyphSheetHeight,
			BOOL HardwareCoordBuffer,
			BOOL AllowOversizedGlyph,
			UINT MaxGlyphCount,
			UINT MipLevels,
			IFW1GlyphSheet **ppGlyphSheet
		);
		virtual HRESULT STDMETHODCALLTYPE CreateColor(
			UINT32 Color,
			IFW1ColorRGBA **ppColor
		);
	
	// Public functions
	public:
		CFW1Factory();
		
		HRESULT initFactory();
	
	// Internal functions
	private:
		virtual ~CFW1Factory();
		
		HRESULT createDWriteFactory(IDWriteFactory **ppDWriteFactory);
		
		void setErrorString(const wchar_t *str);
	
	// Internal data
	private:
		ULONG						m_cRefCount;
		
		std::wstring				m_lastError;
		CRITICAL_SECTION			m_errorStringCriticalSection;
	
	private:
		CFW1Factory(const CFW1Factory&);
		CFW1Factory& operator=(const CFW1Factory&);
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1Factory
