// CFW1StateSaver.h

#ifndef IncludeGuard__FW1_CFW1StateSaver
#define IncludeGuard__FW1_CFW1StateSaver


namespace FW1FontWrapper {


// Saves all the states that can be changed when drawing a string
class CFW1StateSaver {
	// Public functions
	public:
		CFW1StateSaver();
		~CFW1StateSaver();
		
		HRESULT saveCurrentState(ID3D11DeviceContext *pContext);
		HRESULT restoreSavedState();
		void releaseSavedState();
	
	// Internal data
	private:
		bool						m_savedState;
		D3D_FEATURE_LEVEL			m_featureLevel;
		ID3D11DeviceContext			*m_pContext;
		D3D11_PRIMITIVE_TOPOLOGY	m_primitiveTopology;
		ID3D11InputLayout			*m_pInputLayout;
		ID3D11BlendState			*m_pBlendState;
		FLOAT						m_blendFactor[4];
		UINT						m_sampleMask;
		ID3D11DepthStencilState		*m_pDepthStencilState;
		UINT						m_stencilRef;
		ID3D11RasterizerState		*m_pRasterizerState;
		ID3D11ShaderResourceView	*m_pPSSRV;
		ID3D11SamplerState			*m_pSamplerState;
		ID3D11VertexShader			*m_pVS;
		ID3D11ClassInstance			*m_pVSClassInstances[256];
		UINT						m_numVSClassInstances;
		ID3D11Buffer				*m_pVSConstantBuffer;
		ID3D11GeometryShader		*m_pGS;
		ID3D11ClassInstance			*m_pGSClassInstances[256];
		UINT						m_numGSClassInstances;
		ID3D11Buffer				*m_pGSConstantBuffer;
		ID3D11ShaderResourceView	*m_pGSSRV;
		ID3D11PixelShader			*m_pPS;
		ID3D11ClassInstance			*m_pPSClassInstances[256];
		UINT						m_numPSClassInstances;
		ID3D11HullShader			*m_pHS;
		ID3D11ClassInstance			*m_pHSClassInstances[256];
		UINT						m_numHSClassInstances;
		ID3D11DomainShader			*m_pDS;
		ID3D11ClassInstance			*m_pDSClassInstances[256];
		UINT						m_numDSClassInstances;
		ID3D11Buffer				*m_pVB;
		UINT						m_vertexStride;
		UINT						m_vertexOffset;
		ID3D11Buffer				*m_pIndexBuffer;
		DXGI_FORMAT					m_indexFormat;
		UINT						m_indexOffset;
	
	private:
		CFW1StateSaver(const CFW1StateSaver&);
		CFW1StateSaver& operator=(const CFW1StateSaver&);
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1StateSaver
