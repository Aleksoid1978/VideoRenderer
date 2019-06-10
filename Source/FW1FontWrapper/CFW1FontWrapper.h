// CFW1FontWrapper.h

#ifndef IncludeGuard__FW1_CFW1FontWrapper
#define IncludeGuard__FW1_CFW1FontWrapper

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Font-wrapper simplifying drawing strings and text-layouts
class CFW1FontWrapper : public CFW1Object<IFW1FontWrapper> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1FontWrapper
		virtual HRESULT STDMETHODCALLTYPE GetFactory(IFW1Factory **ppFactory);
		
		virtual HRESULT STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice);
		virtual HRESULT STDMETHODCALLTYPE GetDWriteFactory(IDWriteFactory **ppDWriteFactory);
		virtual HRESULT STDMETHODCALLTYPE GetGlyphAtlas(IFW1GlyphAtlas **ppGlyphAtlas);
		virtual HRESULT STDMETHODCALLTYPE GetGlyphProvider(IFW1GlyphProvider **ppGlyphProvider);
		virtual HRESULT STDMETHODCALLTYPE GetRenderStates(IFW1GlyphRenderStates **ppRenderStates);
		virtual HRESULT STDMETHODCALLTYPE GetVertexDrawer(IFW1GlyphVertexDrawer **ppVertexDrawer);
		
		virtual void STDMETHODCALLTYPE DrawTextLayout(
			ID3D11DeviceContext *pContext,
			IDWriteTextLayout *pTextLayout,
			FLOAT OriginX,
			FLOAT OriginY,
			UINT32 Color,
			UINT Flags
		);
		virtual void STDMETHODCALLTYPE DrawTextLayout(
			ID3D11DeviceContext *pContext,
			IDWriteTextLayout *pTextLayout,
			FLOAT OriginX,
			FLOAT OriginY,
			UINT32 Color,
			const FW1_RECTF *pClipRect,
			const FLOAT *pTransformMatrix,
			UINT Flags
		);
		
		virtual void STDMETHODCALLTYPE DrawString(
			ID3D11DeviceContext *pContext,
			const WCHAR *pszString,
			FLOAT FontSize,
			FLOAT X,
			FLOAT Y,
			UINT32 Color,
			UINT Flags
		);
		virtual void STDMETHODCALLTYPE DrawString(
			ID3D11DeviceContext *pContext,
			const WCHAR *pszString,
			const WCHAR *pszFontFamily,
			FLOAT FontSize,
			FLOAT X,
			FLOAT Y,
			UINT32 Color,
			UINT Flags
		);
		virtual void STDMETHODCALLTYPE DrawString(
			ID3D11DeviceContext *pContext,
			const WCHAR *pszString,
			const WCHAR *pszFontFamily,
			FLOAT FontSize,
			const FW1_RECTF *pLayoutRect,
			UINT32 Color,
			const FW1_RECTF *pClipRect,
			const FLOAT *pTransformMatrix,
			UINT Flags
		);
		
		virtual FW1_RECTF STDMETHODCALLTYPE MeasureString(
			const WCHAR *pszString,
			const WCHAR *pszFontFamily,
			FLOAT FontSize,
			const FW1_RECTF *pLayoutRect,
			UINT Flags
		);
		
		virtual void STDMETHODCALLTYPE AnalyzeString(
			ID3D11DeviceContext *pContext,
			const WCHAR *pszString,
			const WCHAR *pszFontFamily,
			FLOAT FontSize,
			const FW1_RECTF *pLayoutRect,
			UINT32 Color,
			UINT Flags,
			IFW1TextGeometry *pTextGeometry
		);
		
		virtual void STDMETHODCALLTYPE AnalyzeTextLayout(
			ID3D11DeviceContext *pContext,
			IDWriteTextLayout *pTextLayout,
			FLOAT OriginX,
			FLOAT OriginY,
			UINT32 Color,
			UINT Flags,
			IFW1TextGeometry *pTextGeometry
		);
		
		virtual void STDMETHODCALLTYPE DrawGeometry(
			ID3D11DeviceContext *pContext,
			IFW1TextGeometry *pGeometry,
			const FW1_RECTF *pClipRect,
			const FLOAT *pTransformMatrix,
			UINT Flags
		);
		
		virtual void STDMETHODCALLTYPE Flush(ID3D11DeviceContext *pContext);
	
	// Public functions
	public:
		CFW1FontWrapper();
		
		HRESULT initFontWrapper(
			IFW1Factory *pFW1Factory,
			ID3D11Device *pDevice,
			IFW1GlyphAtlas *pGlyphAtlas,
			IFW1GlyphProvider *pGlyphProvider,
			IFW1GlyphVertexDrawer *pGlyphVertexDrawer,
			IFW1GlyphRenderStates *pGlyphRenderStates,
			IDWriteFactory *pDWriteFactory,
			const FW1_DWRITEFONTPARAMS *pDefaultFontParams
		);
	
	// Internal functions
	private:
		virtual ~CFW1FontWrapper();
		
		IDWriteTextLayout* createTextLayout(
			const WCHAR *pszString,
			const WCHAR *pszFontFamily,
			FLOAT fontSize,
			const FW1_RECTF *pLayoutRect,
			UINT flags
		);
	
	// Internal data
	private:
		std::wstring					m_lastError;
		
		ID3D11Device					*m_pDevice;
		D3D_FEATURE_LEVEL				m_featureLevel;
		IDWriteFactory					*m_pDWriteFactory;
		
		IFW1GlyphAtlas					*m_pGlyphAtlas;
		IFW1GlyphProvider				*m_pGlyphProvider;
		
		IFW1GlyphRenderStates			*m_pGlyphRenderStates;
		IFW1GlyphVertexDrawer			*m_pGlyphVertexDrawer;
		
		CRITICAL_SECTION				m_textRenderersCriticalSection;
		std::stack<IFW1TextRenderer*>	m_textRenderers;
		CRITICAL_SECTION				m_textGeometriesCriticalSection;
		std::stack<IFW1TextGeometry*>	m_textGeometries;
		
		bool							m_defaultTextInited;
		IDWriteTextFormat				*m_pDefaultTextFormat;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1FontWrapper
