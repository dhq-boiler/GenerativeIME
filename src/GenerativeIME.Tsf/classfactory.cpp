#include "classfactory.h"
#include "textservice.h"
#include <new>

CClassFactory::CClassFactory()
    : m_cRef(1)
{
    InterlockedIncrement(&g_cRefDll);
}

CClassFactory::~CClassFactory()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
    {
        *ppvObj = static_cast<IClassFactory*>(this);
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CClassFactory::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CClassFactory::Release()
{
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) delete this;
    return cr;
}

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

    auto pSvc = new(std::nothrow) CTextService();
    if (pSvc == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = pSvc->QueryInterface(riid, ppvObj);
    pSvc->Release();
    return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
        InterlockedIncrement(&g_cRefDll);
    else
        InterlockedDecrement(&g_cRefDll);
    return S_OK;
}
