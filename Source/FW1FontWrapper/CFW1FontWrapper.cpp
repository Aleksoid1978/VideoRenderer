// CFW1FontWrapper.cpp

#include "FW1Precompiled.h"

#include "CFW1FontWrapper.h"

#define SAFE_RELEASE(pObject) { if(pObject) { (pObject)->Release(); (pObject) = NULL; } }


namespace FW1FontWrapper {


// Construct
CFW1FontWrapper::CFW1FontWrapper() :
	m_pDevice(NULL),
	m_featureLevel(D3D_FEATURE_LEVEL_9_1),
	m_pDWriteFactory(NULL),
	
	m_pGlyphAtlas(NULL),
	m_pGlyphProvider(NULL),
	
	m_pGlyphRenderStates(NULL),
	m_pGlyphVertexDrawer(NULL),
	
	m_defaultTextInited(false),
	m_pDefaultTextFormat(NULL)
{
	InitializeCriticalSection(&m_textRenderersCriticalSection);
	InitializeCriticalSection(&m_textGeometriesCriticalSection);
}


// Destruct
CFW1FontWrapper::~CFW1FontWrapper() {
	SAFE_RELEASE(m_pFW1Factory);
	
	SAFE_RELEASE(m_pDevice);
	SAFE_RELEASE(m_pDWriteFactory);
	
	SAFE_RELEASE(m_pGlyphAtlas);
	SAFE_RELEASE(m_pGlyphProvider);
	
	SAFE_RELEASE(m_pGlyphRenderStates);
	SAFE_RELEASE(m_pGlyphVertexDrawer);
	
	while(!m_textRenderers.empty()) {
		m_textRenderers.top()->Release();
		m_textRenderers.pop();
	}
	
	while(!m_textGeometries.empty()) {
		m_textGeometries.top()->Release();
		m_textGeometries.pop();
	}
	
	SAFE_RELEASE(m_pDefaultTextFormat);
	
	DeleteCriticalSection(&m_textRenderersCriticalSection);
	DeleteCriticalSection(&m_textGeometriesCriticalSection);
}


// Init
HRESULT CFW1FontWrapper::initFontWrapper(
	IFW1Factory *pFW1Factory,
	ID3D11Device *pDevice,
	IFW1GlyphAtlas *pGlyphAtlas,
	IFW1GlyphProvider *pGlyphProvider,
	IFW1GlyphVertexDrawer *pGlyphVertexDrawer,
	IFW1GlyphRenderStates *pGlyphRenderStates,
	IDWriteFactory *pDWriteFactory,
	const FW1_DWRITEFONTPARAMS *pDefaultFontParams
) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	if(
		pDevice == NULL ||
		pGlyphAtlas == NULL ||
		pGlyphProvider == NULL ||
		pGlyphVertexDrawer == NULL ||
		pGlyphRenderStates == NULL ||
		pDWriteFactory == NULL
	)
		return E_INVALIDARG;
	
	pDevice->AddRef();
	m_pDevice = pDevice;
	m_featureLevel = m_pDevice->GetFeatureLevel();
	
	pDWriteFactory->AddRef();
	m_pDWriteFactory = pDWriteFactory;
	
	pGlyphAtlas->AddRef();
	m_pGlyphAtlas = pGlyphAtlas;
	pGlyphProvider->AddRef();
	m_pGlyphProvider = pGlyphProvider;
	
	pGlyphRenderStates->AddRef();
	m_pGlyphRenderStates = pGlyphRenderStates;
	pGlyphVertexDrawer->AddRef();
	m_pGlyphVertexDrawer = pGlyphVertexDrawer;
	
	// Create default text format for strings, if provided
	if(pDefaultFontParams->pszFontFamily != NULL && pDefaultFontParams->pszFontFamily[0] != 0) {
		IDWriteTextFormat *pTextFormat;
		hResult = m_pDWriteFactory->CreateTextFormat(
			pDefaultFontParams->pszFontFamily,
			NULL,
			pDefaultFontParams->FontWeight,
			pDefaultFontParams->FontStyle,
			pDefaultFontParams->FontStretch,
			32.0f,
			(pDefaultFontParams->pszLocale != NULL) ? pDefaultFontParams->pszLocale : L"",
			&pTextFormat
		);
		if(FAILED(hResult)) {
			m_lastError = L"Failed to create DWrite text format";
		}
		else {
			m_pDefaultTextFormat = pTextFormat;
			m_defaultTextInited = true;
			
			hResult = S_OK;
		}
	}
	
	return hResult;
}


// Create text layout from string
IDWriteTextLayout* CFW1FontWrapper::createTextLayout(
	const WCHAR *pszString,
	const WCHAR *pszFontFamily,
	FLOAT fontSize,
	const FW1_RECTF *pLayoutRect,
	UINT flags
) {
	if(m_defaultTextInited) {
		UINT32 stringLength = 0;
		while(pszString[stringLength] != 0)
			++stringLength;
		
		// Create DWrite text layout for the string
		IDWriteTextLayout *pTextLayout;
		HRESULT hResult = m_pDWriteFactory->CreateTextLayout(
			pszString,
			stringLength,
			m_pDefaultTextFormat,
			pLayoutRect->Right - pLayoutRect->Left,
			pLayoutRect->Bottom - pLayoutRect->Top,
			&pTextLayout
		);
		if(SUCCEEDED(hResult)) {
			// Layout settings
			DWRITE_TEXT_RANGE allText = {0, stringLength};
			pTextLayout->SetFontSize(fontSize, allText);
			
			if(pszFontFamily != NULL)
				pTextLayout->SetFontFamilyName(pszFontFamily, allText);
			
			if((flags & FW1_NOWORDWRAP) != 0)
				pTextLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			
			if(flags & FW1_RIGHT)
				pTextLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
			else if(flags & FW1_CENTER)
				pTextLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			if(flags & FW1_BOTTOM)
				pTextLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
			else if(flags & FW1_VCENTER)
				pTextLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			
			return pTextLayout;
		}
	}
	
	return NULL;
}


}// namespace FW1FontWrapper
