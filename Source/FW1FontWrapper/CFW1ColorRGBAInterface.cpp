// CFW1ColorRGBAInterface.cpp

#include "FW1Precompiled.h"

#include "CFW1ColorRGBA.h"


namespace FW1FontWrapper {


// Query interface
HRESULT STDMETHODCALLTYPE CFW1ColorRGBA::QueryInterface(REFIID riid, void **ppvObject) {
	if(ppvObject == NULL)
		return E_INVALIDARG;
	
	if(IsEqualIID(riid, __uuidof(IFW1ColorRGBA))) {
		*ppvObject = static_cast<IFW1ColorRGBA*>(this);
		AddRef();
		return S_OK;
	}
	
	return CFW1Object::QueryInterface(riid, ppvObject);
}


// Set the color
void STDMETHODCALLTYPE CFW1ColorRGBA::SetColor(UINT32 Color) {
	m_color32 = Color;
}


// Set the color
void STDMETHODCALLTYPE CFW1ColorRGBA::SetColor(FLOAT Red, FLOAT Green, FLOAT Blue, FLOAT Alpha) {
	UINT32 color32;
	BYTE *colorBytes = reinterpret_cast<BYTE*>(&color32);
	colorBytes[0] = static_cast<BYTE>(Red * 255.0f + 0.5f);
	colorBytes[1] = static_cast<BYTE>(Green * 255.0f + 0.5f);
	colorBytes[2] = static_cast<BYTE>(Blue * 255.0f + 0.5f);
	colorBytes[3] = static_cast<BYTE>(Alpha * 255.0f + 0.5f);
	
	m_color32 = color32;
}


// Set the color
void STDMETHODCALLTYPE CFW1ColorRGBA::SetColor(const FLOAT *pColor) {
	SetColor(pColor[0], pColor[1], pColor[2], pColor[3]);
}


// Set the color
void STDMETHODCALLTYPE CFW1ColorRGBA::SetColor(const BYTE *pColor) {
	UINT32 color32;
	BYTE *colorBytes = reinterpret_cast<BYTE*>(&color32);
	for(int i=0; i < 4; ++i)
		colorBytes[i] = pColor[i];
	
	m_color32 = color32;
}


// Get the color
UINT32 STDMETHODCALLTYPE CFW1ColorRGBA::GetColor32() {
	return m_color32;
}


}// namespace FW1FontWrapper
