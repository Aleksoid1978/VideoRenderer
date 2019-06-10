// CFW1GlyphVertexDrawer.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphVertexDrawer.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1GlyphVertexDrawer::CFW1GlyphVertexDrawer() :
	m_pDevice(NULL),
	
	m_pVertexBuffer(NULL),
	m_pIndexBuffer(NULL),
	m_vertexBufferSize(0),
	m_maxIndexCount(0)
{
}


// Destruct
CFW1GlyphVertexDrawer::~CFW1GlyphVertexDrawer() {
	SAFE_RELEASE(m_pDevice);
	
	SAFE_RELEASE(m_pVertexBuffer);
	SAFE_RELEASE(m_pIndexBuffer);
}


// Init
HRESULT CFW1GlyphVertexDrawer::initVertexDrawer(
	IFW1Factory *pFW1Factory,
	ID3D11Device *pDevice,
	UINT vertexBufferSize
) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	if(pDevice == NULL)
		return E_INVALIDARG;
	
	pDevice->AddRef();
	m_pDevice = pDevice;
	D3D_FEATURE_LEVEL featureLevel = m_pDevice->GetFeatureLevel();
	
	m_vertexBufferSize = 4096 * 16;
	if(vertexBufferSize >= 1024) {
		if(featureLevel < D3D_FEATURE_LEVEL_9_2)
			vertexBufferSize = std::min(vertexBufferSize, 512U*1024U);
		m_vertexBufferSize = vertexBufferSize;
	}
	
	m_maxIndexCount = (m_vertexBufferSize * 3) / (2 * sizeof(QuadVertex));
	if(m_maxIndexCount < 64)
		m_maxIndexCount = 64;
	
	// Create device buffers
	hResult = createBuffers();
	
	if(SUCCEEDED(hResult))
		hResult = S_OK;
	
	return hResult;
}


// Create vertex/index buffers
HRESULT CFW1GlyphVertexDrawer::createBuffers() {
	// Create vertex buffer
	D3D11_BUFFER_DESC vertexBufferDesc;
	ID3D11Buffer *pVertexBuffer;
	
	ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));
	vertexBufferDesc.ByteWidth = m_vertexBufferSize;
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	
	HRESULT hResult = m_pDevice->CreateBuffer(&vertexBufferDesc, NULL, &pVertexBuffer);
	if(FAILED(hResult)) {
		m_lastError = L"Failed to create vertex buffer";
	}
	else {
		// Create index buffer
		D3D11_BUFFER_DESC indexBufferDesc;
		D3D11_SUBRESOURCE_DATA initData;
		ID3D11Buffer *pIndexBuffer;
		
		ZeroMemory(&indexBufferDesc, sizeof(indexBufferDesc));
		indexBufferDesc.ByteWidth = sizeof(UINT16) * m_maxIndexCount;
		indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		
		UINT16 *indices = new UINT16[m_maxIndexCount];
		for(UINT i=0; i < m_maxIndexCount/6; ++i) {
			indices[i*6] = static_cast<UINT16>(i*4);
			indices[i*6+1] = static_cast<UINT16>(i*4+1);
			indices[i*6+2] = static_cast<UINT16>(i*4+2);
			indices[i*6+3] = static_cast<UINT16>(i*4+1);
			indices[i*6+4] = static_cast<UINT16>(i*4+3);
			indices[i*6+5] = static_cast<UINT16>(i*4+2);
		}
		
		ZeroMemory(&initData, sizeof(initData));
		initData.pSysMem = indices;
		
		hResult = m_pDevice->CreateBuffer(&indexBufferDesc, &initData, &pIndexBuffer);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create index buffer";
		}
		else {
			// Success
			m_pVertexBuffer = pVertexBuffer;
			m_pIndexBuffer = pIndexBuffer;
			
			hResult = S_OK;
		}
		
		delete[] indices;
		
		if(FAILED(hResult))
			pVertexBuffer->Release();
	}
	
	return hResult;
}


// Draw vertices
UINT CFW1GlyphVertexDrawer::drawVertices(
	ID3D11DeviceContext *pContext,
	IFW1GlyphAtlas *pGlyphAtlas,
	const FW1_VERTEXDATA *vertexData,
	UINT preboundSheet
) {
	if(vertexData->SheetCount == 0 || vertexData->TotalVertexCount == 0)
		return preboundSheet;
	
	UINT maxVertexCount = m_vertexBufferSize / sizeof(FW1_GLYPHVERTEX);
	
	UINT currentSheet = 0;
	UINT activeSheet = preboundSheet;
	UINT currentVertex = 0;
	UINT nextSheetStart = vertexData->pVertexCounts[0];
	
	while(currentVertex < vertexData->TotalVertexCount) {
		// Fill the vertex buffer
		UINT vertexCount = std::min(vertexData->TotalVertexCount - currentVertex, maxVertexCount);
		
		D3D11_MAPPED_SUBRESOURCE msr;
		HRESULT hResult = pContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
		if(SUCCEEDED(hResult)) {
			CopyMemory(msr.pData, vertexData->pVertices + currentVertex, vertexCount * sizeof(FW1_GLYPHVERTEX));
			
			pContext->Unmap(m_pVertexBuffer, 0);
			
			// Draw all glyphs in the buffer
			UINT drawnVertices = 0;
			while(drawnVertices < vertexCount) {
				while(currentVertex >= nextSheetStart) {
					++currentSheet;
					nextSheetStart += vertexData->pVertexCounts[currentSheet];
				}
				
				if(currentSheet != activeSheet) {
					// Bind sheet shader resources
					pGlyphAtlas->BindSheet(pContext, currentSheet, 0);
					activeSheet = currentSheet;
				}
				
				UINT drawCount = std::min(vertexCount - drawnVertices, nextSheetStart - currentVertex);
				pContext->Draw(drawCount, drawnVertices);
				
				drawnVertices += drawCount;
				currentVertex += drawCount;
			}
		}
		else
			break;
	}
	
	return activeSheet;
}


// Draw vertices as quads
UINT CFW1GlyphVertexDrawer::drawGlyphsAsQuads(
	ID3D11DeviceContext *pContext,
	IFW1GlyphAtlas *pGlyphAtlas,
	const FW1_VERTEXDATA *vertexData,
	UINT preboundSheet
) {
	if(vertexData->SheetCount == 0 || vertexData->TotalVertexCount == 0)
		return preboundSheet;
	
	UINT maxVertexCount = m_vertexBufferSize / sizeof(QuadVertex);
	if(maxVertexCount > 4 * (m_maxIndexCount / 6))
		maxVertexCount = 4 * (m_maxIndexCount / 6);
	if(maxVertexCount % 4 != 0)
		maxVertexCount -= (maxVertexCount % 4);
	
	UINT currentSheet = 0;
	UINT activeSheet = preboundSheet;
	const FW1_GLYPHCOORDS *sheetGlyphCoords = 0;
	if(activeSheet < vertexData->SheetCount)
		sheetGlyphCoords = pGlyphAtlas->GetGlyphCoords(activeSheet);
	UINT currentVertex = 0;
	UINT nextSheetStart = vertexData->pVertexCounts[0];
	
	while(currentVertex < vertexData->TotalVertexCount) {
		// Fill the vertex buffer
		UINT vertexCount = std::min((vertexData->TotalVertexCount - currentVertex) * 4, maxVertexCount);
		
		D3D11_MAPPED_SUBRESOURCE msr;
		HRESULT hResult = pContext->Map(m_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
		if(SUCCEEDED(hResult)) {
			QuadVertex *bufferVertices = static_cast<QuadVertex*>(msr.pData);
			
			// Convert to quads when filling the buffer
			UINT savedCurrentSheet = currentSheet;
			UINT savedActiveSheet = activeSheet;
			UINT savedNextSheetStart = nextSheetStart;
			UINT savedCurrentVertex = currentVertex;
			
			UINT drawnVertices = 0;
			while(drawnVertices < vertexCount) {
				while(currentVertex >= nextSheetStart) {
					++currentSheet;
					nextSheetStart += vertexData->pVertexCounts[currentSheet];
				}
				
				if(currentSheet != activeSheet) {
					sheetGlyphCoords = pGlyphAtlas->GetGlyphCoords(currentSheet);
					activeSheet = currentSheet;
				}
				
				UINT drawCount = std::min(vertexCount - drawnVertices, (nextSheetStart - currentVertex) * 4);
				
				for(UINT i=0; i < drawCount/4; ++i) {
					FW1_GLYPHVERTEX glyphVertex = vertexData->pVertices[currentVertex + i];
					glyphVertex.PositionX = glyphVertex.PositionX;
					glyphVertex.PositionY = glyphVertex.PositionY;
					
					const FW1_GLYPHCOORDS &glyphCoords = sheetGlyphCoords[glyphVertex.GlyphIndex];
					
					QuadVertex quadVertex;
					
					quadVertex.color = glyphVertex.GlyphColor;
					
					quadVertex.positionX = glyphVertex.PositionX + glyphCoords.PositionLeft;
					quadVertex.positionY = glyphVertex.PositionY + glyphCoords.PositionTop;
					quadVertex.texCoordX = glyphCoords.TexCoordLeft;
					quadVertex.texCoordY = glyphCoords.TexCoordTop;
					bufferVertices[drawnVertices + i*4 + 0] = quadVertex;
					
					quadVertex.positionX = glyphVertex.PositionX + glyphCoords.PositionRight;
					quadVertex.texCoordX = glyphCoords.TexCoordRight;
					bufferVertices[drawnVertices + i*4 + 1] = quadVertex;
					
					quadVertex.positionY = glyphVertex.PositionY + glyphCoords.PositionBottom;
					quadVertex.texCoordY = glyphCoords.TexCoordBottom;
					bufferVertices[drawnVertices + i*4 + 3] = quadVertex;
					
					quadVertex.positionX = glyphVertex.PositionX + glyphCoords.PositionLeft;
					quadVertex.texCoordX = glyphCoords.TexCoordLeft;
					bufferVertices[drawnVertices + i*4 + 2] = quadVertex;
				}
				
				drawnVertices += drawCount;
				currentVertex += drawCount / 4;
			}
			
			pContext->Unmap(m_pVertexBuffer, 0);
			
			// Draw all glyphs in the buffer
			currentSheet = savedCurrentSheet;
			activeSheet = savedActiveSheet;
			nextSheetStart = savedNextSheetStart;
			currentVertex = savedCurrentVertex;
			
			drawnVertices = 0;
			while(drawnVertices < vertexCount) {
				while(currentVertex >= nextSheetStart) {
					++currentSheet;
					nextSheetStart += vertexData->pVertexCounts[currentSheet];
				}
				
				if(currentSheet != activeSheet) {
					// Bind sheet shader resources
					pGlyphAtlas->BindSheet(pContext, currentSheet, FW1_NOGEOMETRYSHADER);
					activeSheet = currentSheet;
				}
				
				UINT drawCount = std::min(vertexCount - drawnVertices, (nextSheetStart - currentVertex) * 4);
				pContext->DrawIndexed((drawCount/2)*3, 0, drawnVertices);
				
				drawnVertices += drawCount;
				currentVertex += drawCount / 4;
			}
		}
		else
			break;
	}
	
	return activeSheet;
}


}// namespace FW1FontWrapper
