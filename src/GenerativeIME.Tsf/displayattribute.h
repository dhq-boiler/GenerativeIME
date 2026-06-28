#pragma once

#include "globals.h"

// ITfDisplayAttributeInfo for the composition's "input" text (under-construction
// text the user is still typing). We only expose one display attribute for now;
// converted/target-clause variants can be added later if/when conversion lands.
class CDisplayAttributeInfoInput : public ITfDisplayAttributeInfo
{
public:
    CDisplayAttributeInfoInput();

    // IUnknown
    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    // ITfDisplayAttributeInfo
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* ptfDisplayAttr) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* ptfDisplayAttr) override;
    STDMETHODIMP Reset() override;

    static const TF_DISPLAYATTRIBUTE s_defaultAttribute;

private:
    ~CDisplayAttributeInfoInput();
    LONG m_cRef;
};

// Phase B "focused bunsetsu" attribute. Renders with a thicker squiggle-like
// underline against the input attribute's flat blue line, so the user can see
// which clause Tab is currently parked on while the rest of a multi-bunsetsu
// composition keeps the normal input underline.
class CDisplayAttributeInfoBunsetsuFocus : public ITfDisplayAttributeInfo
{
public:
    CDisplayAttributeInfoBunsetsuFocus();

    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* ptfDisplayAttr) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* ptfDisplayAttr) override;
    STDMETHODIMP Reset() override;

    static const TF_DISPLAYATTRIBUTE s_defaultAttribute;

private:
    ~CDisplayAttributeInfoBunsetsuFocus();
    LONG m_cRef;
};

// IEnumTfDisplayAttributeInfo returned from CTextService::EnumDisplayAttributeInfo.
// Enumerates the Input attribute and the BunsetsuFocus attribute (in that
// order).
class CEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo
{
public:
    CEnumDisplayAttributeInfo();

    // IUnknown
    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    // IEnumTfDisplayAttributeInfo
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP Next(ULONG ulCount, ITfDisplayAttributeInfo** rgInfo, ULONG* pcFetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG ulCount) override;

private:
    ~CEnumDisplayAttributeInfo();
    LONG m_cRef;
    LONG m_index; // 0 = Input not returned yet, 1 = BunsetsuFocus next, 2 = done
};
