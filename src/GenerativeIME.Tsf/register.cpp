#include "register.h"
#include <msctf.h>
#include <strsafe.h>

namespace
{
    constexpr wchar_t kClsidKeyFormat[]      = L"CLSID\\{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}";
    constexpr wchar_t kInprocServer32[]      = L"InprocServer32";
    constexpr wchar_t kThreadingModel[]      = L"ThreadingModel";
    constexpr wchar_t kThreadingApartment[]  = L"Apartment";

    HRESULT GuidToRegPath(REFGUID g, wchar_t* buf, size_t cch)
    {
        return StringCchPrintfW(
            buf, cch, kClsidKeyFormat,
            g.Data1, g.Data2, g.Data3,
            g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
            g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    }

    LSTATUS RegSetStringW(HKEY hKey, LPCWSTR name, LPCWSTR value)
    {
        return ::RegSetValueExW(hKey, name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value),
            static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    }
}

// -- COM in-proc server registration --------------------------------------

HRESULT RegisterComServer()
{
    wchar_t modulePath[MAX_PATH] = { 0 };
    if (::GetModuleFileNameW(g_hInst, modulePath, MAX_PATH) == 0)
        return HRESULT_FROM_WIN32(::GetLastError());

    wchar_t keyPath[128] = { 0 };
    if (FAILED(GuidToRegPath(c_clsidGenerativeImeTextService, keyPath, ARRAYSIZE(keyPath))))
        return E_FAIL;

    HKEY hKeyClsid = nullptr;
    if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0,
            KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKeyClsid, nullptr) != ERROR_SUCCESS)
        return E_FAIL;

    RegSetStringW(hKeyClsid, nullptr, c_szTextServiceDesc);

    HKEY hKeyInproc = nullptr;
    if (::RegCreateKeyExW(hKeyClsid, kInprocServer32, 0, nullptr, 0,
            KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hKeyInproc, nullptr) == ERROR_SUCCESS)
    {
        RegSetStringW(hKeyInproc, nullptr, modulePath);
        RegSetStringW(hKeyInproc, kThreadingModel, kThreadingApartment);
        ::RegCloseKey(hKeyInproc);
    }

    ::RegCloseKey(hKeyClsid);
    return S_OK;
}

HRESULT UnregisterComServer()
{
    wchar_t keyPath[128] = { 0 };
    if (FAILED(GuidToRegPath(c_clsidGenerativeImeTextService, keyPath, ARRAYSIZE(keyPath))))
        return E_FAIL;

    wchar_t inprocPath[200] = { 0 };
    StringCchCopyW(inprocPath, ARRAYSIZE(inprocPath), keyPath);
    StringCchCatW(inprocPath, ARRAYSIZE(inprocPath), L"\\");
    StringCchCatW(inprocPath, ARRAYSIZE(inprocPath), kInprocServer32);

    ::RegDeleteKeyExW(HKEY_CLASSES_ROOT, inprocPath, KEY_WOW64_64KEY, 0);
    ::RegDeleteKeyExW(HKEY_CLASSES_ROOT, keyPath,    KEY_WOW64_64KEY, 0);
    return S_OK;
}

// -- TSF profile registration ---------------------------------------------

HRESULT RegisterProfile()
{
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&pProfiles));
    if (FAILED(hr)) return hr;

    hr = pProfiles->Register(c_clsidGenerativeImeTextService);
    if (FAILED(hr)) { pProfiles->Release(); return hr; }

    wchar_t modulePath[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(g_hInst, modulePath, MAX_PATH);

    hr = pProfiles->AddLanguageProfile(
        c_clsidGenerativeImeTextService,
        c_langIdJapanese,
        c_guidGenerativeImeProfile,
        c_szTextServiceDesc,
        static_cast<ULONG>(wcslen(c_szTextServiceDesc)),
        modulePath,
        static_cast<ULONG>(wcslen(modulePath)),
        0); // icon index

    pProfiles->Release();
    return hr;
}

HRESULT UnregisterProfile()
{
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
        reinterpret_cast<void**>(&pProfiles));
    if (FAILED(hr)) return hr;

    hr = pProfiles->Unregister(c_clsidGenerativeImeTextService);
    pProfiles->Release();
    return hr;
}

// -- TSF category registration --------------------------------------------

HRESULT RegisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&pCategoryMgr));
    if (FAILED(hr)) return hr;

    const GUID* kCategories[] = {
        &GUID_TFCAT_TIP_KEYBOARD,
        &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
        // IMMERSIVESUPPORT: required for Win11's Input Indicator to render
        // both our branding icon AND the mode "あ/A" pill in the system tray.
        // Without this category the shell treats us as a "legacy" TIP and
        // only shows the language abbreviation ("日本") instead.
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        &GUID_TFCAT_TIPCAP_COMLESS,
        &GUID_TFCAT_TIPCAP_WOW16,
    };

    for (const GUID* cat : kCategories)
    {
        hr = pCategoryMgr->RegisterCategory(
            c_clsidGenerativeImeTextService, *cat, c_clsidGenerativeImeTextService);
        if (FAILED(hr)) break;
    }

    pCategoryMgr->Release();
    return hr;
}

HRESULT UnregisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
        reinterpret_cast<void**>(&pCategoryMgr));
    if (FAILED(hr)) return hr;

    const GUID* kCategories[] = {
        &GUID_TFCAT_TIP_KEYBOARD,
        &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
        // IMMERSIVESUPPORT: required for Win11's Input Indicator to render
        // both our branding icon AND the mode "あ/A" pill in the system tray.
        // Without this category the shell treats us as a "legacy" TIP and
        // only shows the language abbreviation ("日本") instead.
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        &GUID_TFCAT_TIPCAP_COMLESS,
        &GUID_TFCAT_TIPCAP_WOW16,
    };

    for (const GUID* cat : kCategories)
    {
        pCategoryMgr->UnregisterCategory(
            c_clsidGenerativeImeTextService, *cat, c_clsidGenerativeImeTextService);
    }

    pCategoryMgr->Release();
    return S_OK;
}
