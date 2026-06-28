#include "editsession.h"
#include "textservice.h"
#include <stdio.h>

namespace
{
    // Stamps the composition range with our display attribute guidatom via
    // GUID_PROP_ATTRIBUTE. This is what makes the host app render the
    // composition with our underline (TF_LS_SOLID) instead of as plain text.
    HRESULT ApplyDisplayAttributeToComposition(TfEditCookie ec, ITfContext* pContext, ITfComposition* pComposition)
    {
        if (!pContext || !pComposition || g_gaDisplayAttributeInput == TF_INVALID_GUIDATOM)
            return E_UNEXPECTED;

        ITfRange* pRange = nullptr;
        HRESULT hr = pComposition->GetRange(&pRange);
        if (FAILED(hr) || !pRange) return hr;

        ITfProperty* pProp = nullptr;
        hr = pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProp);
        if (SUCCEEDED(hr) && pProp)
        {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = static_cast<LONG>(g_gaDisplayAttributeInput);
            hr = pProp->SetValue(ec, pRange, &v);
            pProp->Release();
        }
        pRange->Release();
        return hr;
    }
}

CEditSession::CEditSession(CTextService* pService, ITfContext* pContext, EditAction action, const std::wstring& text)
    : m_cRef(1)
    , m_pService(pService)
    , m_pContext(pContext)
    , m_action(action)
    , m_text(text)
{
    if (m_pContext) m_pContext->AddRef();
    InterlockedIncrement(&g_cRefDll);
}

CEditSession::~CEditSession()
{
    if (m_pContext) m_pContext->Release();
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CEditSession::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
    {
        *ppvObj = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEditSession::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CEditSession::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CEditSession::DoEditSession(TfEditCookie ec)
{
    switch (m_action)
    {
    case EditAction::StartAndUpdate: return DoStartAndUpdate(ec);
    case EditAction::Update:         return DoUpdate(ec);
    case EditAction::EndCommit:      return DoEnd(ec, false);
    case EditAction::EndCancel:      return DoEnd(ec, true);
    }
    return E_UNEXPECTED;
}

HRESULT CEditSession::DoStartAndUpdate(TfEditCookie ec)
{
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    HRESULT hr = m_pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsertAtSelection);
    if (FAILED(hr)) return hr;

    ITfRange* pRangeInsert = nullptr;
    hr = pInsertAtSelection->InsertTextAtSelection(ec, 0, m_text.c_str(), (LONG)m_text.length(), &pRangeInsert);
    pInsertAtSelection->Release();
    if (FAILED(hr) || !pRangeInsert) return hr;

    ITfContextComposition* pContextComposition = nullptr;
    hr = m_pContext->QueryInterface(IID_ITfContextComposition, (void**)&pContextComposition);
    if (FAILED(hr)) { pRangeInsert->Release(); return hr; }

    ITfComposition* pComposition = nullptr;
    hr = pContextComposition->StartComposition(ec, pRangeInsert, static_cast<ITfCompositionSink*>(m_pService), &pComposition);
    pContextComposition->Release();
    pRangeInsert->Release();
    if (FAILED(hr) || !pComposition) return hr;

    // Move the caret to the end of the composition.
    ITfRange* pRange = nullptr;
    if (SUCCEEDED(pComposition->GetRange(&pRange)) && pRange)
    {
        pRange->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel = {};
        sel.range = pRange;
        sel.style.ase = TF_AE_END;
        sel.style.fInterimChar = FALSE;
        m_pContext->SetSelection(ec, 1, &sel);
        pRange->Release();
    }

    m_pService->SetComposition(pComposition);
    ApplyDisplayAttributeToComposition(ec, m_pContext, pComposition);
    pComposition->Release();
    return S_OK;
}

HRESULT CEditSession::DoUpdate(TfEditCookie ec)
{
    ITfComposition* pComposition = m_pService->GetComposition();
    if (!pComposition) return S_OK;

    ITfRange* pRange = nullptr;
    HRESULT hr = pComposition->GetRange(&pRange);
    if (FAILED(hr) || !pRange) return hr;

    pRange->SetText(ec, 0, m_text.c_str(), (LONG)m_text.length());

    // Collapsing the range gives us a zero-width range at the composition's
    // end; pushing it into the context's selection moves the actual caret
    // there. Without this, the visible caret stays at the composition start
    // even as the displayed text grows, which feels broken to the user.
    pRange->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION sel = {};
    sel.range = pRange;
    sel.style.ase = TF_AE_END;
    sel.style.fInterimChar = FALSE;
    m_pContext->SetSelection(ec, 1, &sel);

    pRange->Release();

    // Re-stamp the attribute: SetText replaces the range contents and TSF
    // does not carry the property forward to the freshly-inserted characters.
    ApplyDisplayAttributeToComposition(ec, m_pContext, pComposition);
    return S_OK;
}

// ---------------------------------------------------------------------------
// CGetRectSession
// ---------------------------------------------------------------------------

CGetRectSession::CGetRectSession(ITfContext* ctx, ITfComposition* comp, POINT* outPos)
    : m_cRef(1), m_ctx(ctx), m_comp(comp), m_outPos(outPos)
{
    if (m_ctx) m_ctx->AddRef();
    if (m_comp) m_comp->AddRef();
    InterlockedIncrement(&g_cRefDll);
}

CGetRectSession::~CGetRectSession()
{
    if (m_comp) m_comp->Release();
    if (m_ctx) m_ctx->Release();
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CGetRectSession::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
    {
        *ppvObj = static_cast<ITfEditSession*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CGetRectSession::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CGetRectSession::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CGetRectSession::DoEditSession(TfEditCookie ec)
{
    if (!m_ctx || !m_comp || !m_outPos) return E_UNEXPECTED;
    ITfContextView* pView = nullptr;
    HRESULT hr = m_ctx->GetActiveView(&pView);
    if (FAILED(hr) || !pView) return hr;

    ITfRange* pRange = nullptr;
    hr = m_comp->GetRange(&pRange);
    if (FAILED(hr) || !pRange) { pView->Release(); return hr; }

    RECT rc = {};
    BOOL clipped = FALSE;
    hr = pView->GetTextExt(ec, pRange, &rc, &clipped);
    if (SUCCEEDED(hr))
    {
        // Anchor the candidate popup just below the composition's bottom-left.
        m_outPos->x = rc.left;
        m_outPos->y = rc.bottom + 2;
    }

    pRange->Release();
    pView->Release();
    return hr;
}

HRESULT CEditSession::DoEnd(TfEditCookie ec, bool cancel)
{
    ITfComposition* pComposition = m_pService->GetComposition();
    if (!pComposition) return S_OK;

    ITfRange* pRange = nullptr;
    if (SUCCEEDED(pComposition->GetRange(&pRange)) && pRange)
    {
        if (cancel)
        {
            pRange->SetText(ec, 0, L"", 0);
        }
        else if (!m_text.empty())
        {
            // Commit: override range with caller-supplied final text. Used by
            // Enter when no Ollama conversion happened, to apply
            // FinalizeTrailingN ("kan" -> "かん"). When m_text is empty the
            // caller wants to keep whatever's already in the range (e.g. the
            // kanji that Space->Ollama already wrote).
            pRange->SetText(ec, 0, m_text.c_str(), (LONG)m_text.length());
        }

        // Anchor the caret at the END of the range BEFORE EndComposition.
        // Without this, EndComposition leaves the visible caret where the
        // composition started, so after committing "あああああ" the cursor
        // jumps back to before the first あ instead of staying at the end.
        pRange->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel = {};
        sel.range = pRange;
        sel.style.ase = TF_AE_END;
        sel.style.fInterimChar = FALSE;
        m_pContext->SetSelection(ec, 1, &sel);

        pRange->Release();
    }

    pComposition->EndComposition(ec);
    m_pService->SetComposition(nullptr);
    return S_OK;
}
