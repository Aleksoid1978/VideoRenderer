// CFW1Factory.cpp

#include "FW1Precompiled.h"

#include "CFW1Factory.h"


namespace FW1FontWrapper {


// Construct
CFW1Factory::CFW1Factory() :
	m_cRefCount(1)
{
	InitializeCriticalSection(&m_errorStringCriticalSection);
}


// Destruct
CFW1Factory::~CFW1Factory() {
	DeleteCriticalSection(&m_errorStringCriticalSection);
}


// Init
HRESULT CFW1Factory::initFactory() {
	return S_OK;
}


// Create a DWrite factory
HRESULT CFW1Factory::createDWriteFactory(IDWriteFactory **ppDWriteFactory) {
	HRESULT hResult = E_FAIL;
	
	typedef HRESULT (WINAPI * PFN_DWRITECREATEFACTORY)(__in DWRITE_FACTORY_TYPE factoryType, __in REFIID iid, __out IUnknown **factory);
	PFN_DWRITECREATEFACTORY pfnDWriteCreateFactory = NULL;
	
#ifdef FW1_DELAYLOAD_DWRITE_DLL
	HMODULE hDWriteLib = LoadLibrary(TEXT("DWrite.dll"));
	if(hDWriteLib == NULL) {
		DWORD dwErr = GetLastError();
		dwErr;
		setErrorString(L"Failed to load DWrite.dll");
	}
	else {
		pfnDWriteCreateFactory =
			reinterpret_cast<PFN_DWRITECREATEFACTORY>(GetProcAddress(hDWriteLib, "DWriteCreateFactory"));
		if(pfnDWriteCreateFactory == NULL) {
			DWORD dwErr = GetLastError();
			dwErr;
			setErrorString(L"Failed to load DWriteCreateFactory");
		}
	}
#else
	pfnDWriteCreateFactory = DWriteCreateFactory;
#endif
	
	if(pfnDWriteCreateFactory != NULL) {
		IDWriteFactory *pDWriteFactory;
		
		hResult = pfnDWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&pDWriteFactory)
		);
		if(FAILED(hResult)) {
			setErrorString(L"DWriteCreateFactory failed");
		}
		else {
			*ppDWriteFactory = pDWriteFactory;
				
			hResult = S_OK;
		}
	}
	
	return hResult;
}


// Set error string
void CFW1Factory::setErrorString(const wchar_t *str) {
	EnterCriticalSection(&m_errorStringCriticalSection);
	m_lastError = str;
	LeaveCriticalSection(&m_errorStringCriticalSection);
}


}// namespace FW1FontWrapper
