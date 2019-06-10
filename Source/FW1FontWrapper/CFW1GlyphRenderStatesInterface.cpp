// CFW1GlyphRenderStatesInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphRenderStates.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1GlyphRenderStates::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1GlyphRenderStates))) {
		*ppvObject = static_cast<IFW1GlyphRenderStates*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Get the D3D11 device used by the render states
HRESULT STDMETHODCALLTYPE CFW1GlyphRenderStates::GetDevice(ID3D11Device **ppDevice) {
	if(ppDevice == NULL)
		return E_INVALIDARG;
	
	m_pDevice->AddRef();
	*ppDevice = m_pDevice;
	
	return S_OK;
}


// Set render states for glyph drawing
void STDMETHODCALLTYPE CFW1GlyphRenderStates::SetStates(ID3D11DeviceContext *pContext, UINT Flags) {
	if(m_hasGeometryShader && ((Flags & FW1_NOGEOMETRYSHADER) == 0)) {
		// Point vertices with geometry shader
		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		pContext->IASetInputLayout(m_pPointInputLayout);
		pContext->VSSetShader(m_pVertexShaderPoint, NULL, 0);
		if((Flags & FW1_CLIPRECT) != 0)
			pContext->GSSetShader(m_pGeometryShaderClipPoint, NULL, 0);
		else
			pContext->GSSetShader(m_pGeometryShaderPoint, NULL, 0);
		pContext->PSSetShader(m_pPixelShader, NULL, 0);
		pContext->GSSetConstantBuffers(0, 1, &m_pConstantBuffer);
	}
	else {
		// Quads constructed on the CPU
		pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pContext->IASetInputLayout(m_pQuadInputLayout);
		if((Flags & FW1_CLIPRECT) != 0) {
			pContext->VSSetShader(m_pVertexShaderClipQuad, NULL, 0);
			pContext->PSSetShader(m_pPixelShaderClip, NULL, 0);
		}
		else {
			pContext->VSSetShader(m_pVertexShaderQuad, NULL, 0);
			pContext->PSSetShader(m_pPixelShader, NULL, 0);
		}
		pContext->VSSetConstantBuffers(0, 1, &m_pConstantBuffer);
		
		if(m_featureLevel >= D3D_FEATURE_LEVEL_10_0)
			pContext->GSSetShader(NULL, NULL, 0);
	}
	
	if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0) {
		pContext->DSSetShader(NULL, NULL, 0);
		pContext->HSSetShader(NULL, NULL, 0);
	}
	
	pContext->OMSetBlendState(m_pBlendState, NULL, 0xffffffff);
	pContext->OMSetDepthStencilState(m_pDepthStencilState, 0);
	
	pContext->RSSetState(m_pRasterizerState);
	
	pContext->PSSetSamplers(0, 1, &m_pSamplerState);
}


// Update constant buffer
void STDMETHODCALLTYPE CFW1GlyphRenderStates::UpdateShaderConstants(
	ID3D11DeviceContext *pContext,
	const FW1_RECTF *pClipRect,
	const FLOAT *pTransformMatrix
) {
	// Shader constants
	ShaderConstants constants;
	ZeroMemory(&constants, sizeof(constants));
	
	// Transform matrix
	if(pTransformMatrix != NULL)
		CopyMemory(constants.TransformMatrix, pTransformMatrix, 16*sizeof(FLOAT));
	else {
		// Get viewport size for orthographic transform
		FLOAT w = 512.0f;
		FLOAT h = 512.0f;
		
		D3D11_VIEWPORT vp;
		UINT nvp = 1;
		pContext->RSGetViewports(&nvp, &vp);
		if(nvp > 0) {
			if(vp.Width >= 1.0f && vp.Height >= 1.0f) {
				w = vp.Width;
				h = vp.Height;
			}
		}
		
		constants.TransformMatrix[0] = 2.0f / w;
		constants.TransformMatrix[12] = -1.0f;
		constants.TransformMatrix[5] = -2.0f / h;
		constants.TransformMatrix[13] = 1.0f;
		constants.TransformMatrix[10] = 1.0f;
		constants.TransformMatrix[15] = 1.0f;
	}
	
	// Clip rect
	if(pClipRect != NULL) {
		constants.ClipRect[0] = -pClipRect->Left;
		constants.ClipRect[1] = -pClipRect->Top;
		constants.ClipRect[2] = pClipRect->Right;
		constants.ClipRect[3] = pClipRect->Bottom;
	}
	else {
		constants.ClipRect[0] = FLT_MAX;
		constants.ClipRect[1] = FLT_MAX;
		constants.ClipRect[2] = FLT_MAX;
		constants.ClipRect[3] = FLT_MAX;
	}
	
	// Update constant buffer
	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hResult = pContext->Map(m_pConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if(SUCCEEDED(hResult)) {
		CopyMemory(msr.pData, &constants, sizeof(constants));
		
		pContext->Unmap(m_pConstantBuffer, 0);
	}
}


// Check for geometry shader
BOOL STDMETHODCALLTYPE CFW1GlyphRenderStates::HasGeometryShader() {
	return (m_hasGeometryShader ? TRUE : FALSE);
}


}// namespace FW1FontWrapper
