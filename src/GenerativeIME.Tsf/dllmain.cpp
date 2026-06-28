#include "globals.h"
#include "classfactory.h"
#include "register.h"
#include <new>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_cRefDll <= 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (ppv == nullptr) return E_INVALIDARG;
    *ppv = nullptr;

    if (!IsEqualCLSID(rclsid, c_clsidGenerativeImeTextService))
        return CLASS_E_CLASSNOTAVAILABLE;

    CClassFactory* pFactory = new (std::nothrow) CClassFactory();
    if (pFactory == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllRegisterServer(void)
{
    HRESULT hr = RegisterComServer();
    if (FAILED(hr)) return hr;

    hr = RegisterProfile();
    if (FAILED(hr)) { UnregisterComServer(); return hr; }

    hr = RegisterCategories();
    if (FAILED(hr)) { UnregisterProfile(); UnregisterComServer(); return hr; }

    return S_OK;
}

STDAPI DllUnregisterServer(void)
{
    UnregisterCategories();
    UnregisterProfile();
    UnregisterComServer();
    return S_OK;
}
