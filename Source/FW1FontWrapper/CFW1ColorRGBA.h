// CFW1ColorRGBA.h

#ifndef IncludeGuard__FW1_CFW1ColorRGBA
#define IncludeGuard__FW1_CFW1ColorRGBA

#include "CFW1Object.h"


namespace FW1FontWrapper {


// A color
class CFW1ColorRGBA : public CFW1Object<IFW1ColorRGBA> {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
		
		// IFW1Color32
		virtual void STDMETHODCALLTYPE SetColor(UINT32 Color);
		virtual void STDMETHODCALLTYPE SetColor(FLOAT Red, FLOAT Green, FLOAT Blue, FLOAT Alpha);
		virtual void STDMETHODCALLTYPE SetColor(const FLOAT *pColor);
		virtual void STDMETHODCALLTYPE SetColor(const BYTE *pColor);
		
		virtual UINT32 STDMETHODCALLTYPE GetColor32();
	
	// Public functions
	public:
		CFW1ColorRGBA();
		
		HRESULT initColor(IFW1Factory *pFW1Factory, UINT32 initialColor32);
	
	// Internal functions
	private:
		virtual ~CFW1ColorRGBA();
	
	// Internal data
	private:
		UINT32						m_color32;
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1ColorRGBA
