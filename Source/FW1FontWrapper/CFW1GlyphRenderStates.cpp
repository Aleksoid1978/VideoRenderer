// CFW1GlyphRenderStates.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphRenderStates.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1GlyphRenderStates::CFW1GlyphRenderStates() :
	m_pfnD3DCompile(NULL),
	
	m_pDevice(NULL),
	m_featureLevel(D3D_FEATURE_LEVEL_9_1),
	
	m_pVertexShaderQuad(NULL),
	m_pVertexShaderClipQuad(NULL),
	m_pQuadInputLayout(NULL),
	
	m_pVertexShaderPoint(NULL),
	m_pPointInputLayout(NULL),
	m_pGeometryShaderPoint(NULL),
	m_pGeometryShaderClipPoint(NULL),
	m_hasGeometryShader(false),
	
	m_pPixelShader(NULL),
	m_pPixelShaderClip(NULL),
	
	m_pConstantBuffer(NULL),
	
	m_pBlendState(NULL),
	m_pSamplerState(NULL),
	m_pRasterizerState(NULL),
	m_pDepthStencilState(NULL)
{
}


// Destruct
CFW1GlyphRenderStates::~CFW1GlyphRenderStates() {
	SAFE_RELEASE(m_pDevice);
	
	SAFE_RELEASE(m_pVertexShaderQuad);
	SAFE_RELEASE(m_pVertexShaderClipQuad);
	SAFE_RELEASE(m_pQuadInputLayout);
	
	SAFE_RELEASE(m_pVertexShaderPoint);
	SAFE_RELEASE(m_pPointInputLayout);
	SAFE_RELEASE(m_pGeometryShaderPoint);
	SAFE_RELEASE(m_pGeometryShaderClipPoint);
	
	SAFE_RELEASE(m_pPixelShader);
	SAFE_RELEASE(m_pPixelShaderClip);
	
	SAFE_RELEASE(m_pConstantBuffer);
	
	SAFE_RELEASE(m_pBlendState);
	SAFE_RELEASE(m_pSamplerState);
	SAFE_RELEASE(m_pRasterizerState);
	SAFE_RELEASE(m_pDepthStencilState);
}


// Init
HRESULT CFW1GlyphRenderStates::initRenderResources(
	IFW1Factory *pFW1Factory,
	ID3D11Device *pDevice,
	bool wantGeometryShader,
	bool anisotropicFiltering
) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	if(pDevice == NULL)
		return E_INVALIDARG;
	
	pDevice->AddRef();
	m_pDevice = pDevice;
	m_featureLevel = m_pDevice->GetFeatureLevel();
	
	// D3DCompiler
#ifdef FW1_DELAYLOAD_D3DCOMPILER_XX_DLL
	HMODULE hD3DCompiler = LoadLibrary(D3DCOMPILER_DLL);
	if(hD3DCompiler == NULL) {
		DWORD dwErr = GetLastError();
		dwErr;
		m_lastError = std::wstring(L"Failed to load ") + D3DCOMPILER_DLL_W;
		hResult = E_FAIL;
	}
	else {
		m_pfnD3DCompile = reinterpret_cast<pD3DCompile>(GetProcAddress(hD3DCompiler, "D3DCompile"));
		if(m_pfnD3DCompile == NULL) {
			DWORD dwErr = GetLastError();
			dwErr;
			m_lastError = std::wstring(L"Failed to load D3DCompile from ") + D3DCOMPILER_DLL_W;
			hResult = E_FAIL;
		}
		else {
			hResult = S_OK;
		}
	}
#else
	m_pfnD3DCompile = D3DCompile;
	hResult = S_OK;
#endif
	
	// Create all needed resources
	if(SUCCEEDED(hResult))
		hResult = createQuadShaders();
	if(SUCCEEDED(hResult))
		hResult = createPixelShaders();
	if(SUCCEEDED(hResult))
		hResult = createConstantBuffer();
	if(SUCCEEDED(hResult))
		hResult = createRenderStates(anisotropicFiltering);
	if(SUCCEEDED(hResult) && wantGeometryShader) {
		hResult = createGlyphShaders();
		if(FAILED(hResult))
			hResult = S_OK;
	}
	
	if(SUCCEEDED(hResult))
		hResult = S_OK;
	
#ifdef FW1_DELAYLOAD_D3DCOMPILER_XX_DLL
	FreeLibrary(hD3DCompiler);
#endif
	
	return hResult;
}


// Create quad shaders
HRESULT CFW1GlyphRenderStates::createQuadShaders() {
	// Vertex shaders
	const char vsSimpleStr[] =
	"cbuffer ShaderConstants : register(b0) {\r\n"
	"	float4x4 TransformMatrix : packoffset(c0);\r\n"
	"};\r\n"
	"\r\n"
	"struct VSIn {\r\n"
	"	float4 Position : POSITION;\r\n"
	"	float4 GlyphColor : GLYPHCOLOR;\r\n"
	"};\r\n"
	"\r\n"
	"struct VSOut {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"};\r\n"
	"\r\n"
	"VSOut VS(VSIn Input) {\r\n"
	"	VSOut Output;\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(Input.Position.xy, 0.0f, 1.0f));\r\n"
	"	Output.GlyphColor = Input.GlyphColor;\r\n"
	"	Output.TexCoord = Input.Position.zw;\r\n"
	"	\r\n"
	"	return Output;\r\n"
	"}\r\n"
	"";
	
	const char vsClipStr[] =
	"cbuffer ShaderConstants : register(b0) {\r\n"
	"	float4x4 TransformMatrix : packoffset(c0);\r\n"
	"	float4 ClipRect : packoffset(c4);\r\n"
	"};\r\n"
	"\r\n"
	"struct VSIn {\r\n"
	"	float4 Position : POSITION;\r\n"
	"	float4 GlyphColor : GLYPHCOLOR;\r\n"
	"};\r\n"
	"\r\n"
	"struct VSOut {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"	float4 ClipDistance : CLIPDISTANCE;\r\n"
	"};\r\n"
	"\r\n"
	"VSOut VS(VSIn Input) {\r\n"
	"	VSOut Output;\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(Input.Position.xy, 0.0f, 1.0f));\r\n"
	"	Output.GlyphColor = Input.GlyphColor;\r\n"
	"	Output.TexCoord = Input.Position.zw;\r\n"
	"	Output.ClipDistance = ClipRect + float4(Input.Position.xy, -Input.Position.xy);\r\n"
	"	\r\n"
	"	return Output;\r\n"
	"}\r\n"
	"";
	
	// Shader compile profile
	const char *vs_profile = "vs_4_0_level_9_1";
	if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0)
		vs_profile = "vs_5_0";
	else if(m_featureLevel >= D3D_FEATURE_LEVEL_10_0)
		vs_profile = "vs_4_0";
	else if(m_featureLevel >= D3D_FEATURE_LEVEL_9_3)
		vs_profile = "vs_4_0_level_9_3";
	
	// Compile vertex shader
	ID3DBlob *pVSCode;
	
	HRESULT hResult = m_pfnD3DCompile(
		vsSimpleStr,
		sizeof(vsSimpleStr),
		NULL,
		NULL,
		NULL,
		"VS",
		vs_profile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0,
		&pVSCode,
		NULL
	);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to compile vertex shader";
	}
	else {
		// Create vertex shader
		ID3D11VertexShader *pVS;
		
		hResult = m_pDevice->CreateVertexShader(pVSCode->GetBufferPointer(), pVSCode->GetBufferSize(), NULL, &pVS);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create vertex shader";
		}
		else {
			// Compile clipping vertex shader
			ID3DBlob *pVSClipCode;
			
			hResult = m_pfnD3DCompile(
				vsClipStr,
				sizeof(vsClipStr),
				NULL,
				NULL,
				NULL,
				"VS",
				vs_profile,
				D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&pVSClipCode,
				NULL
			);
			if(FAILED(hResult)) {
				m_lastError = L"Failed to compile clipping vertex shader";
			}
			else {
				// Create vertex shader
				ID3D11VertexShader *pVSClip;
				
				hResult = m_pDevice->CreateVertexShader(
					pVSClipCode->GetBufferPointer(),
					pVSClipCode->GetBufferSize(),
					NULL,
					&pVSClip
				);
				if(FAILED(hResult)) {
					m_lastError = L"Failed to create clipping vertex shader";
				}
				else {
					// Create input layout
					ID3D11InputLayout *pInputLayout;
					
					// Quad vertex input layout
					D3D11_INPUT_ELEMENT_DESC inputElements[] = {
						{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
						{"GLYPHCOLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
					};
					
					hResult = m_pDevice->CreateInputLayout(
						inputElements,
						2,
						pVSCode->GetBufferPointer(),
						pVSCode->GetBufferSize(),
						&pInputLayout
					);
					if(FAILED(hResult)) {
						m_lastError = L"Failed to create input layout";
					}
					else {
						// Success
						m_pVertexShaderQuad = pVS;
						m_pVertexShaderClipQuad = pVSClip;
						m_pQuadInputLayout = pInputLayout;
						
						hResult = S_OK;
					}
					
					if(FAILED(hResult))
						pVSClip->Release();
				}
				
				pVSClipCode->Release();
			}
			
			if(FAILED(hResult))
				pVS->Release();
		}
		
		pVSCode->Release();
	}
	
	return hResult;
}

// Create point to quad geometry shader
HRESULT CFW1GlyphRenderStates::createGlyphShaders() {
	if(m_featureLevel < D3D_FEATURE_LEVEL_10_0)
		return E_FAIL;
	
	// Geometry shader constructing glyphs from point input and texture buffer
	const char gsSimpleStr[] =
	"cbuffer ShaderConstants : register(b0) {\r\n"
	"	float4x4 TransformMatrix : packoffset(c0);\r\n"
	"};\r\n"
	"\r\n"
	"Buffer<float4> tex0 : register(t0);\r\n"
	"\r\n"
	"struct GSIn {\r\n"
	"	float3 PositionIndex : POSITIONINDEX;\r\n"
	"	float4 GlyphColor : GLYPHCOLOR;\r\n"
	"};\r\n"
	"\r\n"
	"struct GSOut {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"};\r\n"
	"\r\n"
	"[maxvertexcount(4)]\r\n"
	"void GS(point GSIn Input[1], inout TriangleStream<GSOut> TriStream) {\r\n"
	"	const float2 basePosition = Input[0].PositionIndex.xy;\r\n"
	"	const uint glyphIndex = asuint(Input[0].PositionIndex.z);\r\n"
	"	\r\n"
	"	float4 texCoords = tex0.Load(uint2(glyphIndex*2, 0));\r\n"
	"	float4 offsets = tex0.Load(uint2(glyphIndex*2+1, 0));\r\n"
	"	\r\n"
	"	GSOut Output;\r\n"
	"	Output.GlyphColor = Input[0].GlyphColor;\r\n"
	"	\r\n"
	"	float4 positions = basePosition.xyxy + offsets;\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.xy, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.xy;\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.zy, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.zy;\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.xw, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.xw;\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.zw, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.zw;\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	TriStream.RestartStrip();\r\n"
	"}\r\n"
	"";
	
	// Geometry shader with rect clipping
	const char gsClipStr[] =
	"cbuffer ShaderConstants : register(b0) {\r\n"
	"	float4x4 TransformMatrix : packoffset(c0);\r\n"
	"	float4 ClipRect : packoffset(c4);\r\n"
	"};\r\n"
	"\r\n"
	"Buffer<float4> tex0 : register(t0);\r\n"
	"\r\n"
	"struct GSIn {\r\n"
	"	float3 PositionIndex : POSITIONINDEX;\r\n"
	"	float4 GlyphColor : GLYPHCOLOR;\r\n"
	"};\r\n"
	"\r\n"
	"struct GSOut {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"	float4 ClipDistance : SV_ClipDistance;\r\n"
	"};\r\n"
	"\r\n"
	"[maxvertexcount(4)]\r\n"
	"void GS(point GSIn Input[1], inout TriangleStream<GSOut> TriStream) {\r\n"
	"	const float2 basePosition = Input[0].PositionIndex.xy;\r\n"
	"	const uint glyphIndex = asuint(Input[0].PositionIndex.z);\r\n"
	"	\r\n"
	"	float4 texCoords = tex0.Load(uint2(glyphIndex*2, 0));\r\n"
	"	float4 offsets = tex0.Load(uint2(glyphIndex*2+1, 0));\r\n"
	"	\r\n"
	"	GSOut Output;\r\n"
	"	Output.GlyphColor = Input[0].GlyphColor;\r\n"
	"	\r\n"
	"	float4 positions = basePosition.xyxy + offsets;\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.xy, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.xy;\r\n"
	"	Output.ClipDistance = ClipRect + float4(positions.xy, -positions.xy);\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.zy, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.zy;\r\n"
	"	Output.ClipDistance = ClipRect + float4(positions.zy, -positions.zy);\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.xw, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.xw;\r\n"
	"	Output.ClipDistance = ClipRect + float4(positions.xw, -positions.xw);\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	Output.Position = mul(TransformMatrix, float4(positions.zw, 0.0f, 1.0f));\r\n"
	"	Output.TexCoord = texCoords.zw;\r\n"
	"	Output.ClipDistance = ClipRect + float4(positions.zw, -positions.zw);\r\n"
	"	TriStream.Append(Output);\r\n"
	"	\r\n"
	"	TriStream.RestartStrip();\r\n"
	"}\r\n"
	"";
	
	// Vertex shader
	const char vsEmptyStr[] =
	"struct GSIn {\r\n"
	"	float3 PositionIndex : POSITIONINDEX;\r\n"
	"	float4 GlyphColor : GLYPHCOLOR;\r\n"
	"};\r\n"
	"\r\n"
	"GSIn VS(GSIn Input) {\r\n"
	"	return Input;\r\n"
	"}\r\n"
	"";
	
	// Shader compile profiles
	const char *vs_profile = "vs_4_0";
	const char *gs_profile = "gs_4_0";
	if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0) {
		vs_profile = "vs_5_0";
		gs_profile = "gs_5_0";
	}
	
	// Compile geometry shader
	ID3DBlob *pGSCode;
	
	HRESULT hResult = m_pfnD3DCompile(
		gsSimpleStr,
		sizeof(gsSimpleStr),
		NULL,
		NULL,
		NULL,
		"GS",
		gs_profile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0,
		&pGSCode,
		NULL
	);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to compile geometry shader";
	}
	else {
		// Create geometry shader
		ID3D11GeometryShader *pGS;
		
		hResult = m_pDevice->CreateGeometryShader(pGSCode->GetBufferPointer(), pGSCode->GetBufferSize(), NULL, &pGS);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create geometry shader";
		}
		else {
			// Compile clipping geometry shader
			ID3DBlob *pGSClipCode;
			
			hResult = m_pfnD3DCompile(
				gsClipStr,
				sizeof(gsClipStr),
				NULL,
				NULL,
				NULL,
				"GS",
				gs_profile,
				D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&pGSClipCode,
				NULL
			);
			if(FAILED(hResult)) {
				m_lastError = L"Failed to compile clipping geometry shader";
			}
			else {
				// Create clipping geometry shader
				ID3D11GeometryShader *pGSClip;
				
				hResult = m_pDevice->CreateGeometryShader(
					pGSClipCode->GetBufferPointer(),
					pGSClipCode->GetBufferSize(),
					NULL,
					&pGSClip
				);
				if(FAILED(hResult)) {
					m_lastError = L"Failed to create clipping geometry shader";
				}
				else {
					ID3DBlob *pVSEmptyCode;
					
					// Compile vertex shader
					hResult = m_pfnD3DCompile(
						vsEmptyStr,
						sizeof(vsEmptyStr),
						NULL,
						NULL,
						NULL,
						"VS",
						vs_profile,
						D3DCOMPILE_OPTIMIZATION_LEVEL3,
						0,
						&pVSEmptyCode,
						NULL
					);
					if(FAILED(hResult)) {
						m_lastError = L"Failed to compile empty vertex shader";
					}
					else {
						// Create vertex shader
						ID3D11VertexShader *pVSEmpty;
						
						hResult = m_pDevice->CreateVertexShader(
							pVSEmptyCode->GetBufferPointer(),
							pVSEmptyCode->GetBufferSize(),
							NULL,
							&pVSEmpty
						);
						if(FAILED(hResult)) {
							m_lastError = L"Failed to create empty vertex shader";
						}
						else {
							ID3D11InputLayout *pInputLayout;
							
							// Input layout for geometry shader
							D3D11_INPUT_ELEMENT_DESC inputElements[] = {
								{"POSITIONINDEX", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
								{"GLYPHCOLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
							};
							
							hResult = m_pDevice->CreateInputLayout(
								inputElements,
								2,
								pVSEmptyCode->GetBufferPointer(),
								pVSEmptyCode->GetBufferSize(),
								&pInputLayout
							);
							if(FAILED(hResult)) {
								m_lastError = L"Failed to create input layout for geometry shader";
							}
							else {
								// Success
								m_pVertexShaderPoint = pVSEmpty;
								m_pGeometryShaderPoint = pGS;
								m_pGeometryShaderClipPoint = pGSClip;
								m_pPointInputLayout = pInputLayout;
								m_hasGeometryShader = true;
								
								hResult = S_OK;
							}
							
							if(FAILED(hResult))
								pVSEmpty->Release();
						}
						
						pVSEmptyCode->Release();
					}
					
					if(FAILED(hResult))
						pGSClip->Release();
				}
				
				pGSClipCode->Release();
			}
			
			if(FAILED(hResult))
				pGS->Release();
		}
		
		pGSCode->Release();
	}
	
	return hResult;
}

// Create pixel shaders
HRESULT CFW1GlyphRenderStates::createPixelShaders() {
	// Pixel shader
	const char psStr[] =
	"SamplerState sampler0 : register(s0);\r\n"
	"Texture2D<float> tex0 : register(t0);\r\n"
	"\r\n"
	"struct PSIn {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"};\r\n"
	"\r\n"
	"float4 PS(PSIn Input) : SV_Target {\r\n"
	"	float a = tex0.Sample(sampler0, Input.TexCoord);\r\n"
	"	\r\n"
	"	if(a == 0.0f)\r\n"
	"		discard;\r\n"
	"	\r\n"
	"	return (a * Input.GlyphColor.a) * float4(Input.GlyphColor.rgb, 1.0f);\r\n"
	"}\r\n"
	"";
	
	// Clipping pixel shader
	const char psClipStr[] =
	"SamplerState sampler0 : register(s0);\r\n"
	"Texture2D<float> tex0 : register(t0);\r\n"
	"\r\n"
	"struct PSIn {\r\n"
	"	float4 Position : SV_Position;\r\n"
	"	float4 GlyphColor : COLOR;\r\n"
	"	float2 TexCoord : TEXCOORD;\r\n"
	"	float4 ClipDistance : CLIPDISTANCE;\r\n"
	"};\r\n"
	"\r\n"
	"float4 PS(PSIn Input) : SV_Target {\r\n"
	"	clip(Input.ClipDistance);\r\n"
	"	\r\n"
	"	float a = tex0.Sample(sampler0, Input.TexCoord);\r\n"
	"	\r\n"
	"	if(a == 0.0f)\r\n"
	"		discard;\r\n"
	"	\r\n"
	"	return (a * Input.GlyphColor.a) * float4(Input.GlyphColor.rgb, 1.0f);\r\n"
	"}\r\n"
	"";
	
	// Shader compile profile
	const char *ps_profile = "ps_4_0_level_9_1";
	if(m_featureLevel >= D3D_FEATURE_LEVEL_11_0)
		ps_profile = "ps_5_0";
	else if(m_featureLevel >= D3D_FEATURE_LEVEL_10_0)
		ps_profile = "ps_4_0";
	else if(m_featureLevel >= D3D_FEATURE_LEVEL_9_3)
		ps_profile = "ps_4_0_level_9_3";
	
	// Compile pixel shader
	ID3DBlob *pPSCode;
	
	HRESULT hResult = m_pfnD3DCompile(
		psStr,
		sizeof(psStr),
		NULL,
		NULL,
		NULL,
		"PS",
		ps_profile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3,
		0,
		&pPSCode,
		NULL
	);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to compile pixel shader";
	}
	else {
		// Create pixel shader
		ID3D11PixelShader *pPS;
		
		hResult = m_pDevice->CreatePixelShader(pPSCode->GetBufferPointer(), pPSCode->GetBufferSize(), NULL, &pPS);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create pixel shader";
		}
		else {
			// Compile clipping pixel shader
			ID3DBlob *pPSClipCode;
			
			HRESULT hResult = m_pfnD3DCompile(
				psClipStr,
				sizeof(psClipStr),
				NULL,
				NULL,
				NULL,
				"PS",
				ps_profile,
				D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&pPSClipCode,
				NULL
			);
			if(FAILED(hResult)) {
				m_lastError = L"Failed to compile clipping pixel shader";
			}
			else {
				// Create pixel shader
				ID3D11PixelShader *pPSClip;
				
				hResult = m_pDevice->CreatePixelShader(
					pPSClipCode->GetBufferPointer(),
					pPSClipCode->GetBufferSize(),
					NULL, &pPSClip
				);
				if(FAILED(hResult)) {
					m_lastError = L"Failed to create clipping pixel shader";
				}
				else {
					// Success
					m_pPixelShader = pPS;
					m_pPixelShaderClip = pPSClip;
					
					hResult = S_OK;
				}
				
				pPSClipCode->Release();
			}
			
			if(FAILED(hResult))
				pPS->Release();
		}
		
		pPSCode->Release();
	}
	
	return hResult;
}


// Create constant buffer
HRESULT CFW1GlyphRenderStates::createConstantBuffer() {
	// Create constant buffer
	D3D11_BUFFER_DESC constantBufferDesc;
	ID3D11Buffer *pConstantBuffer;
			
	ZeroMemory(&constantBufferDesc, sizeof(constantBufferDesc));
	constantBufferDesc.ByteWidth = sizeof(ShaderConstants);
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			
	HRESULT hResult = m_pDevice->CreateBuffer(&constantBufferDesc, NULL, &pConstantBuffer);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to create constant buffer";
	}
	else {
		// Success
		m_pConstantBuffer = pConstantBuffer;
				
		hResult = S_OK;
	}

	return hResult;
}


// Create render states
HRESULT CFW1GlyphRenderStates::createRenderStates(bool anisotropicFiltering) {
	// Create blend-state
	D3D11_BLEND_DESC blendDesc;
	ID3D11BlendState *pBlendState;
	
	ZeroMemory(&blendDesc, sizeof(blendDesc));
	for(int i=0; i < 4; ++i) {
		blendDesc.RenderTarget[i].BlendEnable = TRUE;
		blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	}
	
	HRESULT hResult = m_pDevice->CreateBlendState(&blendDesc, &pBlendState);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to create blend state";
	}
	else {
		// Create sampler state
		D3D11_SAMPLER_DESC samplerDesc;
		ID3D11SamplerState *pSamplerState;
		
		ZeroMemory(&samplerDesc, sizeof(samplerDesc));
		if(anisotropicFiltering) {
			samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
			samplerDesc.MaxAnisotropy = 2;
			if(m_featureLevel >= D3D_FEATURE_LEVEL_9_2)
				samplerDesc.MaxAnisotropy = 5;
		}
		else
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		
		hResult = m_pDevice->CreateSamplerState(&samplerDesc, &pSamplerState);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create sampler state";
		}
		else {
			// Create rasterizer state
			D3D11_RASTERIZER_DESC rasterizerDesc;
			ID3D11RasterizerState *pRasterizerState;
			
			ZeroMemory(&rasterizerDesc, sizeof(rasterizerDesc));
			rasterizerDesc.FillMode = D3D11_FILL_SOLID;
			rasterizerDesc.CullMode = D3D11_CULL_NONE;
			rasterizerDesc.FrontCounterClockwise = FALSE;
			rasterizerDesc.DepthClipEnable = TRUE;
			
			hResult = m_pDevice->CreateRasterizerState(&rasterizerDesc, &pRasterizerState);
			if(FAILED(hResult)) {
				m_lastError = L"Failed to create rasterizer state";
			}
			else {
				// Create depth-stencil state
				D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
				ID3D11DepthStencilState *pDepthStencilState;
				
				ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));
				depthStencilDesc.DepthEnable = FALSE;
				
				hResult = m_pDevice->CreateDepthStencilState(&depthStencilDesc, &pDepthStencilState);
				if(FAILED(hResult)) {
					m_lastError = L"Failed to create depth stencil state";
				}
				else {
					// Success
					m_pBlendState = pBlendState;
					m_pSamplerState = pSamplerState;
					m_pRasterizerState = pRasterizerState;
					m_pDepthStencilState = pDepthStencilState;
					
					hResult = S_OK;
				}
				
				if(FAILED(hResult))
					pRasterizerState->Release();
			}
			
			if(FAILED(hResult))
				pSamplerState->Release();
		}
		
		if(FAILED(hResult))
			pBlendState->Release();
	}
	
	return hResult;
}


}// namespace FW1FontWrapper
