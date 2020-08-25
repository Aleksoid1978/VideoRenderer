/*
 * (C) 2018-2020 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include <InitGuid.h>
#include "VideoRenderer.h"
#include "PropPage.h"

#include "../external/minhook/include/MinHook.h"

template <class T>
static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
	*phr = S_OK;
	CUnknown* punk = new(std::nothrow) T(lpunk, phr);
	if (punk == nullptr) {
		*phr = E_OUTOFMEMORY;
	}
	return punk;
}

const AMOVIESETUP_PIN sudpPins[] = {
	{L"Input", FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, std::size(sudPinTypesIn), sudPinTypesIn},
};

const AMOVIESETUP_FILTER sudFilter[] = {
	{&__uuidof(CMpcVideoRenderer), L"MPC Video Renderer", MERIT_DO_NOT_USE, std::size(sudpPins), sudpPins, CLSID_LegacyAmFilterCategory},
};

CFactoryTemplate g_Templates[] = {
	{sudFilter[0].strName, &__uuidof(CMpcVideoRenderer), CreateInstance<CMpcVideoRenderer>, nullptr, &sudFilter[0]},
	{L"MainProp", &__uuidof(CVRMainPPage), CreateInstance<CVRMainPPage>, nullptr, nullptr},
	{L"InfoProp", &__uuidof(CVRInfoPPage), CreateInstance<CVRInfoPPage>, nullptr, nullptr}
};

int g_cTemplates = std::size(g_Templates);

STDAPI DllRegisterServer()
{
	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
	return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE hDllHandle, DWORD dwReason, LPVOID pReserved)
{
	switch (dwReason) {
		case DLL_PROCESS_ATTACH:
			MH_Initialize();
			break;
		case DLL_PROCESS_DETACH:
			MH_Uninitialize();
			break;
	}
	return DllEntryPoint(hDllHandle, dwReason, pReserved);
}

void CALLBACK OpenConfiguration(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow)
{
	HRESULT hr = S_OK;
	CUnknown *pInstance = CreateInstance<CMpcVideoRenderer>(nullptr, &hr);
	IBaseFilter *pFilter = nullptr;
	pInstance->NonDelegatingQueryInterface(IID_IBaseFilter, (void **)&pFilter);
	if (pFilter) {
		pFilter->AddRef();

		CoInitialize(nullptr);

		// Get PropertyPages interface
		ISpecifyPropertyPages *pProp = nullptr;
		HRESULT hr = pFilter->QueryInterface<ISpecifyPropertyPages>(&pProp);
		if (SUCCEEDED(hr) && pProp)
		{
			// Get the filter's name and IUnknown pointer.
			FILTER_INFO FilterInfo;
			hr = pFilter->QueryFilterInfo(&FilterInfo);
			// We don't need the graph, so don't sit on a ref to it
			if (FilterInfo.pGraph)
				FilterInfo.pGraph->Release();

			IUnknown *pFilterUnk = nullptr;
			pFilter->QueryInterface<IUnknown>(&pFilterUnk);

			// Show the page.
			CAUUID caGUID;
			pProp->GetPages(&caGUID);
			pProp->Release();
			hr = OleCreatePropertyFrame(
				nullptr,            // Parent window
				0, 0,               // Reserved
				FilterInfo.achName, // Caption for the dialog box
				1,                  // Number of objects (just the filter)
				&pFilterUnk,        // Array of object pointers.
				caGUID.cElems,      // Number of property pages
				caGUID.pElems,      // Array of property page CLSIDs
				0,                  // Locale identifier
				0, nullptr          // Reserved
			);

			// Clean up.
			pFilterUnk->Release();
			CoTaskMemFree(caGUID.pElems);

			hr = S_OK;
		}
		CoUninitialize();
	}
	delete pInstance;
}
