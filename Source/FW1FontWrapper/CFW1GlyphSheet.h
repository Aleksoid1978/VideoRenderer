// CFW1GlyphSheet.h

#ifndef IncludeGuard__FW1_CFW1GlyphSheet
#define IncludeGuard__FW1_CFW1GlyphSheet

#include "CFW1Object.h"


namespace FW1FontWrapper {


// A texture containing multiple glyphimages
class CFW1GlyphSheet : public CFW1Object<IFW1GlyphSheet> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1GlyphSheet
		virtual HRESULT STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice);
		virtual void STDMETHODCALLTYPE GetDesc(FW1_GLYPHSHEETDESC *pDesc);
		
		virtual HRESULT STDMETHODCALLTYPE GetSheetTexture(ID3D11ShaderResourceView **ppSheetTextureSRV);
		virtual HRESULT STDMETHODCALLTYPE GetCoordBuffer(ID3D11ShaderResourceView **ppCoordBufferSRV);
		
		virtual const FW1_GLYPHCOORDS* STDMETHODCALLTYPE GetGlyphCoords();
		virtual HRESULT STDMETHODCALLTYPE BindSheet(ID3D11DeviceContext *pContext, UINT Flags);
		
		virtual UINT STDMETHODCALLTYPE InsertGlyph(
			const FW1_GLYPHMETRICS *pGlyphMetrics,
			const void *pGlyphData,
			UINT RowPitch,
			UINT PixelStride
		);
		virtual void STDMETHODCALLTYPE CloseSheet();
		virtual void STDMETHODCALLTYPE Flush(ID3D11DeviceContext *pContext);
	
	// Public functions
	public:
		CFW1GlyphSheet();
		
		HRESULT initGlyphSheet(
			IFW1Factory *pFW1Factory,
			ID3D11Device *pDevice,
			UINT sheetWidth,
			UINT sheetHeight,
			bool coordBuffer,
			bool allowOversizedGlyph,
			UINT maxGlyphCount,
			UINT mipLevelCount
		);
	
	// Internal types
	private:
		struct RectUI {
			UINT					left;
			UINT					top;
			UINT					right;
			UINT					bottom;
		};
		
		class HeightRange {
			public:
				HeightRange(UINT totalWidth);
				~HeightRange();
				
				UINT findMin(UINT width, UINT *outMin);
				void update(UINT startX, UINT width, UINT newHeight);
			
			private:
				HeightRange();
				HeightRange(const HeightRange&);
				HeightRange& operator=(const HeightRange&);
				
				UINT findMax(UINT startX, UINT width);
				
				UINT				*m_heights;
				UINT				m_totalWidth;
		};
		
		class CriticalSectionLock {
			public:
				CriticalSectionLock(LPCRITICAL_SECTION pCriticalSection) : m_pCriticalSection(pCriticalSection) {
					EnterCriticalSection(m_pCriticalSection);
				}
				~CriticalSectionLock() {
					LeaveCriticalSection(m_pCriticalSection);
				}
			
			private:
				CriticalSectionLock();
				CriticalSectionLock(const CriticalSectionLock&);
				CriticalSectionLock& operator=(const CriticalSectionLock&);
				
				LPCRITICAL_SECTION	m_pCriticalSection;
		};
	
	// Internal functions
	private:
		virtual ~CFW1GlyphSheet();
		
		HRESULT createDeviceResources();
	
	// Internal data
	private:
		std::wstring				m_lastError;
		
		UINT						m_sheetWidth;
		UINT						m_sheetHeight;
		bool						m_hardwareCoordBuffer;
		bool						m_allowOversizedGlyph;
		UINT						m_mipLevelCount;
		UINT						m_alignWidth;
		
		UINT8						*m_textureData;
		FW1_GLYPHCOORDS				*m_glyphCoords;
		UINT						m_maxGlyphCount;
		UINT						m_glyphCount;
		
		ID3D11Device				*m_pDevice;
		
		ID3D11Texture2D				*m_pTexture;
		ID3D11ShaderResourceView	*m_pTextureSRV;
		ID3D11Buffer				*m_pCoordBuffer;
		ID3D11ShaderResourceView	*m_pCoordBufferSRV;
		
		bool						m_closed;
		bool						m_static;
		
		HeightRange					*m_heightRange;
		
		UINT						m_updatedGlyphCount;
		RectUI						m_dirtyRect;
		CRITICAL_SECTION			m_sheetCriticalSection;
		CRITICAL_SECTION			m_flushCriticalSection;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1GlyphSheet
