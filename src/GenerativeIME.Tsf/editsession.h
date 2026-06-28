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

    // Phase B: ask the session to split the composition's display attribute
    // along bunsetsu boundaries when it writes the text. `focusedStart` and
    // `focusedLen` are character offsets into `text`; that range gets the
    // BunsetsuFocus attribute and everything else keeps the plain Input
    // attribute. Pass both as 0 (or omit this call) to use the legacy
    // single-attribute path.
    void SetBunsetsuFocus(size_t focusedStart, size_t focusedLen)
    {
        m_focusedStart = focusedStart;
        m_focusedLen   = focusedLen;
        m_hasFocus     = (focusedLen > 0);
    }

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
    size_t        m_focusedStart = 0;
    size_t        m_focusedLen   = 0;
    bool          m_hasFocus     = false;
};

// Read-only session that returns the screen rect of a substring of the
// composition. Used to anchor the candidate window to the focused
// bunsetsu's column rather than the composition's left edge.
class CGetBunsetsuRectSession : public ITfEditSession
{
public:
    CGetBunsetsuRectSession(ITfContext* ctx, ITfComposition* comp,
                            ULONG start, ULONG length, POINT* outPos);

    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;
    STDMETHODIMP DoEditSession(TfEditCookie ec) override;

private:
    ~CGetBunsetsuRectSession();
    LONG            m_cRef;
    ITfContext*     m_ctx;
    ITfComposition* m_comp;
    ULONG           m_start;
    ULONG           m_length;
    POINT*          m_outPos;
};
