// CFW1GlyphSheetInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1GlyphSheet.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1GlyphSheet::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1GlyphSheet))) {
		*ppvObject = static_cast<IFW1GlyphSheet*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Get the D3D11 device used by this sheet
HRESULT STDMETHODCALLTYPE CFW1GlyphSheet::GetDevice(ID3D11Device **ppDevice) {
	if(ppDevice == NULL)
		return E_INVALIDARG;
	
	m_pDevice->AddRef();
	*ppDevice = m_pDevice;
	
	return S_OK;
}


// Get sheet desc
void STDMETHODCALLTYPE CFW1GlyphSheet::GetDesc(FW1_GLYPHSHEETDESC *pDesc) {
	pDesc->GlyphCount = m_glyphCount;
	pDesc->Width = m_sheetWidth;
	pDesc->Height = m_sheetHeight;
	pDesc->MipLevels = m_mipLevelCount;
}


// Get the sheet texture
HRESULT STDMETHODCALLTYPE CFW1GlyphSheet::GetSheetTexture(ID3D11ShaderResourceView **ppSheetTextureSRV) {
	if(ppSheetTextureSRV == NULL)
		return E_INVALIDARG;
	
	m_pTextureSRV->AddRef();
	*ppSheetTextureSRV = m_pTextureSRV;
	
	return S_OK;
}


// Get the glyph coordinate buffer
HRESULT STDMETHODCALLTYPE CFW1GlyphSheet::GetCoordBuffer(ID3D11ShaderResourceView **ppCoordBufferSRV) {
	if(ppCoordBufferSRV == NULL)
		return E_INVALIDARG;
	
	if(m_pCoordBufferSRV != NULL)
		m_pCoordBufferSRV->AddRef();
	*ppCoordBufferSRV = m_pCoordBufferSRV;
	
	return S_OK;
}


// Get glyph coordinate array for all glyphs in the sheet
const FW1_GLYPHCOORDS* STDMETHODCALLTYPE CFW1GlyphSheet::GetGlyphCoords() {
	return m_glyphCoords;
}


// Set sheet shader resources
HRESULT STDMETHODCALLTYPE CFW1GlyphSheet::BindSheet(ID3D11DeviceContext *pContext, UINT Flags) {
	pContext->PSSetShaderResources(0, 1, &m_pTextureSRV);
	if((Flags & FW1_NOGEOMETRYSHADER) == 0 && m_hardwareCoordBuffer)
		pContext->GSSetShaderResources(0, 1, &m_pCoordBufferSRV);
	
	return S_OK;
}


// Insert a new glyph in the sheet
UINT STDMETHODCALLTYPE CFW1GlyphSheet::InsertGlyph(
	const FW1_GLYPHMETRICS *pGlyphMetrics,
	LPCVOID pGlyphData,
	UINT RowPitch,
	UINT PixelStride
) {
	if(m_closed)
		return 0xffffffff;
	if(m_glyphCount >= m_maxGlyphCount)
		return 0xffffffff;
	
	CriticalSectionLock lock(&m_sheetCriticalSection);
	
	if(m_closed)
		return 0xffffffff;
	if(m_glyphCount >= m_maxGlyphCount)
		return 0xffffffff;
	
	const UINT &width = pGlyphMetrics->Width;
	const UINT &height = pGlyphMetrics->Height;
	
	// Position the glyph if it fits
	UINT blockWidth = width / m_alignWidth + 1;
	if(width % m_alignWidth != 0)
		++blockWidth;
	UINT blockHeight = height / m_alignWidth + 1;
	if(height % m_alignWidth != 0)
		++blockHeight;
	
	UINT blockX;
	UINT blockY;
	UINT positionX;
	UINT positionY;
	
	if(m_glyphCount > 0 || !m_allowOversizedGlyph) {
		if(m_alignWidth + width + m_alignWidth > m_sheetWidth)
			return 0xffffffff;
		
		// Position the glyph at the lowest Y possible (fills reasonably well with different sized glyphs)
		blockX = m_heightRange->findMin(blockWidth, &blockY);
		
		positionX = m_alignWidth + blockX * m_alignWidth;
		positionY = m_alignWidth + blockY * m_alignWidth;
		
		if(positionY + height + m_alignWidth > m_sheetHeight)
			return 0xffffffff;
	}
	else {
		blockX = 0;
		blockY = 0;
		
		positionX = m_alignWidth;
		positionY = m_alignWidth;
	}
	
	m_heightRange->update(blockX, blockWidth, blockY + blockHeight);
	
	// Store glyph coordinates
	FLOAT coordOffset = static_cast<FLOAT>(m_alignWidth) * 0.5f;
	
	UINT alignedWidth = (blockWidth - 1) * m_alignWidth;
	UINT alignedHeight = (blockHeight - 1) * m_alignWidth;
	
	FW1_GLYPHCOORDS glyphCoords;
	glyphCoords.TexCoordLeft = (static_cast<FLOAT>(positionX) - coordOffset) / static_cast<FLOAT>(m_sheetWidth);
	glyphCoords.TexCoordTop = (static_cast<FLOAT>(positionY) - coordOffset) / static_cast<FLOAT>(m_sheetHeight);
	glyphCoords.TexCoordRight =
		(static_cast<FLOAT>(positionX + alignedWidth) + coordOffset) / static_cast<FLOAT>(m_sheetWidth);
	glyphCoords.TexCoordBottom =
		(static_cast<FLOAT>(positionY + alignedHeight) + coordOffset) / static_cast<FLOAT>(m_sheetHeight);
	glyphCoords.PositionLeft = pGlyphMetrics->OffsetX - coordOffset;
	glyphCoords.PositionTop = pGlyphMetrics->OffsetY - coordOffset;
	glyphCoords.PositionRight = pGlyphMetrics->OffsetX + static_cast<FLOAT>(alignedWidth) + coordOffset;
	glyphCoords.PositionBottom = pGlyphMetrics->OffsetY + static_cast<FLOAT>(alignedHeight) + coordOffset;
	
	UINT glyphIndex = m_glyphCount;
	
	m_glyphCoords[glyphIndex] = glyphCoords;
	
	// Glyph pixels
	for(UINT i=0; i < height && i < m_sheetHeight-positionY; ++i) {
		const UINT8 *src = static_cast<const UINT8*>(pGlyphData) + i*RowPitch;
		UINT8 *dst = m_textureData + (positionY+i)*m_sheetWidth + positionX;
		for(UINT j=0; j < width && j < m_sheetWidth-positionX; ++j)
			dst[j] = src[j*PixelStride];
	}
	
	// Update dirty rect to be flushed to device texture
	if(m_updatedGlyphCount == 0) {
		m_dirtyRect.left = positionX - m_alignWidth;
		m_dirtyRect.top = positionY - m_alignWidth;
		m_dirtyRect.right = std::min(positionX + width + m_alignWidth, m_sheetWidth);
		m_dirtyRect.bottom = std::min(positionY + height + m_alignWidth, m_sheetHeight);
	}
	else {
		m_dirtyRect.left = std::min(m_dirtyRect.left, positionX - m_alignWidth);
		m_dirtyRect.top = std::min(m_dirtyRect.top, positionY - m_alignWidth);
		m_dirtyRect.right = std::min(std::max(m_dirtyRect.right, positionX + width + m_alignWidth), m_sheetWidth);
		m_dirtyRect.bottom = std::min(std::max(m_dirtyRect.bottom, positionY + height + m_alignWidth), m_sheetHeight);
	}
	
	_WriteBarrier();
	MemoryBarrier();
	
	++m_glyphCount;
	++m_updatedGlyphCount;
	
	return glyphIndex;
}


// Disallow insertion of additional glyphs in this sheet
void STDMETHODCALLTYPE CFW1GlyphSheet::CloseSheet() {
	EnterCriticalSection(&m_sheetCriticalSection);
	m_closed = true;
	LeaveCriticalSection(&m_sheetCriticalSection);
}


// Flush any inserted glyphs
void STDMETHODCALLTYPE CFW1GlyphSheet::Flush(ID3D11DeviceContext *pContext) {
	EnterCriticalSection(&m_flushCriticalSection);
	if(!m_static) {
		EnterCriticalSection(&m_sheetCriticalSection);
		
		UINT glyphCount = m_glyphCount;
		RectUI dirtyRect = m_dirtyRect;
		
		UINT updatedGlyphCount = m_updatedGlyphCount;
		m_updatedGlyphCount = 0;
		
		if(m_closed)
			m_static = true;
		
		LeaveCriticalSection(&m_sheetCriticalSection);
		
		if(updatedGlyphCount > 0) {
			// Update coord buffer
			if(m_hardwareCoordBuffer) {
				UINT startIndex = glyphCount - updatedGlyphCount;
				UINT endIndex = glyphCount;
				
				D3D11_BOX dstBox;
				ZeroMemory(&dstBox, sizeof(dstBox));
				dstBox.left = startIndex * sizeof(FW1_GLYPHCOORDS);
				dstBox.right = endIndex * sizeof(FW1_GLYPHCOORDS);
				dstBox.top = 0;
				dstBox.bottom = 1;
				dstBox.front = 0;
				dstBox.back = 1;
				
				pContext->UpdateSubresource(
					m_pCoordBuffer,
					0,
					&dstBox,
					m_glyphCoords + startIndex,
					0,
					0
				);
			}
			
			// Update texture
			if(dirtyRect.right > dirtyRect.left && dirtyRect.bottom > dirtyRect.top) {
				UINT8 *srcMem = m_textureData;
				
				D3D11_BOX dstBox;
				ZeroMemory(&dstBox, sizeof(dstBox));
				dstBox.left = dirtyRect.left;
				dstBox.right = dirtyRect.right;
				dstBox.top = dirtyRect.top;
				dstBox.bottom = dirtyRect.bottom;
				dstBox.front = 0;
				dstBox.back = 1;
				
				// Update each mip-level
				for(UINT i=0; i < m_mipLevelCount; ++i) {
					pContext->UpdateSubresource(
						m_pTexture,
						D3D11CalcSubresource(i, 0, m_mipLevelCount),
						&dstBox,
						srcMem + dstBox.top * (m_sheetWidth >> i) + dstBox.left,
						m_sheetWidth >> i,
						0
					);
					
					if(i+1 < m_mipLevelCount) {
						UINT8 *nextMip = srcMem + (m_sheetWidth >> i) * (m_sheetHeight >> i);
						
						dstBox.left >>= 1;
						dstBox.right >>= 1;
						dstBox.top >>= 1;
						dstBox.bottom >>= 1;
						
						// Calculate the next mip-level for the current dirty-rect
						for(UINT j = dstBox.top; j < dstBox.bottom; ++j) {
							const UINT8 *src0 = srcMem + j * 2 * (m_sheetWidth >> i);
							const UINT8 *src1 = src0 + (m_sheetWidth >> i);
							UINT8 *dst = nextMip + j * (m_sheetWidth >> (i+1));
							
							for(UINT k = dstBox.left; k < dstBox.right; ++k) {
								UINT src = src0[k*2] + src0[k*2+1] + src1[k*2] + src1[k*2+1];
								dst[k] = static_cast<UINT8>(src >> 2);
							}
						}
						
						srcMem = nextMip;
					}
				}
			}
		}
		
		// This sheet is now static, save some memory
		if(m_static) {
			delete[] m_textureData;
			m_textureData = 0;
		}
	}
	
	LeaveCriticalSection(&m_flushCriticalSection);
}


}// namespace FW1FontWrapper
