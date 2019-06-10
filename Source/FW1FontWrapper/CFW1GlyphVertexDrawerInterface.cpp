// CFW1GlyphVertexDrawerInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphVertexDrawer.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1GlyphVertexDrawer::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1GlyphVertexDrawer))) {
		*ppvObject = static_cast<IFW1GlyphVertexDrawer*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Get the D3D11 device used by this vetex drawer
HRESULT STDMETHODCALLTYPE CFW1GlyphVertexDrawer::GetDevice(ID3D11Device **ppDevice) {
	if(ppDevice == NULL)
		return E_INVALIDARG;
	
	m_pDevice->AddRef();
	*ppDevice = m_pDevice;
	
	return S_OK;
}


// Draw vertices
UINT STDMETHODCALLTYPE CFW1GlyphVertexDrawer::DrawVertices(
	ID3D11DeviceContext *pContext,
	IFW1GlyphAtlas *pGlyphAtlas,
	const FW1_VERTEXDATA *pVertexData,
	UINT Flags,
	UINT PreboundSheet
) {
	UINT stride;
	UINT offset = 0;
	
	if((Flags & FW1_NOGEOMETRYSHADER) == 0)
		stride = sizeof(FW1_GLYPHVERTEX);
	else {
		stride = sizeof(QuadVertex);
		if((Flags & FW1_BUFFERSPREPARED) == 0)
			pContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	}
	if((Flags & FW1_BUFFERSPREPARED) == 0)
		pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
	
	if((Flags & FW1_NOGEOMETRYSHADER) == 0)
		return drawVertices(pContext, pGlyphAtlas, pVertexData, PreboundSheet);
	else
		return drawGlyphsAsQuads(pContext, pGlyphAtlas, pVertexData, PreboundSheet);
}


}// namespace FW1FontWrapper
