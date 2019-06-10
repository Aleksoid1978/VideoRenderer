// CFW1StateSaver.cpp

#include "FW1Precompiled.h"

#include "CFW1StateSaver.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1StateSaver::CFW1StateSaver() :
	m_savedState(false),
	m_featureLevel(D3D_FEATURE_LEVEL_11_0),
	m_pContext(NULL),
	m_primitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED),
	m_pInputLayout(NULL),
	m_pBlendState(NULL),
	m_sampleMask(0xffffffff),
	m_pDepthStencilState(NULL),
	m_stencilRef(0),
	m_pRasterizerState(NULL),
	m_pPSSRV(NULL),
	m_pSamplerState(NULL),
	m_pVS(NULL),
	m_numVSClassInstances(0),
	m_pVSConstantBuffer(NULL),
	m_pGS(NULL),
	m_numGSClassInstances(0),
	m_pGSConstantBuffer(NULL),
	m_pGSSRV(NULL),
	m_pPS(NULL),
	m_numPSClassInstances(0),
	m_pHS(NULL),
	m_numHSClassInstances(0),
	m_pDS(NULL),
	m_numDSClassInstances(0),
	m_pVB(NULL),
	m_vertexStride(0),
	m_vertexOffset(0),
	m_pIndexBuffer(NULL),
	m_indexFormat(DXGI_FORMAT_UNKNOWN),
	m_indexOffset(0)
{
	for(int i=0; i < 4; ++i)
		m_blendFactor[i] = 0.0f;
	for(int i=0; i < 256; ++i) {
		m_pVSClassInstances[i] = NULL;
		m_pGSClassInstances[i] = NULL;
		m_pPSClassInstances[i] = NULL;
		m_pHSClassInstances[i] = NULL;
		m_pDSClassInstances[i] = NULL;
	}
}


// Destruct
CFW1StateSaver::~CFW1StateSaver() {
	releaseSavedState();
}


// Save all states that are changed by the font-wrapper when drawing a string
HRESULT CFW1StateSaver::saveCurrentState(ID3D11DeviceContext *pContext) {
	if(m_savedState)
		releaseSavedState();
	if(pContext == NULL)
		return E_INVALIDARG;
	
	ID3D11Device *pDevice;
	pContext->GetDevice(&pDevice);
	if(pDevice != NULL) {
		m_featureLevel = pDevice->GetFeatureLevel();
		pDevice->Release();
	}
	
	pContext->AddRef();
	m_pContext = pContext;
	
	m_pContext->IAGetPrimitiveTopology(&m_primitiveTopology);
	m_pContext->IAGetInputLayout(&m_pInputLayout);
	
	m_pContext->OMGetBlendState(&m_pBlendState, m_blendFactor, &m_sampleMask);
	m_pContext->OMGetDepthStencilState(&m_pDepthStencilState, &m_stencilRef);
	
	m_pContext->RSGetState(&m_pRasterizerState);
	
	m_numVSClassInstances = 256;
	m_pContext->VSGetShader(&m_pVS, m_pVSClassInstances, &m_numVSClassInstances);
	m_pContext->VSGetConstantBuffers(0, 1, &m_pVSConstantBuffer);
	
	m_numPSClassInstances = 256;
	m_pContext->PSGetShader(&m_pPS, m_pPSClassInstances, &m_numPSClassInstances);
	m_pContext->PSGetShaderResources(0, 1, &m_pPSSRV);
	pContext->PSGetSamplers(0, 1, &m_pSamplerState);
	
	if(m_featureLevel >= D3D_FEATURE_LEVEL_10_0) {
		m_numGSClassInstances = 256;
		m_pContext->GSGetShader(&m_pGS, m_pGSClassInstances, &m_numGSClassInstances);
		m_pContext->GSGetConstantBuffers(0, 1, &m_pGSConstantBuffer);
		
		m_pContext->GSGetShaderResources(0, 1, &m_pGSSRV);
		
		if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0) {
			m_numHSClassInstances = 256;
			m_pContext->HSGetShader(&m_pHS, m_pHSClassInstances, &m_numHSClassInstances);
			
			m_numDSClassInstances = 256;
			m_pContext->DSGetShader(&m_pDS, m_pDSClassInstances, &m_numDSClassInstances);
		}
	}
	
	m_pContext->IAGetVertexBuffers(0, 1, &m_pVB, &m_vertexStride, &m_vertexOffset);
	
	m_pContext->IAGetIndexBuffer(&m_pIndexBuffer, &m_indexFormat, &m_indexOffset);
	
	m_savedState = true;
	
	return S_OK;
}


// Restore state
HRESULT CFW1StateSaver::restoreSavedState() {
	if(!m_savedState)
		return E_FAIL;
	
	m_pContext->IASetPrimitiveTopology(m_primitiveTopology);
	m_pContext->IASetInputLayout(m_pInputLayout);
	
	m_pContext->OMSetBlendState(m_pBlendState, m_blendFactor, m_sampleMask);
	m_pContext->OMSetDepthStencilState(m_pDepthStencilState, m_stencilRef);
	
	m_pContext->RSSetState(m_pRasterizerState);
	
	m_pContext->VSSetShader(m_pVS, m_pVSClassInstances, m_numVSClassInstances);
	m_pContext->VSSetConstantBuffers(0, 1, &m_pVSConstantBuffer);
	
	m_pContext->PSSetShader(m_pPS, m_pPSClassInstances, m_numPSClassInstances);
	m_pContext->PSSetShaderResources(0, 1, &m_pPSSRV);
	m_pContext->PSSetSamplers(0, 1, &m_pSamplerState);
	
	if(m_featureLevel >= D3D_FEATURE_LEVEL_10_0) {
		m_pContext->GSSetShader(m_pGS, m_pGSClassInstances, m_numGSClassInstances);
		m_pContext->GSSetConstantBuffers(0, 1, &m_pGSConstantBuffer);
		
		m_pContext->GSSetShaderResources(0, 1, &m_pGSSRV);
		
		if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0) {
			m_pContext->HSSetShader(m_pHS, m_pHSClassInstances, m_numHSClassInstances);
			
			m_pContext->DSSetShader(m_pDS, m_pDSClassInstances, m_numDSClassInstances);
		}
	}
	
	m_pContext->IASetVertexBuffers(0, 1, &m_pVB, &m_vertexStride, &m_vertexOffset);
	
	m_pContext->IASetIndexBuffer(m_pIndexBuffer, m_indexFormat, m_indexOffset);
	
	return S_OK;
}


// Release state
void CFW1StateSaver::releaseSavedState() {
	m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	SAFE_RELEASE(m_pInputLayout);
	SAFE_RELEASE(m_pBlendState);
	for(int i=0; i < 4; ++i)
		m_blendFactor[i] = 0.0f;
	m_sampleMask = 0xffffffff;
	SAFE_RELEASE(m_pDepthStencilState);
	m_stencilRef = 0;
	SAFE_RELEASE(m_pRasterizerState);
	SAFE_RELEASE(m_pPSSRV);
	SAFE_RELEASE(m_pSamplerState);
	SAFE_RELEASE(m_pVS);
	for(UINT i=0; i < m_numVSClassInstances; ++i)
		SAFE_RELEASE(m_pVSClassInstances[i]);
	m_numVSClassInstances = 0;
	SAFE_RELEASE(m_pVSConstantBuffer);
	SAFE_RELEASE(m_pGS);
	for(UINT i=0; i < m_numGSClassInstances; ++i)
		SAFE_RELEASE(m_pGSClassInstances[i]);
	m_numGSClassInstances = 0;
	SAFE_RELEASE(m_pGSConstantBuffer);
	SAFE_RELEASE(m_pGSSRV);
	SAFE_RELEASE(m_pPS);
	for(UINT i=0; i < m_numPSClassInstances; ++i)
		SAFE_RELEASE(m_pPSClassInstances[i]);
	m_numPSClassInstances = 0;
	SAFE_RELEASE(m_pHS);
	for(UINT i=0; i < m_numHSClassInstances; ++i)
		SAFE_RELEASE(m_pHSClassInstances[i]);
	m_numHSClassInstances = 0;
	SAFE_RELEASE(m_pDS);
	for(UINT i=0; i < m_numDSClassInstances; ++i)
		SAFE_RELEASE(m_pDSClassInstances[i]);
	m_numDSClassInstances = 0;
	SAFE_RELEASE(m_pVB);
	m_vertexStride = 0;
	m_vertexOffset = 0;
	SAFE_RELEASE(m_pIndexBuffer);
	m_indexFormat = DXGI_FORMAT_UNKNOWN;
	m_indexOffset = 0;
	
	SAFE_RELEASE(m_pContext);
	m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	
	m_savedState = false;
}


}// namespace FW1FontWrapper
