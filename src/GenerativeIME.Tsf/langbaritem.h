#pragma once

#include "globals.h"

class CTextService;

// LangBar mode button — exposes the "あ" / "A" indicator that the Win11
// Input Indicator (system tray) reads to know our IME has a mode UI and is
// fully TSF-aware. Without registering at least one LangBar item the tray
// just shows the language abbreviation ("日本") instead of the IME branding.
//
// Clicking the button toggles IME ON/OFF via CTextService::ToggleImeFromUI.
class CLangBarItemButton
    : public ITfLangBarItemButton
    , public ITfSource
{
public:
    explicit CLangBarItemButton(CTextService* pService);

    // IUnknown
    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    // Called by CTextService when OPENCLOSE compartment or shaping mode
    // changes so the tray button can re-render with the new mode glyph.
    // Reads current state from m_pService — we don't cache it here because
    // two copies of "is the IME on?" drift apart in subtle ways.
    void UpdateMode();

private:
    ~CLangBarItemButton();

    LONG                m_cRef;
    TF_LANGBARITEMINFO  m_info;
    ITfLangBarItemSink* m_pSink;
    DWORD               m_dwSinkCookie;
    CTextService*       m_pService; // weak ref; owner outlives this button
};
