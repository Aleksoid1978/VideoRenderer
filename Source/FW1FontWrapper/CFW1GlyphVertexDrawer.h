// CFW1GlyphVertexDrawer.h

#ifndef IncludeGuard__FW1_CFW1GlyphVertexDrawer
#define IncludeGuard__FW1_CFW1GlyphVertexDrawer

#include "CFW1Object.h"


namespace FW1FontWrapper {


// Draws glyph-vertices from system memory using a dynamic vertex buffer
class CFW1GlyphVertexDrawer : public CFW1Object<IFW1GlyphVertexDrawer> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1GlyphVertexDrawer
		virtual HRESULT STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice);
		
		virtual UINT STDMETHODCALLTYPE DrawVertices(
			ID3D11DeviceContext *pContext,
			IFW1GlyphAtlas *pGlyphAtlas,
			const FW1_VERTEXDATA *pVertexData,
			UINT Flags,
			UINT PreboundSheet
		);
	
	// Public functions
	public:
		CFW1GlyphVertexDrawer();
		
		HRESULT initVertexDrawer(IFW1Factory *pFW1Factory, ID3D11Device *pDevice, UINT vertexBufferSize);
	
	// Internal types
	private:
		struct QuadVertex {
			FLOAT						positionX;
			FLOAT						positionY;
			FLOAT						texCoordX;
			FLOAT						texCoordY;
			UINT32						color;
		};
	
	// Internal functions
	private:
		virtual ~CFW1GlyphVertexDrawer();
		
		HRESULT createBuffers();
		
		UINT drawVertices(
			ID3D11DeviceContext *pContext,
			IFW1GlyphAtlas *pGlyphAtlas,
			const FW1_VERTEXDATA *vertexData,
			UINT preboundSheet
		);
		UINT drawGlyphsAsQuads(
			ID3D11DeviceContext *pContext,
			IFW1GlyphAtlas *pGlyphAtlas,
			const FW1_VERTEXDATA *vertexData,
			UINT preboundSheet
		);
	
	// Internal data
	private:
		std::wstring					m_lastError;
		
		ID3D11Device					*m_pDevice;
		
		ID3D11Buffer					*m_pVertexBuffer;
		ID3D11Buffer					*m_pIndexBuffer;
		UINT							m_vertexBufferSize;
		UINT							m_maxIndexCount;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1GlyphVertexDrawer
