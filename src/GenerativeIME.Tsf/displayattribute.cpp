#include "displayattribute.h"
#include <oleauto.h>

// Default rendering for "input" (composition / under-construction) text.
// Solid blue underline, default text color, no background fill, marked as input clause.
const TF_DISPLAYATTRIBUTE CDisplayAttributeInfoInput::s_defaultAttribute =
{
    {TF_CT_NONE, 0}, // text color (app default)
    {TF_CT_NONE, 0}, // background (app default)
    TF_LS_SOLID, // underline style
    FALSE, // underline boldness
    {TF_CT_COLORREF, RGB(0, 0, 255)}, // underline color
    TF_ATTR_INPUT // attribute info
};

CDisplayAttributeInfoInput::CDisplayAttributeInfoInput()
    : m_cRef(1)
{
    InterlockedIncrement(&g_cRefDll);
}

CDisplayAttributeInfoInput::~CDisplayAttributeInfoInput()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CDisplayAttributeInfoInput::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo))
    {
        *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfoInput::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfoInput::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CDisplayAttributeInfoInput::GetGUID(GUID* pguid)
{
    if (!pguid) return E_INVALIDARG;
    *pguid = c_guidDisplayAttributeInput;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfoInput::GetDescription(BSTR* pbstrDesc)
{
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(L"GenerativeIME Display Attribute (Input)");
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CDisplayAttributeInfoInput::GetAttributeInfo(TF_DISPLAYATTRIBUTE* ptfDisplayAttr)
{
    if (!ptfDisplayAttr) return E_INVALIDARG;
    *ptfDisplayAttr = s_defaultAttribute;
    return S_OK;
}

// User customization is intentionally unsupported; we always render with the default.
STDMETHODIMP CDisplayAttributeInfoInput::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* /*ptfDisplayAttr*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP CDisplayAttributeInfoInput::Reset()
{
    return S_OK;
}

// ---------------------------------------------------------------------------

// Phase B focused-bunsetsu: thicker squiggly underline + warmer color so the
// focused clause stands out against the plain-blue input underline used on
// the rest of the composition.
const TF_DISPLAYATTRIBUTE CDisplayAttributeInfoBunsetsuFocus::s_defaultAttribute =
{
    {TF_CT_NONE, 0},
    {TF_CT_NONE, 0},
    TF_LS_SQUIGGLE,
    TRUE,
    {TF_CT_COLORREF, RGB(220, 80, 0)},
    TF_ATTR_TARGET_CONVERTED
};

CDisplayAttributeInfoBunsetsuFocus::CDisplayAttributeInfoBunsetsuFocus()
    : m_cRef(1)
{
    InterlockedIncrement(&g_cRefDll);
}

CDisplayAttributeInfoBunsetsuFocus::~CDisplayAttributeInfoBunsetsuFocus()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfDisplayAttributeInfo))
    {
        *ppvObj = static_cast<ITfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfoBunsetsuFocus::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfoBunsetsuFocus::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::GetGUID(GUID* pguid)
{
    if (!pguid) return E_INVALIDARG;
    *pguid = c_guidDisplayAttributeBunsetsuFocus;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::GetDescription(BSTR* pbstrDesc)
{
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(L"GenerativeIME Display Attribute (Bunsetsu Focus)");
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::GetAttributeInfo(TF_DISPLAYATTRIBUTE* ptfDisplayAttr)
{
    if (!ptfDisplayAttr) return E_INVALIDARG;
    *ptfDisplayAttr = s_defaultAttribute;
    return S_OK;
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::SetAttributeInfo(const TF_DISPLAYATTRIBUTE* /*ptfDisplayAttr*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP CDisplayAttributeInfoBunsetsuFocus::Reset()
{
    return S_OK;
}

// ---------------------------------------------------------------------------

CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo()
    : m_cRef(1)
      , m_index(0)
{
    InterlockedIncrement(&g_cRefDll);
}

CEnumDisplayAttributeInfo::~CEnumDisplayAttributeInfo()
{
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CEnumDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfDisplayAttributeInfo))
    {
        *ppvObj = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo** ppEnum)
{
    if (!ppEnum) return E_INVALIDARG;
    auto pClone = new CEnumDisplayAttributeInfo();
    if (!pClone) return E_OUTOFMEMORY;
    pClone->m_index = m_index;
    *ppEnum = pClone;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched)
{
    if (ulCount > 0 && !rgInfo) return E_INVALIDARG;

    ULONG fetched = 0;
    while (fetched < ulCount && m_index < 2)
    {
        ITfDisplayAttributeInfo* p = nullptr;
        switch (m_index)
        {
        case 0: p = new CDisplayAttributeInfoInput();
            break;
        case 1: p = new CDisplayAttributeInfoBunsetsuFocus();
            break;
        }
        if (!p) return E_OUTOFMEMORY;
        rgInfo[fetched++] = p;
        m_index++;
    }

    if (pcFetched) *pcFetched = fetched;
    return (fetched == ulCount) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Reset()
{
    m_index = 0;
    return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG ulCount)
{
    m_index += static_cast<LONG>(ulCount);
    if (m_index > 2) m_index = 2;
    return S_OK;
}
