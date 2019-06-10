// CFW1FactoryInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1Factory.h"

#include "CFW1FontWrapper.h"
#include "CFW1GlyphVertexDrawer.h"
#include "CFW1GlyphRenderStates.h"
#include "CFW1TextRenderer.h"
#include "CFW1TextGeometry.h"
#include "CFW1GlyphProvider.h"
#include "CFW1DWriteRenderTarget.h"
#include "CFW1GlyphAtlas.h"
#include "CFW1GlyphSheet.h"
#include "CFW1ColorRGBA.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1Factory::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IUnknown))) {
		*ppvObject = static_cast<IUnknown*>(this);
		AddRef();
		return S_OK;
	}
	else if(IsEqualIID(riid, __uuidof(IFW1Factory))) {
		*ppvObject = static_cast<IFW1Factory*>(this);
		AddRef();
		return S_OK;
	}
	
	*ppvObject = NULL;
	return E_NOINTERFACE;
}


// Add reference
ULONG STDMETHODCALLTYPE CFW1Factory::AddRef() {
	return static_cast<ULONG>(InterlockedIncrement(reinterpret_cast<LONG*>(&m_cRefCount)));
}


// Release
ULONG STDMETHODCALLTYPE CFW1Factory::Release() {
	ULONG newCount = static_cast<ULONG>(InterlockedDecrement(reinterpret_cast<LONG*>(&m_cRefCount)));
	
	if(newCount == 0)
		delete this;
	
	return newCount;
}


// Create font wrapper with default settings
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateFontWrapper(
	ID3D11Device *pDevice,
	LPCWSTR pszFontFamily,
	IFW1FontWrapper **ppFontWrapper
) {
	FW1_FONTWRAPPERCREATEPARAMS createParams;
	ZeroMemory(&createParams, sizeof(createParams));
	
	createParams.GlyphSheetWidth = 512;
	createParams.GlyphSheetHeight = 512;
	createParams.MaxGlyphCountPerSheet = 2048;
	createParams.SheetMipLevels = 1;
	createParams.AnisotropicFiltering = TRUE;//FALSE;
	createParams.MaxGlyphWidth = 384;
	createParams.MaxGlyphHeight = 384;
	createParams.DisableGeometryShader = FALSE;
	createParams.VertexBufferSize = 0;
	createParams.DefaultFontParams.pszFontFamily = pszFontFamily;
	createParams.DefaultFontParams.FontWeight = DWRITE_FONT_WEIGHT_NORMAL;
	createParams.DefaultFontParams.FontStyle = DWRITE_FONT_STYLE_NORMAL;
	createParams.DefaultFontParams.FontStretch = DWRITE_FONT_STRETCH_NORMAL;
	createParams.DefaultFontParams.pszLocale = L"";
	
	return CreateFontWrapper(pDevice, NULL, &createParams, ppFontWrapper);
}


// Create font wrapper
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateFontWrapper(
	ID3D11Device *pDevice,
	IDWriteFactory *pDWriteFactory,
	const FW1_FONTWRAPPERCREATEPARAMS *pCreateParams,
	IFW1FontWrapper **ppFontWrapper
) {
	if(pCreateParams == NULL || ppFontWrapper == NULL)
		return E_INVALIDARG;
	
	HRESULT hResult;
	
	// If no DWrite factory is provided, attempt to create one
	if(pDWriteFactory == NULL)
		hResult = createDWriteFactory(&pDWriteFactory);
	else {
		pDWriteFactory->AddRef();
		hResult = S_OK;
	}
	if(FAILED(hResult)) {
	}
	else {
		// Get system font collection
		IDWriteFontCollection *pFontCollection;
		
		hResult = pDWriteFactory->GetSystemFontCollection(&pFontCollection, FALSE);
		if(FAILED(hResult)) {
			setErrorString(L"GetSystemFontCollection failed");
		}
		else {
			// Create glyph atlas
			IFW1GlyphAtlas *pGlyphAtlas;
			
			hResult = CreateGlyphAtlas(
				pDevice,
				pCreateParams->GlyphSheetWidth,
				pCreateParams->GlyphSheetHeight,
				(pCreateParams->DisableGeometryShader == FALSE) ? TRUE : FALSE,
				TRUE,
				pCreateParams->MaxGlyphCountPerSheet,
				pCreateParams->SheetMipLevels,
				4096,
				&pGlyphAtlas
			);
			if(FAILED(hResult)) {
			}
			else {
				// Create glyph provider
				IFW1GlyphProvider *pGlyphProvider;
				
				hResult = CreateGlyphProvider(
					pGlyphAtlas,
					pDWriteFactory,
					pFontCollection,
					pCreateParams->MaxGlyphWidth,
					pCreateParams->MaxGlyphHeight,
					&pGlyphProvider
				);
				if(FAILED(hResult)) {
				}
				else {
					// Create glyph vertex drawer
					IFW1GlyphVertexDrawer *pGlyphVertexDrawer;
					
					hResult = CreateGlyphVertexDrawer(
						pDevice,
						pCreateParams->VertexBufferSize,
						&pGlyphVertexDrawer
					);
					if(FAILED(hResult)) {
					}
					else {
						// Create glyph render states
						IFW1GlyphRenderStates *pGlyphRenderStates;
						
						hResult = CreateGlyphRenderStates(
							pDevice,
							pCreateParams->DisableGeometryShader,
							pCreateParams->AnisotropicFiltering,
							&pGlyphRenderStates
						);
						if(FAILED(hResult)) {
						}
						else {
							// Create font wrapper
							IFW1FontWrapper *pFontWrapper;
							
							hResult = CreateFontWrapper(
								pDevice,
								pGlyphAtlas,
								pGlyphProvider,
								pGlyphVertexDrawer,
								pGlyphRenderStates,
								pDWriteFactory,
								&pCreateParams->DefaultFontParams,
								&pFontWrapper
							);
							if(FAILED(hResult)) {
							}
							else {
								// Success
								*ppFontWrapper = pFontWrapper;
								
								hResult = S_OK;
							}
							
							pGlyphRenderStates->Release();
						}
						
						pGlyphVertexDrawer->Release();
					}
					
					pGlyphProvider->Release();
				}
				
				pGlyphAtlas->Release();
			}
			
			pFontCollection->Release();
		}
		
		pDWriteFactory->Release();
	}
	
	return hResult;
}


// Create font wrapper
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateFontWrapper(
	ID3D11Device *pDevice,
	IFW1GlyphAtlas *pGlyphAtlas,
	IFW1GlyphProvider *pGlyphProvider,
	IFW1GlyphVertexDrawer *pGlyphVertexDrawer,
	IFW1GlyphRenderStates *pGlyphRenderStates,
	IDWriteFactory *pDWriteFactory,
	const FW1_DWRITEFONTPARAMS *pDefaultFontParams,
	IFW1FontWrapper **ppFontWrapper
) {
	if(ppFontWrapper == NULL)
		return E_INVALIDARG;
	
	CFW1FontWrapper *pFontWrapper = new CFW1FontWrapper;
	HRESULT hResult = pFontWrapper->initFontWrapper(
		this,
		pDevice,
		pGlyphAtlas,
		pGlyphProvider,
		pGlyphVertexDrawer,
		pGlyphRenderStates,
		pDWriteFactory,
		pDefaultFontParams
	);
	if(FAILED(hResult)) {
		pFontWrapper->Release();
		setErrorString(L"initFontWrapper failed");
	}
	else {
		*ppFontWrapper = pFontWrapper;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create glyph vertex drawer
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateGlyphVertexDrawer(
	ID3D11Device *pDevice,
	UINT VertexBufferSize,
	IFW1GlyphVertexDrawer **ppGlyphVertexDrawer
) {
	if(ppGlyphVertexDrawer == NULL)
		return E_INVALIDARG;
	
	CFW1GlyphVertexDrawer *pGlyphVertexDrawer = new CFW1GlyphVertexDrawer;
	HRESULT hResult = pGlyphVertexDrawer->initVertexDrawer(
		this,
		pDevice,
		VertexBufferSize
	);
	if(FAILED(hResult)) {
		pGlyphVertexDrawer->Release();
		setErrorString(L"initVertexDrawer failed");
	}
	else {
		*ppGlyphVertexDrawer = pGlyphVertexDrawer;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create glyph render states
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateGlyphRenderStates(
	ID3D11Device *pDevice,
	BOOL DisableGeometryShader,
	BOOL AnisotropicFiltering,
	IFW1GlyphRenderStates **ppGlyphRenderStates
) {
	if(ppGlyphRenderStates == NULL)
		return E_INVALIDARG;
	
	CFW1GlyphRenderStates *pGlyphRenderStates = new CFW1GlyphRenderStates;
	HRESULT hResult = pGlyphRenderStates->initRenderResources(
		this,
		pDevice,
		(DisableGeometryShader == FALSE),
		(AnisotropicFiltering != FALSE)
	);
	if(FAILED(hResult)) {
		pGlyphRenderStates->Release();
		setErrorString(L"initRenderResources failed");
	}
	else {
		*ppGlyphRenderStates = pGlyphRenderStates;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create DWrite text renderer
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateTextRenderer(
	IFW1GlyphProvider *pGlyphProvider,
	IFW1TextRenderer **ppTextRenderer
) {
	if(ppTextRenderer == NULL)
		return E_INVALIDARG;
	
	CFW1TextRenderer *pTextRenderer = new CFW1TextRenderer;
	HRESULT hResult = pTextRenderer->initTextRenderer(this, pGlyphProvider);
	if(FAILED(hResult)) {
		pTextRenderer->Release();
		setErrorString(L"initTextRenderer failed");
	}
	else {
		*ppTextRenderer = pTextRenderer;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create text geometry
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateTextGeometry(
	IFW1TextGeometry **ppTextGeometry
) {
	if(ppTextGeometry == NULL)
		return E_INVALIDARG;
	
	CFW1TextGeometry *pTextGeometry = new CFW1TextGeometry;
	HRESULT hResult = pTextGeometry->initTextGeometry(this);
	if(FAILED(hResult)) {
		pTextGeometry->Release();
		setErrorString(L"initTextGeometry failed");
	}
	else {
		*ppTextGeometry = pTextGeometry;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create glyph provider
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateGlyphProvider(
	IFW1GlyphAtlas *pGlyphAtlas,
	IDWriteFactory *pDWriteFactory,
	IDWriteFontCollection *pFontCollection,
	UINT MaxGlyphWidth,
	UINT MaxGlyphHeight,
	IFW1GlyphProvider **ppGlyphProvider
) {
	if(ppGlyphProvider == NULL)
		return E_INVALIDARG;
	
	CFW1GlyphProvider *pGlyphProvider = new CFW1GlyphProvider;
	HRESULT hResult = pGlyphProvider->initGlyphProvider(
		this,
		pGlyphAtlas,
		pDWriteFactory,
		pFontCollection,
		MaxGlyphWidth,
		MaxGlyphHeight
	);
	if(FAILED(hResult)) {
		pGlyphProvider->Release();
		setErrorString(L"initGlyphProvider failed");
	}
	else {
		*ppGlyphProvider = pGlyphProvider;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create DWrite render target
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateDWriteRenderTarget(
	IDWriteFactory *pDWriteFactory,
	UINT RenderTargetWidth,
	UINT RenderTargetHeight,
	IFW1DWriteRenderTarget **ppRenderTarget
) {
	if(ppRenderTarget == NULL)
		return E_INVALIDARG;
	
	CFW1DWriteRenderTarget *pRenderTarget = new CFW1DWriteRenderTarget;
	HRESULT hResult = pRenderTarget->initRenderTarget(this, pDWriteFactory, RenderTargetWidth, RenderTargetHeight);
	if(FAILED(hResult)) {
		pRenderTarget->Release();
		setErrorString(L"initRenderTarget failed");
	}
	else {
		*ppRenderTarget = pRenderTarget;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create glyph atlas
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateGlyphAtlas(
	ID3D11Device *pDevice,
	UINT GlyphSheetWidth,
	UINT GlyphSheetHeight,
	BOOL HardwareCoordBuffer,
	BOOL AllowOversizedGlyph,
	UINT MaxGlyphCountPerSheet,
	UINT MipLevels,
	UINT MaxGlyphSheetCount,
	IFW1GlyphAtlas **ppGlyphAtlas
) {
	if(ppGlyphAtlas == NULL)
		return E_INVALIDARG;
	
	CFW1GlyphAtlas *pGlyphAtlas = new CFW1GlyphAtlas;
	HRESULT hResult = pGlyphAtlas->initGlyphAtlas(
		this,
		pDevice,
		GlyphSheetWidth,
		GlyphSheetHeight,
		(HardwareCoordBuffer != FALSE),
		(AllowOversizedGlyph != FALSE),
		MaxGlyphCountPerSheet,
		MipLevels,
		MaxGlyphSheetCount
	);
	if(FAILED(hResult)) {
		pGlyphAtlas->Release();
		setErrorString(L"initGlyphAtlas failed");
	}
	else {
		*ppGlyphAtlas = pGlyphAtlas;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create glyph sheet
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateGlyphSheet(
	ID3D11Device *pDevice,
	UINT GlyphSheetWidth,
	UINT GlyphSheetHeight,
	BOOL HardwareCoordBuffer,
	BOOL AllowOversizedGlyph,
	UINT MaxGlyphCount,
	UINT MipLevels,
	IFW1GlyphSheet **ppGlyphSheet
) {
	if(ppGlyphSheet == NULL)
		return E_INVALIDARG;
	
	CFW1GlyphSheet *pGlyphSheet = new CFW1GlyphSheet;
	HRESULT hResult = pGlyphSheet->initGlyphSheet(
		this,
		pDevice,
		GlyphSheetWidth,
		GlyphSheetHeight,
		(HardwareCoordBuffer != FALSE),
		(AllowOversizedGlyph != FALSE),
		MaxGlyphCount,
		MipLevels
	);
	if(FAILED(hResult)) {
		pGlyphSheet->Release();
		setErrorString(L"initGlyphSheet failed");
	}
	else {
		*ppGlyphSheet = pGlyphSheet;
		
		hResult = S_OK;
	}
	
	return hResult;
}


// Create color
HRESULT STDMETHODCALLTYPE CFW1Factory::CreateColor(UINT32 Color, IFW1ColorRGBA **ppColor) {
	if(ppColor == NULL)
		return E_INVALIDARG;
	
	CFW1ColorRGBA *pColor = new CFW1ColorRGBA;
	HRESULT hResult = pColor->initColor(this, Color);
	if(FAILED(hResult)) {
		pColor->Release();
		setErrorString(L"initColor failed");
	}
	else {
		*ppColor = pColor;
		
		hResult = S_OK;
	}
	
	return hResult;
}


}// namespace FW1FontWrapper
