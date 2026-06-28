#pragma once

#include "globals.h"
#include <msctf.h>
#include <string>

class CTextService;

enum class EditAction
{
    StartAndUpdate, // start composition + set text
    Update,         // update existing composition text
    EndCommit,      // end composition keeping current text
    EndCancel       // end composition with empty text
};

// Read-only session: queries the composition range's screen rect so the
// candidate window can be anchored under the actual text being composed,
// not just the (often-unavailable) Win32 caret.
class CGetRectSession : public ITfEditSession
{
public:
    CGetRectSession(ITfContext* ctx, ITfComposition* comp, POINT* outPos);

    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

private:
    ~CGetRectSession();
    LONG            m_cRef;
    ITfContext*     m_ctx;
    ITfComposition* m_comp;
    POINT*          m_outPos; // caller-owned; we write the bottom-left of the range
};

class CEditSession : public ITfEditSession
{
public:
    CEditSession(CTextService* pService, ITfContext* pContext, EditAction action, const std::wstring& text);

    // IUnknown
    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

private:
    ~CEditSession();

    HRESULT DoStartAndUpdate(TfEditCookie ec);
    HRESULT DoUpdate(TfEditCookie ec);
    HRESULT DoEnd(TfEditCookie ec, bool cancel);

    LONG          m_cRef;
    CTextService* m_pService;   // weak ref; owns this session's lifetime indirectly via RequestEditSession
    ITfContext*   m_pContext;   // strong ref
    EditAction    m_action;
    std::wstring  m_text;
};
