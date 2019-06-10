// CFW1ColorRGBA.cpp

#include "FW1Precompiled.h"

#include "CFW1ColorRGBA.h"


namespace FW1FontWrapper {


// Construct
CFW1ColorRGBA::CFW1ColorRGBA() :
	m_color32(0xffffffff)
{
}


// Destruct
CFW1ColorRGBA::~CFW1ColorRGBA() {
}


// Init
HRESULT CFW1ColorRGBA::initColor(IFW1Factory *pFW1Factory, UINT32 initialColor32) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	m_color32 = initialColor32;
	
	return S_OK;
}


}// namespace FW1FontWrapper
