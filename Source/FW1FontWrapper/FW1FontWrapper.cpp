// FW1FontWrapper.cpp

#include "FW1Precompiled.h"

#include "CFW1Factory.h"

#ifndef FW1_DELAYLOAD_DWRITE_DLL
	#pragma comment (lib, "DWrite.lib")
#endif

#ifndef FW1_DELAYLOAD_D3DCOMPILER_XX_DLL
	#pragma comment (lib, "DWrite.lib")
#endif

#ifdef FW1_COMPILETODLL
	#ifndef _M_X64
		#pragma comment (linker, "/EXPORT:FW1CreateFactory=_FW1CreateFactory@8,@1")
	#endif
#endif


// Create FW1 factory
extern HRESULT STDMETHODCALLTYPE FW1CreateFactory(UINT32 Version, IFW1Factory **ppFactory) {
	if(Version != FW1_VERSION)
		return E_FAIL;
	
	if(ppFactory == NULL)
		return E_INVALIDARG;
	
	FW1FontWrapper::CFW1Factory *pFactory = new FW1FontWrapper::CFW1Factory;
	HRESULT hResult = pFactory->initFactory();
	if(FAILED(hResult)) {
		pFactory->Release();
	}
	else {
		*ppFactory = pFactory;
		
		hResult = S_OK;
	}
	
	return hResult;
}
