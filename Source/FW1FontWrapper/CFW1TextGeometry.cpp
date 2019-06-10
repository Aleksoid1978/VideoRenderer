// CFW1TextGeometry.cpp

#include "FW1Precompiled.h"

#include "CFW1TextGeometry.h"


namespace FW1FontWrapper {


// Construct
CFW1TextGeometry::CFW1TextGeometry() :
	m_maxSheetIndex(0),
	m_sorted(false)
{
}


// Destruct
CFW1TextGeometry::~CFW1TextGeometry() {
}


// Init glyph provider
HRESULT CFW1TextGeometry::initTextGeometry(IFW1Factory *pFW1Factory) {
	HRESULT hResult = initBaseObject(pFW1Factory);
	if(FAILED(hResult))
		return hResult;
	
	return hResult;
}


}// namespace FW1FontWrapper
