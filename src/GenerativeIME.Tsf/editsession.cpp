#include "editsession.h"
#include "textservice.h"
#include <stdio.h>

namespace
{
    HRESULT SetAttributeOnRange(TfEditCookie ec, ITfProperty* pProp,
                                ITfRange* pRange, TfGuidAtom atom)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = static_cast<LONG>(atom);
        return pProp->SetValue(ec, pRange, &v);
    }

    // Stamps the entire composition with the plain "input" attribute.
    // Used when Phase B isn't active or when there's no focus information.
    HRESULT ApplyInputAttributeToComposition(TfEditCookie ec, ITfContext* pContext, ITfComposition* pComposition)
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
            hr = SetAttributeOnRange(ec, pProp, pRange, g_gaDisplayAttributeInput);
            pProp->Release();
        }
        pRange->Release();
        return hr;
    }

    // Phase B: split the composition into three contiguous sub-ranges
    // (pre / focused / post) and stamp the focused middle one with the
    // BunsetsuFocus attribute, the others with Input. If the BunsetsuFocus
    // atom didn't register we fall back to the plain Input stamp.
    HRESULT ApplyBunsetsuAttributesToComposition(
        TfEditCookie ec, ITfContext* pContext, ITfComposition* pComposition,
        ULONG focusedStart, ULONG focusedLen)
    {
        if (!pContext || !pComposition) return E_UNEXPECTED;
        if (g_gaDisplayAttributeBunsetsuFocus == TF_INVALID_GUIDATOM)
            return ApplyInputAttributeToComposition(ec, pContext, pComposition);

        ITfRange* pFull = nullptr;
        HRESULT hr = pComposition->GetRange(&pFull);
        if (FAILED(hr) || !pFull) return hr;

        ITfProperty* pProp = nullptr;
        hr = pContext->GetProperty(GUID_PROP_ATTRIBUTE, &pProp);
        if (FAILED(hr) || !pProp)
        {
            pFull->Release();
            return hr;
        }

        // Build [pre, focused, post] by cloning the full range and shifting
        // anchors. ShiftStart / ShiftEnd take character counts and return
        // how many they actually moved (clamped at range boundaries).
        ITfRange* pPre     = nullptr;
        ITfRange* pFocused = nullptr;
        ITfRange* pPost    = nullptr;
        pFull->Clone(&pPre);
        pFull->Clone(&pFocused);
        pFull->Clone(&pPost);

        if (pPre)
        {
            LONG shifted = 0;
            pPre->ShiftEnd(ec, (LONG)focusedStart, &shifted, nullptr);
            SetAttributeOnRange(ec, pProp, pPre, g_gaDisplayAttributeInput);
            pPre->Release();
        }
        if (pFocused)
        {
            LONG shifted = 0;
            pFocused->ShiftStart(ec, (LONG)focusedStart, &shifted, nullptr);
            pFocused->Collapse(ec, TF_ANCHOR_START);
            pFocused->ShiftEnd(ec, (LONG)focusedLen, &shifted, nullptr);
            SetAttributeOnRange(ec, pProp, pFocused, g_gaDisplayAttributeBunsetsuFocus);
            pFocused->Release();
        }
        if (pPost)
        {
            LONG shifted = 0;
            pPost->ShiftStart(ec, (LONG)(focusedStart + focusedLen), &shifted, nullptr);
            SetAttributeOnRange(ec, pProp, pPost, g_gaDisplayAttributeInput);
            pPost->Release();
        }

        pProp->Release();
        pFull->Release();
        return S_OK;
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
    case EditAction::InsertDirect:   return DoInsertDirect(ec);
    }
    return E_UNEXPECTED;
}

HRESULT CEditSession::DoStartAndUpdate(TfEditCookie ec)
{
    // Defense in depth: if a previous composition is still live (e.g. a
    // CommitConvertedIfAny EndCommit was queued async by the host and hasn't
    // run yet, or the caller chose StartAndUpdate while m_pComposition was
    // still non-null), end it BEFORE inserting the new text. Without this,
    // the InsertTextAtSelection at the caret can land inside the still-open
    // composition and the following StartComposition may fail or create a
    // nested composition — the observed symptom was the 2nd bunsetsu's
    // first romaji pair appearing as raw ASCII (e.g. "k a") because the
    // fresh composition never actually started around them.
    ITfComposition* pOld = m_pService->GetComposition();
    if (pOld)
    {
        OutputDebugStringW(L"[GenerativeIME] DoStartAndUpdate: found leftover composition, ending it first\n");
        ITfRange* pOldRange = nullptr;
        if (SUCCEEDED(pOld->GetRange(&pOldRange)) && pOldRange)
        {
            pOldRange->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION oldSel = {};
            oldSel.range = pOldRange;
            oldSel.style.ase = TF_AE_END;
            oldSel.style.fInterimChar = FALSE;
            m_pContext->SetSelection(ec, 1, &oldSel);
            pOldRange->Release();
        }
        pOld->EndComposition(ec);
        m_pService->SetComposition(nullptr);
    }

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
    if (m_hasFocus)
        ApplyBunsetsuAttributesToComposition(ec, m_pContext, pComposition,
                                             (ULONG)m_focusedStart, (ULONG)m_focusedLen);
    else
        ApplyInputAttributeToComposition(ec, m_pContext, pComposition);
    pComposition->Release();
    return S_OK;
}

// F4 repeat-paste: drop the text straight into the document at the caret
// with NO composition — the text is already final (it's the previous
// commit), so opening a composition would just add an underline flash and
// an extra Enter for the user.
HRESULT CEditSession::DoInsertDirect(TfEditCookie ec)
{
    if (m_text.empty()) return S_OK;

    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    HRESULT hr = m_pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pInsertAtSelection);
    if (FAILED(hr)) return hr;

    ITfRange* pRange = nullptr;
    hr = pInsertAtSelection->InsertTextAtSelection(ec, 0, m_text.c_str(), (LONG)m_text.length(), &pRange);
    pInsertAtSelection->Release();
    if (FAILED(hr) || !pRange) return hr;

    // Caret to the end of the inserted text so repeated presses chain
    // (‼️‼️‼️ …) instead of inserting in front of the previous paste.
    pRange->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION sel = {};
    sel.range = pRange;
    sel.style.ase = TF_AE_END;
    sel.style.fInterimChar = FALSE;
    m_pContext->SetSelection(ec, 1, &sel);
    pRange->Release();
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
    if (m_hasFocus)
        ApplyBunsetsuAttributesToComposition(ec, m_pContext, pComposition,
                                             (ULONG)m_focusedStart, (ULONG)m_focusedLen);
    else
        ApplyInputAttributeToComposition(ec, m_pContext, pComposition);
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

// ---------------------------------------------------------------------------
// CGetBunsetsuRectSession
// ---------------------------------------------------------------------------

CGetBunsetsuRectSession::CGetBunsetsuRectSession(ITfContext* ctx, ITfComposition* comp,
                                                 ULONG start, ULONG length, POINT* outPos)
    : m_cRef(1), m_ctx(ctx), m_comp(comp), m_start(start), m_length(length), m_outPos(outPos)
{
    if (m_ctx)  m_ctx->AddRef();
    if (m_comp) m_comp->AddRef();
    InterlockedIncrement(&g_cRefDll);
}

CGetBunsetsuRectSession::~CGetBunsetsuRectSession()
{
    if (m_comp) m_comp->Release();
    if (m_ctx)  m_ctx->Release();
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CGetBunsetsuRectSession::QueryInterface(REFIID riid, void** ppvObj)
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

STDMETHODIMP_(ULONG) CGetBunsetsuRectSession::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CGetBunsetsuRectSession::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CGetBunsetsuRectSession::DoEditSession(TfEditCookie ec)
{
    if (!m_ctx || !m_comp || !m_outPos) return E_UNEXPECTED;

    ITfContextView* pView = nullptr;
    HRESULT hr = m_ctx->GetActiveView(&pView);
    if (FAILED(hr) || !pView) return hr;

    ITfRange* pSub = nullptr;
    hr = m_comp->GetRange(&pSub);
    if (FAILED(hr) || !pSub) { pView->Release(); return hr; }

    // Trim the cloned range to [m_start, m_start + m_length) before asking
    // for its on-screen rect. ShiftStart / ShiftEnd return how many chars
    // they actually moved (clamped at the range's natural boundaries) so a
    // request past the end just gives us the tail rect, which is fine.
    LONG shifted = 0;
    pSub->ShiftStart(ec, (LONG)m_start, &shifted, nullptr);
    pSub->Collapse(ec, TF_ANCHOR_START);
    pSub->ShiftEnd(ec, (LONG)m_length, &shifted, nullptr);

    RECT rc = {};
    BOOL clipped = FALSE;
    hr = pView->GetTextExt(ec, pSub, &rc, &clipped);
    if (SUCCEEDED(hr))
    {
        m_outPos->x = rc.left;
        m_outPos->y = rc.bottom + 2;
    }

    pSub->Release();
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
