// CFW1Object.h

#ifndef IncludeGuard__FW1_CFW1Object
#define IncludeGuard__FW1_CFW1Object


namespace FW1FontWrapper {


// Helper baseclass to avoid writing IUnknown and IFW1Object implementations once per class
template<class IBase>
class CFW1Object : public IBase {
	public:
		// IUnknown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0 {
			if(ppvObject == NULL)
				return E_INVALIDARG;
			
			if(IsEqualIID(riid, __uuidof(IUnknown))) {
				*ppvObject = static_cast<IUnknown*>(this);
				AddRef();
				return S_OK;
			}
			else if(IsEqualIID(riid, __uuidof(IFW1Object))) {
				*ppvObject = static_cast<IFW1Object*>(this);
				AddRef();
				return S_OK;
			}
			
			*ppvObject = NULL;
			return E_NOINTERFACE;
		}
		
		virtual ULONG STDMETHODCALLTYPE AddRef() {
			return static_cast<ULONG>(InterlockedIncrement(reinterpret_cast<LONG*>(&m_cRefCount)));
		}
		
		virtual ULONG STDMETHODCALLTYPE Release() {
			ULONG newCount = static_cast<ULONG>(InterlockedDecrement(reinterpret_cast<LONG*>(&m_cRefCount)));
			
			if(newCount == 0)
				delete this;
			
			return newCount;
		}
		
		// IFW1Object
		virtual HRESULT STDMETHODCALLTYPE GetFactory(IFW1Factory **ppFW1Factory) {
			if(ppFW1Factory == NULL)
				return E_INVALIDARG;
			
			m_pFW1Factory->AddRef();
			*ppFW1Factory = m_pFW1Factory;
			
			return S_OK;
		}
	
	// Internal functions
	protected:
		CFW1Object() :
			m_cRefCount(1),
			
			m_pFW1Factory(NULL)
		{
		}
		
		virtual ~CFW1Object() {
			if(m_pFW1Factory != NULL)
				m_pFW1Factory->Release();
		}
		
		HRESULT initBaseObject(IFW1Factory *pFW1Factory) {
			if(pFW1Factory == NULL)
				return E_INVALIDARG;
			
			pFW1Factory->AddRef();
			m_pFW1Factory = pFW1Factory;
			
			return S_OK;
		}
	
	// Internal data
	protected:
		IFW1Factory					*m_pFW1Factory;
	
	private:
		ULONG						m_cRefCount;
	
	private:
		CFW1Object(const CFW1Object&);
		CFW1Object& operator=(const CFW1Object&);
};


}// namespace FW1FontWrapper


#endif// IncludeGuard__FW1_CFW1Object
