// CFW1TextGeometryInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1TextGeometry.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1TextGeometry::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1TextGeometry))) {
		*ppvObject = static_cast<IFW1TextGeometry*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Clear geometry
void STDMETHODCALLTYPE CFW1TextGeometry::Clear() {
	m_vertices.clear();
	m_maxSheetIndex = 0;
	
	m_sorted = false;
}


// Add a vertex
void STDMETHODCALLTYPE CFW1TextGeometry::AddGlyphVertex(const FW1_GLYPHVERTEX *pVertex) {
	m_vertices.push_back(*pVertex);
	
	UINT sheetIndex = pVertex->GlyphIndex >> 16;
	m_maxSheetIndex = std::max(m_maxSheetIndex, sheetIndex);
	
	m_sorted = false;
}


// Get current glyph vertices
FW1_VERTEXDATA STDMETHODCALLTYPE CFW1TextGeometry::GetGlyphVerticesTemp() {
	FW1_VERTEXDATA vertexData;
	
	if(!m_vertices.empty()) {
		UINT32 sheetCount = m_maxSheetIndex + 1;
		
		// Sort and prepare vertices
		if(!m_sorted) {
			m_sortedVertices.resize(m_vertices.size());
			m_vertexCounts.resize(sheetCount);
			m_vertexStartIndices.resize(sheetCount);
			
			std::fill(m_vertexCounts.begin(), m_vertexCounts.end(), 0);
			
			UINT * const vertexCounts = &m_vertexCounts[0];
			const FW1_GLYPHVERTEX * const vertices = &m_vertices[0];
			const UINT32 vertexCount = static_cast<UINT32>(m_vertices.size());
			
			for(UINT32 i=0; i < vertexCount; ++i) {
				UINT32 sheetIndex = vertices[i].GlyphIndex >> 16;
				
				++vertexCounts[sheetIndex];
			}
			
			UINT * const vertexStartIndices = &m_vertexStartIndices[0];
			
			UINT currentStartIndex = 0;
			for(UINT32 i=0; i < sheetCount; ++i) {
				vertexStartIndices[i] = currentStartIndex;
				
				currentStartIndex += vertexCounts[i];
			}
			
			FW1_GLYPHVERTEX * const sortedVertices = &m_sortedVertices[0];
			
			for(UINT32 i=0; i < vertexCount; ++i) {
				const FW1_GLYPHVERTEX &vertex = vertices[i];
				UINT32 sheetIndex = vertex.GlyphIndex >> 16;
				
				UINT &vertexIndex = vertexStartIndices[sheetIndex];
				
				sortedVertices[vertexIndex] = vertex;
				sortedVertices[vertexIndex].GlyphIndex &= 0xffff;
				
				++vertexIndex;
			}
			
			m_sorted = true;
		}
		
		vertexData.SheetCount = sheetCount;
		vertexData.pVertexCounts = &m_vertexCounts[0];
		vertexData.TotalVertexCount = static_cast<UINT>(m_vertices.size());
		vertexData.pVertices = &m_sortedVertices[0];
	}
	else {
		vertexData.SheetCount = 0;
		vertexData.pVertexCounts = 0;
		vertexData.TotalVertexCount = 0;
		vertexData.pVertices = 0;
	}
	
	return vertexData;
}


}// namespace FW1FontWrapper
