// CFW1GlyphRenderStates.h

#ifndef IncludeGuard__FW1_CFW1GlyphRenderStates
#define IncludeGuard__FW1_CFW1GlyphRenderStates

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Shader etc. needed to draw glyphs
class CFW1GlyphRenderStates : public CFW1Object<IFW1GlyphRenderStates> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1GlyphRenderStates
		virtual HRESULT STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice);
		
		virtual void STDMETHODCALLTYPE SetStates(ID3D11DeviceContext *pContext, UINT Flags);
		virtual void STDMETHODCALLTYPE UpdateShaderConstants(
			ID3D11DeviceContext *pContext,
			const FW1_RECTF *pClipRect,
			const FLOAT *pTransformMatrix
		);
		virtual BOOL STDMETHODCALLTYPE HasGeometryShader();
	
	// Public functions
	public:
		CFW1GlyphRenderStates();
		
		HRESULT initRenderResources(
			IFW1Factory *pFW1Factory,
			ID3D11Device *pDevice,
			bool wantGeometryShader,
			bool anisotropicFiltering
		);
	
	// Internal types
	private:
		struct ShaderConstants {
			FLOAT					TransformMatrix[16];
			FLOAT					ClipRect[4];
		};
	
	// Internal functions
	private:
		virtual ~CFW1GlyphRenderStates();
		
		HRESULT createQuadShaders();
		HRESULT createGlyphShaders();
		HRESULT createPixelShaders();
		HRESULT createConstantBuffer();
		HRESULT createRenderStates(bool anisotropicFiltering);
	
	// Internal data
	private:
		std::wstring				m_lastError;
		
		pD3DCompile					m_pfnD3DCompile;
		
		ID3D11Device				*m_pDevice;
		D3D_FEATURE_LEVEL			m_featureLevel;
		
		ID3D11VertexShader			*m_pVertexShaderQuad;
		ID3D11VertexShader			*m_pVertexShaderClipQuad;
		ID3D11InputLayout			*m_pQuadInputLayout;
		
		ID3D11VertexShader			*m_pVertexShaderPoint;
		ID3D11InputLayout			*m_pPointInputLayout;
		ID3D11GeometryShader		*m_pGeometryShaderPoint;
		ID3D11GeometryShader		*m_pGeometryShaderClipPoint;
		bool						m_hasGeometryShader;
		
		ID3D11PixelShader			*m_pPixelShader;
		ID3D11PixelShader			*m_pPixelShaderClip;
		
		ID3D11Buffer				*m_pConstantBuffer;
		
		ID3D11BlendState			*m_pBlendState;
		ID3D11SamplerState			*m_pSamplerState;
		ID3D11RasterizerState		*m_pRasterizerState;
		ID3D11DepthStencilState		*m_pDepthStencilState;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1GlyphRenderStates
