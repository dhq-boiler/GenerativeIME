#include "textservice.h"
#include "editsession.h"
#include "displayattribute.h"
#include "langbaritem.h"
#include "romajitokana.h"
#include "ollamaclient.h"
#include "candidatewindow.h"
#include "symboldictionary.h"
#include "skkdictionary.h"
#include "bunsetsu.h"
#include "mecabanalyzer.h"
#include "learningstore.h"
#include <stdio.h>
#include <thread>
#include <vector>
#include <unordered_map>

constexpr UINT WM_OLLAMA_DONE         = WM_USER + 1;
constexpr UINT WM_LANGBAR_MENU        = WM_USER + 2;
constexpr UINT WM_SET_IME_MODE        = WM_USER + 3; // wParam = 1 to turn ON, 0 to turn OFF
constexpr UINT WM_OLLAMA_REORDER_DONE  = WM_USER + 4;
constexpr UINT WM_OLLAMA_FALLBACK_DONE = WM_USER + 5;
constexpr wchar_t kMsgWndClass[] = L"GenerativeIME_MsgWnd_v1";

// Posted from worker thread back to the IME thread via PostMessage.
// Owned by the worker until PostMessage handoff; then owned by WndProc which
// deletes it after HandleOllamaDone returns.
struct PendingOllamaRequest
{
    CTextService*              service;
    ITfContext*                context;       // AddRef'd on construction, Release'd on destruction
    std::wstring               reading;
    std::wstring               recentContext; // recently committed text — snapshot for the prompt
    std::vector<std::wstring>  candidates;    // all parsed "text" values
    HRESULT                    hr;
    DWORD                      httpStatus;

    PendingOllamaRequest(CTextService* s, ITfContext* c, std::wstring r, std::wstring ctx)
        : service(s), context(c), reading(std::move(r)), recentContext(std::move(ctx)),
          hr(E_FAIL), httpStatus(0)
    {
        if (context) context->AddRef();
    }
    ~PendingOllamaRequest()
    {
        if (context) context->Release();
    }
};

// Async supplementary Ollama lookup fired when MeCab's split looks dubious
// (see bunsetsu::LooksSuspect). The worker generates a fresh candidate list
// from scratch; on arrival the IME prepends those candidates above MeCab's
// answer in the candidate window. Same seq-based staleness pattern as the
// reorder request, on the same m_reorderSeq counter — both ops are
// "asynchronous edits to the candidate list" so they share invalidation.
struct PendingOllamaFallbackRequest
{
    CTextService*              service;
    ITfContext*                tfContext;
    std::wstring               reading;
    std::wstring               recentContext;
    std::wstring               mecabTop;       // MeCab's answer, passed to the prompt as "what NOT to repeat"
    std::vector<std::wstring>  candidates;     // filled by worker
    unsigned                   seq;
    HRESULT                    hr;

    PendingOllamaFallbackRequest(CTextService* s, ITfContext* c, std::wstring r,
                                 std::wstring ctx, std::wstring top, unsigned sequence)
        : service(s), tfContext(c), reading(std::move(r)), recentContext(std::move(ctx)),
          mecabTop(std::move(top)), seq(sequence), hr(E_FAIL)
    {
        if (tfContext) tfContext->AddRef();
    }
    ~PendingOllamaFallbackRequest()
    {
        if (tfContext) tfContext->Release();
    }
};

// Async reorder of SKK-supplied candidates against the current context.
// Lifecycle: worker thread fills `reordered`; PostMessage hands ownership to
// the IME thread, which deletes after HandleOllamaReorderDone.
struct PendingOllamaReorderRequest
{
    CTextService*              service;
    ITfContext*                tfContext;     // AddRef'd; needed for composition repaint after reorder
    std::wstring               reading;
    std::wstring               recentContext;
    std::vector<std::wstring>  original;      // candidates we showed the user immediately
    std::vector<std::wstring>  reordered;     // filled by worker; empty if reorder failed
    unsigned                   seq;           // discarded on arrival if service's seq has moved on

    PendingOllamaReorderRequest(CTextService* s, ITfContext* c, std::wstring r,
                                std::wstring ctx, std::vector<std::wstring> orig,
                                unsigned sequence)
        : service(s), tfContext(c), reading(std::move(r)), recentContext(std::move(ctx)),
          original(std::move(orig)), seq(sequence)
    {
        if (tfContext) tfContext->AddRef();
    }
    ~PendingOllamaReorderRequest()
    {
        if (tfContext) tfContext->Release();
    }
};

namespace
{
    // Composition display = converted kana + still-untranslatable trailing romaji,
    // so the user sees "きょ" + "u" while mid-typing "kyou". Default = hiragana.
    std::wstring BuildHiraganaDisplay(const std::wstring& romajiBuffer)
    {
        auto r = romaji::Convert(romajiBuffer);
        return r.hira + r.remaining;
    }

    // Hiragana -> Katakana: shift Unicode 0x3041-0x3096 by +0x60.
    std::wstring ToFullKatakana(const std::wstring& hira)
    {
        std::wstring out;
        out.reserve(hira.size());
        for (wchar_t c : hira)
        {
            if (c >= 0x3041 && c <= 0x3096) out.push_back((wchar_t)(c + 0x60));
            else                            out.push_back(c);
        }
        return out;
    }

    // Full-width Katakana -> Half-width Katakana. Voiced/semi-voiced sounds
    // expand to two code units (base + ﾞ / ﾟ) as is standard for halfwidth.
    std::wstring ToHalfKatakana(const std::wstring& kata)
    {
        static const std::unordered_map<wchar_t, std::wstring> map = {
            {L'ア',L"ｱ"},{L'イ',L"ｲ"},{L'ウ',L"ｳ"},{L'エ',L"ｴ"},{L'オ',L"ｵ"},
            {L'カ',L"ｶ"},{L'キ',L"ｷ"},{L'ク',L"ｸ"},{L'ケ',L"ｹ"},{L'コ',L"ｺ"},
            {L'サ',L"ｻ"},{L'シ',L"ｼ"},{L'ス',L"ｽ"},{L'セ',L"ｾ"},{L'ソ',L"ｿ"},
            {L'タ',L"ﾀ"},{L'チ',L"ﾁ"},{L'ツ',L"ﾂ"},{L'テ',L"ﾃ"},{L'ト',L"ﾄ"},
            {L'ナ',L"ﾅ"},{L'ニ',L"ﾆ"},{L'ヌ',L"ﾇ"},{L'ネ',L"ﾈ"},{L'ノ',L"ﾉ"},
            {L'ハ',L"ﾊ"},{L'ヒ',L"ﾋ"},{L'フ',L"ﾌ"},{L'ヘ',L"ﾍ"},{L'ホ',L"ﾎ"},
            {L'マ',L"ﾏ"},{L'ミ',L"ﾐ"},{L'ム',L"ﾑ"},{L'メ',L"ﾒ"},{L'モ',L"ﾓ"},
            {L'ヤ',L"ﾔ"},{L'ユ',L"ﾕ"},{L'ヨ',L"ﾖ"},
            {L'ラ',L"ﾗ"},{L'リ',L"ﾘ"},{L'ル',L"ﾙ"},{L'レ',L"ﾚ"},{L'ロ',L"ﾛ"},
            {L'ワ',L"ﾜ"},{L'ヲ',L"ｦ"},{L'ン',L"ﾝ"},
            {L'ガ',L"ｶﾞ"},{L'ギ',L"ｷﾞ"},{L'グ',L"ｸﾞ"},{L'ゲ',L"ｹﾞ"},{L'ゴ',L"ｺﾞ"},
            {L'ザ',L"ｻﾞ"},{L'ジ',L"ｼﾞ"},{L'ズ',L"ｽﾞ"},{L'ゼ',L"ｾﾞ"},{L'ゾ',L"ｿﾞ"},
            {L'ダ',L"ﾀﾞ"},{L'ヂ',L"ﾁﾞ"},{L'ヅ',L"ﾂﾞ"},{L'デ',L"ﾃﾞ"},{L'ド',L"ﾄﾞ"},
            {L'バ',L"ﾊﾞ"},{L'ビ',L"ﾋﾞ"},{L'ブ',L"ﾌﾞ"},{L'ベ',L"ﾍﾞ"},{L'ボ',L"ﾎﾞ"},
            {L'パ',L"ﾊﾟ"},{L'ピ',L"ﾋﾟ"},{L'プ',L"ﾌﾟ"},{L'ペ',L"ﾍﾟ"},{L'ポ',L"ﾎﾟ"},
            {L'ァ',L"ｧ"},{L'ィ',L"ｨ"},{L'ゥ',L"ｩ"},{L'ェ',L"ｪ"},{L'ォ',L"ｫ"},
            {L'ャ',L"ｬ"},{L'ュ',L"ｭ"},{L'ョ',L"ｮ"},{L'ッ',L"ｯ"},
            {L'ヴ',L"ｳﾞ"},
            {L'ー',L"ｰ"},{L'、',L"､"},{L'。',L"｡"},{L'「',L"｢"},{L'」',L"｣"},{L'・',L"･"},
        };
        std::wstring out;
        out.reserve(kata.size());
        for (wchar_t c : kata)
        {
            auto it = map.find(c);
            if (it != map.end()) out += it->second;
            else                 out.push_back(c);
        }
        return out;
    }

    // ASCII -> Full-width: ! through ~ shift by +0xFEE0; space -> U+3000.
    std::wstring ToFullWidthAscii(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            if (c >= 0x21 && c <= 0x7E) out.push_back((wchar_t)(c + 0xFEE0));
            else if (c == L' ')         out.push_back(L'　');
            else                        out.push_back(c);
        }
        return out;
    }
}

// Method on CTextService — needs access to m_imeMode, so it can't live in the
// anonymous namespace's free helpers. Inline glue: build the right display
// string per active mode.
namespace
{
    std::wstring DisplayForMode(const std::wstring& romaji, ImeMode mode)
    {
        switch (mode)
        {
        case ImeMode::FullKatakana:
        {
            auto r = romaji::Convert(romaji);
            return ToFullKatakana(r.hira) + ToFullKatakana(r.remaining);
        }
        case ImeMode::HalfKatakana:
        {
            auto r = romaji::Convert(romaji);
            return ToHalfKatakana(ToFullKatakana(r.hira)) + r.remaining;
        }
        case ImeMode::FullAlnum:
            return ToFullWidthAscii(romaji);
        case ImeMode::Hiragana:
        case ImeMode::Off:
        default:
            return BuildHiraganaDisplay(romaji);
        }
    }

    // Extract every {"text":"..."} string value from Ollama's response JSON,
    // in the order the model emitted them. Hand-rolled scan: response is
    // small, comes from our own prompt, and pulling in a real JSON dep just
    // for these fields would be overkill.
    bool ReadJsonString(const std::wstring& body, size_t& pos, std::wstring& out)
    {
        if (pos >= body.size() || body[pos] != L'"') return false;
        pos++;
        out.clear();
        while (pos < body.size())
        {
            wchar_t c = body[pos++];
            if (c == L'"') return true;
            if (c == L'\\' && pos < body.size())
            {
                wchar_t e = body[pos++];
                switch (e)
                {
                case L'"':  out += L'"';  break;
                case L'\\': out += L'\\'; break;
                case L'/':  out += L'/';  break;
                case L'n':  out += L'\n'; break;
                case L't':  out += L'\t'; break;
                case L'r':  out += L'\r'; break;
                default:    out += e;     break;
                }
            }
            else
            {
                out += c;
            }
        }
        return false;
    }

    // Extract `"<key>":[N1, N2, ...]` as a vector of ints. Used for the
    // reorder response shape `{"order":[2,0,1,3]}`. Hand-rolled for the
    // same reason ExtractAllCandidates is.
    std::vector<int> ExtractIntArray(const std::wstring& jsonBody, const std::wstring& key)
    {
        std::vector<int> result;
        std::wstring pat = L"\"" + key + L"\"";
        size_t kpos = jsonBody.find(pat);
        if (kpos == std::wstring::npos) return result;
        size_t open = jsonBody.find(L'[', kpos);
        if (open == std::wstring::npos) return result;
        size_t close = jsonBody.find(L']', open);
        if (close == std::wstring::npos) return result;

        size_t pos = open + 1;
        while (pos < close)
        {
            while (pos < close && !iswdigit(jsonBody[pos]) && jsonBody[pos] != L'-') pos++;
            if (pos >= close) break;
            std::wstring num;
            if (jsonBody[pos] == L'-') { num += L'-'; pos++; }
            while (pos < close && iswdigit(jsonBody[pos])) { num += jsonBody[pos++]; }
            if (!num.empty())
            {
                try { result.push_back(std::stoi(num)); }
                catch (...) { /* skip malformed entries */ }
            }
        }
        return result;
    }

    std::vector<std::wstring> ExtractAllCandidates(const std::wstring& jsonBody)
    {
        std::vector<std::wstring> result;
        size_t pos = 0;
        while (pos < jsonBody.size())
        {
            size_t found = jsonBody.find(L"\"text\"", pos);
            if (found == std::wstring::npos) break;
            pos = found + 6;
            while (pos < jsonBody.size() && (jsonBody[pos] == L' ' || jsonBody[pos] == L'\t')) pos++;
            if (pos >= jsonBody.size() || jsonBody[pos] != L':') continue;
            pos++;
            while (pos < jsonBody.size() && (jsonBody[pos] == L' ' || jsonBody[pos] == L'\t')) pos++;

            std::wstring text;
            if (ReadJsonString(jsonBody, pos, text) && !text.empty())
            {
                result.push_back(std::move(text));
            }
        }
        return result;
    }
}

CTextService::CTextService()
    : m_cRef(1)
    , m_pThreadMgr(nullptr)
    , m_tfClientId(TF_CLIENTID_NULL)
    , m_pComposition(nullptr)
    , m_pLangBarItem(nullptr)
    , m_isImeOn(TRUE)
    , m_compositionConverted(FALSE)
    , m_pCompOpenClose(nullptr)
    , m_pCompConvMode(nullptr)
    , m_dwCookieOpenClose(TF_INVALID_COOKIE)
    , m_dwCookieConvMode(TF_INVALID_COOKIE)
    , m_hwndMsg(nullptr)
    , m_pCandWnd(nullptr)
    , m_pLearning(nullptr)
    , m_imeMode(ImeMode::Hiragana)
{
    InterlockedIncrement(&g_cRefDll);
}

CTextService::~CTextService()
{
    if (m_pComposition) { m_pComposition->Release(); m_pComposition = nullptr; }
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppvObj = static_cast<ITfTextInputProcessor*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfCompartmentEventSink))
    {
        *ppvObj = static_cast<ITfCompartmentEventSink*>(this);
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CTextService::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release()
{
    LONG cr = InterlockedDecrement(&m_cRef);
    if (cr == 0) delete this;
    return cr;
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
    m_pThreadMgr = pThreadMgr;
    if (m_pThreadMgr) m_pThreadMgr->AddRef();
    m_tfClientId = tfClientId;

    HRESULT hr = InitKeyEventSink();
    if (FAILED(hr))
    {
        wchar_t buf[96];
        swprintf_s(buf, L"[GenerativeIME] InitKeyEventSink failed hr=0x%08X\n", (unsigned)hr);
        OutputDebugStringW(buf);
        Deactivate();
        return hr;
    }

    HRESULT hrPk = InitPreservedKeys();
    wchar_t pkbuf[96];
    swprintf_s(pkbuf, L"[GenerativeIME] InitPreservedKeys hr=0x%08X\n", (unsigned)hrPk);
    OutputDebugStringW(pkbuf);

    HRESULT hrComp = SetIMEStateCompartments(TRUE);
    wchar_t cbuf[96];
    swprintf_s(cbuf, L"[GenerativeIME] SetIMEStateCompartments(TRUE) hr=0x%08X\n", (unsigned)hrComp);
    OutputDebugStringW(cbuf);

    HRESULT hrAtom = InitDisplayAttributeGuidAtom();
    wchar_t abuf[128];
    swprintf_s(abuf, L"[GenerativeIME] InitDisplayAttributeGuidAtom hr=0x%08X atom=0x%08X\n",
               (unsigned)hrAtom, (unsigned)g_gaDisplayAttributeInput);
    OutputDebugStringW(abuf);

    HRESULT hrLb = InitLangBarItem();
    wchar_t lbuf[96];
    swprintf_s(lbuf, L"[GenerativeIME] InitLangBarItem hr=0x%08X\n", (unsigned)hrLb);
    OutputDebugStringW(lbuf);

    HRESULT hrSink = InitCompartmentSinks();
    wchar_t sbuf[96];
    swprintf_s(sbuf, L"[GenerativeIME] InitCompartmentSinks hr=0x%08X\n", (unsigned)hrSink);
    OutputDebugStringW(sbuf);
    SyncImeStateFromCompartments();

    HRESULT hrMsg = InitMessageWindow();
    wchar_t mbuf[96];
    swprintf_s(mbuf, L"[GenerativeIME] InitMessageWindow hr=0x%08X\n", (unsigned)hrMsg);
    OutputDebugStringW(mbuf);

    if (!m_pCandWnd) m_pCandWnd = new CCandidateWindow();
    HRESULT hrCw = m_pCandWnd->Create();
    wchar_t cwbuf[96];
    swprintf_s(cwbuf, L"[GenerativeIME] CandidateWindow.Create hr=0x%08X\n", (unsigned)hrCw);
    OutputDebugStringW(cwbuf);

    if (!m_pLearning) m_pLearning = new LearningStore();
    HRESULT hrLs = m_pLearning->Load();
    wchar_t lsbuf[96];
    swprintf_s(lsbuf, L"[GenerativeIME] LearningStore.Load hr=0x%08X\n", (unsigned)hrLs);
    OutputDebugStringW(lsbuf);

    // Warm the SKK dictionary so the first user keystroke isn't blocked by
    // a 4–6 MB file read. Subsequent Activate calls hit the std::call_once
    // fast path and return the already-loaded singleton in nanoseconds.
    SkkDictionary::GetGlobal();
    // Same idea for MeCab — sys.dic is ~188 MB, init is heavy.
    MecabAnalyzer::GetGlobal();
    // Also kick Ollama: gemma4:12b cold-loads in ~90s on a CPU-only box,
    // which would blow past the per-request timeout in the fallback path.
    // Fire it now (fire-and-forget) so by the time the user types the
    // model is resident and subsequent calls return in <1s.
    StartOllamaWarmupAsync();

    {
        wchar_t buf[128];
        swprintf_s(buf, L"[GenerativeIME] Activated this=%p msgWnd=%p clientId=%u\n",
                   this, m_hwndMsg, (unsigned)m_tfClientId);
        OutputDebugStringW(buf);
    }
    return S_OK;
}

STDMETHODIMP CTextService::Deactivate()
{
    {
        wchar_t buf[128];
        swprintf_s(buf, L"[GenerativeIME] Deactivate enter this=%p msgWnd=%p\n",
                   this, m_hwndMsg);
        OutputDebugStringW(buf);
    }
    if (m_pCandWnd) { m_pCandWnd->Destroy(); delete m_pCandWnd; m_pCandWnd = nullptr; }
    if (m_pLearning) { delete m_pLearning; m_pLearning = nullptr; }
    UninitMessageWindow();
    UninitCompartmentSinks();
    UninitLangBarItem();
    // Intentionally do NOT call SetIMEStateCompartments(FALSE) here.
    // OPENCLOSE / INPUTMODE_CONVERSION live on the *global* compartment manager,
    // so they're shared across every CTextService instance in this process
    // (and there ARE multiple — TSF spins up a fresh instance per thread/doc
    // context). Clearing OPENCLOSE here flips the global state to FALSE and
    // silently turns off the IME for every other live instance — observed
    // symptom: pick "半角カナ" from the tray menu, then a sibling instance
    // Deactivates a frame later and your fresh HalfKatakana selection becomes
    // "A" (Off) until the user taps 半角/全角 to flip OPENCLOSE back.
    UninitPreservedKeys();
    UninitKeyEventSink();

    if (m_pThreadMgr)
    {
        m_pThreadMgr->Release();
        m_pThreadMgr = nullptr;
    }
    m_tfClientId = TF_CLIENTID_NULL;
    m_romajiBuffer.clear();
    OutputDebugStringW(L"[GenerativeIME] Deactivated\n");
    return S_OK;
}

HRESULT CTextService::InitKeyEventSink()
{
    if (!m_pThreadMgr) return E_UNEXPECTED;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr);
    if (FAILED(hr)) return hr;

    hr = pKeystrokeMgr->AdviseKeyEventSink(m_tfClientId, static_cast<ITfKeyEventSink*>(this), TRUE);
    pKeystrokeMgr->Release();
    return hr;
}

void CTextService::UninitKeyEventSink()
{
    if (!m_pThreadMgr || m_tfClientId == TF_CLIENTID_NULL) return;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
    {
        pKeystrokeMgr->UnadviseKeyEventSink(m_tfClientId);
        pKeystrokeMgr->Release();
    }
}

// Preserved keys are how TSF lets us claim system-level VKs (Han/Zen, IME-On,
// IME-Off, Kanji) without breaking the rest of the shell. The OS routes them
// through OnPreservedKey only while this text service is the active TIP and
// the focus is on a text input — exactly the scope we want.
HRESULT CTextService::InitPreservedKeys()
{
    if (!m_pThreadMgr || m_tfClientId == TF_CLIENTID_NULL) return E_UNEXPECTED;

    ITfKeystrokeMgr* pMgr = nullptr;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pMgr);
    if (FAILED(hr) || !pMgr) return hr;

    auto preserve = [&](REFGUID guid, UINT vk, const wchar_t* desc) {
        TF_PRESERVEDKEY pk = { vk, 0 };
        pMgr->PreserveKey(m_tfClientId, guid, &pk, desc, (ULONG)wcslen(desc));
    };
    preserve(c_guidKeyKanji,  VK_KANJI,      L"GenerativeIME Toggle");
    preserve(c_guidKeyImeOn,  VK_OEM_AUTO,   L"GenerativeIME ON");
    preserve(c_guidKeyImeOff, VK_OEM_ENLW,   L"GenerativeIME OFF");

    pMgr->Release();
    return S_OK;
}

void CTextService::UninitPreservedKeys()
{
    if (!m_pThreadMgr || m_tfClientId == TF_CLIENTID_NULL) return;

    ITfKeystrokeMgr* pMgr = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pMgr)) || !pMgr) return;

    TF_PRESERVEDKEY pk;
    pk = { VK_KANJI,    0 }; pMgr->UnpreserveKey(c_guidKeyKanji,  &pk);
    pk = { VK_OEM_AUTO, 0 }; pMgr->UnpreserveKey(c_guidKeyImeOn,  &pk);
    pk = { VK_OEM_ENLW, 0 }; pMgr->UnpreserveKey(c_guidKeyImeOff, &pk);

    pMgr->Release();
}

// Sets the OPENCLOSE and INPUTMODE_CONVERSION compartments so the OS / host
// app reflects "IME is ON, native (hiragana) mode" in the IME indicator.
//
// Critical: these must be set via the *global* compartment manager
// (ITfThreadMgr::GetGlobalCompartment), not the thread-level compartment
// manager you get from QueryInterface(IID_ITfCompartmentMgr) on the
// thread mgr. The Win11 Input Indicator reads global compartments for
// keyboard state and ignores thread-scoped writes — that's why our
// previous thread-scoped version returned hr=0x0 but the "あ"/branding
// icon never appeared.
HRESULT CTextService::SetIMEStateCompartments(BOOL enable)
{
    if (!m_pThreadMgr || m_tfClientId == TF_CLIENTID_NULL) return E_UNEXPECTED;

    ITfCompartmentMgr* pCompMgr = nullptr;
    HRESULT hr = m_pThreadMgr->GetGlobalCompartment(&pCompMgr);
    if (FAILED(hr) || !pCompMgr) return hr;

    HRESULT hrOpenClose = E_FAIL;
    {
        ITfCompartment* pComp = nullptr;
        if (SUCCEEDED(pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &pComp)) && pComp)
        {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = enable ? 1 : 0;
            hrOpenClose = pComp->SetValue(m_tfClientId, &v);
            pComp->Release();
        }
    }

    HRESULT hrConvMode = E_FAIL;
    {
        ITfCompartment* pComp = nullptr;
        if (SUCCEEDED(pCompMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &pComp)) && pComp)
        {
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = enable ? TF_CONVERSIONMODE_NATIVE : 0;
            hrConvMode = pComp->SetValue(m_tfClientId, &v);
            pComp->Release();
        }
    }

    pCompMgr->Release();

    wchar_t buf[160];
    swprintf_s(buf, L"[GenerativeIME]   global OPENCLOSE hr=0x%08X, INPUTMODE_CONVERSION hr=0x%08X\n",
               (unsigned)hrOpenClose, (unsigned)hrConvMode);
    OutputDebugStringW(buf);
    return SUCCEEDED(hrOpenClose) ? S_OK : hrOpenClose;
}

// Maps our display attribute GUID to a TfGuidAtom for cheap reuse in every edit
// session. Cached in g_gaDisplayAttributeInput. Doing this once on Activate
// rather than per-composition keeps the hot path off CoCreateInstance.
HRESULT CTextService::InitDisplayAttributeGuidAtom()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCategoryMgr));
    if (FAILED(hr) || !pCategoryMgr) return hr;

    hr = pCategoryMgr->RegisterGUID(c_guidDisplayAttributeInput, &g_gaDisplayAttributeInput);
    pCategoryMgr->Release();
    return hr;
}

// Registers our LangBarItem with the system's ITfLangBarItemMgr so the Win11
// Input Indicator can read its icon/text and render "あ" in the system tray.
// Without this the tray shows just the language name ("日本") since the OS
// can't find any mode UI for the text service.
HRESULT CTextService::InitLangBarItem()
{
    if (!m_pThreadMgr) return E_UNEXPECTED;
    if (m_pLangBarItem) { OutputDebugStringW(L"[GenerativeIME] LangBar: already initialized, skip\n"); return S_OK; }

    ITfLangBarItemMgr* pMgr = nullptr;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pMgr);
    if (FAILED(hr) || !pMgr)
    {
        wchar_t b[120];
        swprintf_s(b, L"[GenerativeIME] LangBar: QI(ITfLangBarItemMgr) failed hr=0x%08X\n", (unsigned)hr);
        OutputDebugStringW(b);
        return hr;
    }

    m_pLangBarItem = new CLangBarItemButton(this);
    if (!m_pLangBarItem) { pMgr->Release(); return E_OUTOFMEMORY; }

    hr = pMgr->AddItem(m_pLangBarItem);
    wchar_t b[120];
    swprintf_s(b, L"[GenerativeIME] LangBar: AddItem hr=0x%08X (item=%p)\n", (unsigned)hr, (void*)m_pLangBarItem);
    OutputDebugStringW(b);
    if (FAILED(hr))
    {
        m_pLangBarItem->Release();
        m_pLangBarItem = nullptr;
    }
    pMgr->Release();
    return hr;
}

void CTextService::UninitLangBarItem()
{
    if (!m_pLangBarItem) return;
    if (m_pThreadMgr)
    {
        ITfLangBarItemMgr* pMgr = nullptr;
        if (SUCCEEDED(m_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pMgr)) && pMgr)
        {
            pMgr->RemoveItem(m_pLangBarItem);
            pMgr->Release();
        }
    }
    m_pLangBarItem->Release();
    m_pLangBarItem = nullptr;
}

// Hooks up sinks on the OPENCLOSE and INPUTMODE_CONVERSION global compartments
// so we get OnChange callbacks whenever the user toggles IME mode (e.g. via
// Half/Full-width key) or another component flips the conversion mode. Without
// this, our SetIMEStateCompartments(TRUE) at Activate would lock us "always on"
// from our own view and we'd keep eating alpha keys after the user pressed
// Half/Full-width to turn the IME off.
HRESULT CTextService::InitCompartmentSinks()
{
    if (!m_pThreadMgr) return E_UNEXPECTED;
    // Idempotent guard: if Activate is called a second time without an
    // intervening Deactivate, don't double-advise — that would compound
    // OnChange callbacks per real change and slow the shell down.
    if (m_pCompOpenClose || m_pCompConvMode) return S_OK;

    ITfCompartmentMgr* pCompMgr = nullptr;
    HRESULT hr = m_pThreadMgr->GetGlobalCompartment(&pCompMgr);
    if (FAILED(hr) || !pCompMgr) return hr;

    auto advise = [this](ITfCompartmentMgr* pMgr, REFGUID guid,
                         ITfCompartment** ppKeep, DWORD* pCookie) -> HRESULT
    {
        ITfCompartment* pComp = nullptr;
        HRESULT h = pMgr->GetCompartment(guid, &pComp);
        if (FAILED(h) || !pComp) return h;

        ITfSource* pSource = nullptr;
        h = pComp->QueryInterface(IID_ITfSource, (void**)&pSource);
        if (FAILED(h) || !pSource) { pComp->Release(); return h; }

        h = pSource->AdviseSink(IID_ITfCompartmentEventSink,
                                static_cast<ITfCompartmentEventSink*>(this), pCookie);
        pSource->Release();

        if (FAILED(h)) { pComp->Release(); return h; }
        *ppKeep = pComp; // hand off ownership
        return S_OK;
    };

    HRESULT hrOpen = advise(pCompMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                            &m_pCompOpenClose, &m_dwCookieOpenClose);
    HRESULT hrConv = advise(pCompMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                            &m_pCompConvMode, &m_dwCookieConvMode);

    pCompMgr->Release();
    return SUCCEEDED(hrOpen) ? S_OK : hrOpen;
}

void CTextService::UninitCompartmentSinks()
{
    auto unadvise = [](ITfCompartment** ppComp, DWORD* pCookie)
    {
        if (!*ppComp) return;
        ITfSource* pSource = nullptr;
        if (SUCCEEDED((*ppComp)->QueryInterface(IID_ITfSource, (void**)&pSource)) && pSource)
        {
            if (*pCookie != TF_INVALID_COOKIE) pSource->UnadviseSink(*pCookie);
            pSource->Release();
        }
        (*ppComp)->Release();
        *ppComp = nullptr;
        *pCookie = TF_INVALID_COOKIE;
    };
    unadvise(&m_pCompOpenClose, &m_dwCookieOpenClose);
    unadvise(&m_pCompConvMode,  &m_dwCookieConvMode);
}

// Reads current OPENCLOSE value and propagates to m_isImeOn + LangBarItem.
// Used both at Activate time (to pick up state set by us or any prior session)
// and from OnChange when the value flips at runtime.
void CTextService::SyncImeStateFromCompartments()
{
    if (!m_pCompOpenClose) return;
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(m_pCompOpenClose->GetValue(&v)))
    {
        BOOL newOn = (v.vt == VT_I4 && v.lVal != 0);
        if (newOn != m_isImeOn)
        {
            m_isImeOn = newOn;
            if (m_pLangBarItem) m_pLangBarItem->UpdateMode();
            // Drop any in-flight composition state when the user toggles off
            // so we don't strand half-typed romaji once the user comes back on.
            if (!m_isImeOn) m_romajiBuffer.clear();
        }
        VariantClear(&v);
    }
}

// Writes the OPENCLOSE compartment; our OnChange sink then propagates the
// new value into m_isImeOn and repaints the LangBar button.
void CTextService::SetImeOpenClose(BOOL on)
{
    if (!m_pCompOpenClose) return;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = on ? 1 : 0;
    m_pCompOpenClose->SetValue(m_tfClientId, &v);
}

STDMETHODIMP CTextService::OnChange(REFGUID rguid)
{
    if (IsEqualGUID(rguid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
    {
        SyncImeStateFromCompartments();
    }
    return S_OK;
}

void CTextService::TryOllamaConvertAsync(ITfContext* pContext)
{
    if (!pContext || m_romajiBuffer.empty() || !m_hwndMsg) return;

    auto r = romaji::Convert(m_romajiBuffer);
    std::wstring reading = r.hira + romaji::FinalizeTrailingN(r.remaining);
    if (reading.empty()) return;

    // Local symbol dictionary first: instant, no LLM round-trip. If we get
    // a hit, show that and skip Ollama — the user typed "やじるし" because
    // they want an arrow, not whatever the model would guess at.
    auto symHits = symbols::LookupAll(reading);
    if (!symHits.empty() && m_pCandWnd)
    {
        if (m_pLearning) symHits = m_pLearning->Reorder(reading, symHits);
        m_lastReading = reading;
        m_pCandWnd->SetCandidates(symHits);
        POINT pt = QueryCandidateAnchorPos(pContext);
        m_pCandWnd->ShowAt(pt);
        ApplyCandidateSelection(pContext);
        return;
    }

    // SKK dictionary second: a real reading→word dictionary purpose-built for
    // IME use. Gives us correct, dictionary-grade conversions for known words
    // without an LLM round-trip and without the model's hallucination problem
    // (gemma4:12b will happily invent "丁寧解" for "ていれいかい" if asked).
    if (auto* skk = SkkDictionary::GetGlobal(); skk && skk->IsLoaded())
    {
        auto skkHits = skk->Lookup(reading);
        if (!skkHits.empty() && m_pCandWnd)
        {
            // SKK indexes uninflected readings, so a hit on a conjugated
            // verb form ("みた", "もえた", "おりた") finds proper-noun
            // homophones or okuri-ari leftovers ("三田" / "燃え立" /
            // "下り立") instead of the obvious "見た" / "燃えた" / "下りた".
            // MeCab can rebuild the inflected kanji form; prepend it.
            if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
            {
                std::wstring prevTop = skkHits.front();
                size_t       beforeN = skkHits.size();
                skkHits = bunsetsu::MergeMecabVerbForms(reading, *mecab, skkHits);
                if (skkHits.size() != beforeN || skkHits.front() != prevTop)
                {
                    wchar_t logbuf[256];
                    swprintf_s(logbuf,
                               L"[GenerativeIME] SKK+MeCab merge: top %s -> %s (%zu cands)\n",
                               prevTop.c_str(),
                               skkHits.empty() ? L"(none)" : skkHits.front().c_str(),
                               skkHits.size());
                    OutputDebugStringW(logbuf);
                }
            }
            if (m_pLearning) skkHits = m_pLearning->Reorder(reading, skkHits);
            m_lastReading = reading;
            m_pCandWnd->SetCandidates(skkHits);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);

            // Kick off an async context-aware reorder when there's something
            // to reorder. The candidate window stays usable in the meantime;
            // if the user makes a selection before reorder lands, we drop
            // the result on arrival to avoid surprising them.
            if (skkHits.size() >= 2 && !m_recentContext.empty())
            {
                StartReorderAsync(pContext, reading, skkHits);
            }
            return;
        }

        // No whole-reading match. Try MeCab morphological analysis first —
        // it correctly handles cases SKK greedy split can't ("きょうはかいぎ"
        // becomes 今日/は/会議 instead of き/ょうはかいぎ). MeCab + UniDic
        // gives us the canonical kanji form as `lemma` for free; we augment
        // each morpheme with SKK candidates for variant choices (雨/飴/天).
        if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
        {
            auto parts = bunsetsu::SplitMecab(reading, *mecab, skk);
            if (!parts.empty() && bunsetsu::AnyHit(parts) && m_pCandWnd)
            {
                wchar_t logbuf[200];
                m_lastReading = reading;

                std::wstring shownTop;
                if (parts.size() == 1)
                {
                    // Single-morpheme input (e.g. "あるく" → 1 verb form).
                    // The whole-reading SKK lookup didn't fire (no surface
                    // entry), but MeCab handed us a perfectly good kanji
                    // form plus its SKK alternates. Show the bunsetsu's
                    // full candidate list so the user can pick variants.
                    swprintf_s(logbuf,
                               L"[GenerativeIME] MeCab single morpheme: %zu candidates, top=%s\n",
                               parts[0].candidates.size(),
                               parts[0].candidates.empty() ? L"(none)"
                                   : parts[0].candidates[0].c_str());
                    OutputDebugStringW(logbuf);
                    m_pCandWnd->SetCandidates(parts[0].candidates);
                    shownTop = parts[0].candidates.empty() ? std::wstring{} : parts[0].candidates[0];
                }
                else
                {
                    // Multi-morpheme: Phase A still only shows the joined
                    // 1st-of-each as a single candidate. Per-bunsetsu
                    // selection is Phase B.
                    std::wstring combined = bunsetsu::JoinSelected(parts);
                    swprintf_s(logbuf,
                               L"[GenerativeIME] MeCab split: %zu parts -> %s\n",
                               parts.size(), combined.c_str());
                    OutputDebugStringW(logbuf);
                    m_pCandWnd->SetCandidates({ combined });
                    shownTop = combined;
                }

                POINT pt = QueryCandidateAnchorPos(pContext);
                m_pCandWnd->ShowAt(pt);
                ApplyCandidateSelection(pContext);

                // #13: when MeCab's split looks like the kind of over-literal
                // answer UniDic-Lite tends to give for everyday phrases
                // (3+ morphemes, lemma contains a rare-in-practice kanji
                // like 顎 / 所為 / 為), fire Ollama in parallel and let it
                // overrule. The user sees MeCab's answer immediately; the
                // LLM result lands a beat later and prepends saner choices.
                if (bunsetsu::LooksSuspect(reading, *mecab))
                {
                    OutputDebugStringW(L"[GenerativeIME] MeCab split looks suspect — kicking Ollama fallback\n");
                    StartMecabSupplementAsync(pContext, reading, shownTop);
                }
                return;
            }
        }

        // MeCab declined to give a useful split — fall back to greedy SKK.
        // This is rare in practice (MeCab handles most well-formed kana)
        // but happens for completely OOV chunks, where greedy at least gets
        // SOMETHING onto the screen before Ollama takes over.
        auto parts = bunsetsu::SplitGreedy(reading, *skk);
        if (parts.size() >= 2 && bunsetsu::AnyHit(parts) && m_pCandWnd)
        {
            std::wstring combined = bunsetsu::JoinSelected(parts);
            wchar_t logbuf[160];
            swprintf_s(logbuf,
                       L"[GenerativeIME] SKK greedy split: %zu parts -> %s\n",
                       parts.size(), combined.c_str());
            OutputDebugStringW(logbuf);

            m_lastReading = reading;
            m_pCandWnd->SetCandidates({ combined });
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);
            return;
        }
    }

    auto* pending = new PendingOllamaRequest(this, pContext, reading, m_recentContext);
    HWND hwnd = m_hwndMsg;

    OutputDebugStringW(L"[GenerativeIME] Ollama: convert begin (async)\n");

    // Detach: worker is fire-and-forget. Lifetime is managed by `pending`
    // ownership transfer to PostMessage. If PostMessage fails we delete here.
    std::thread([pending, hwnd]()
    {
        std::wstring prompt;
        prompt += L"あなたは日本語のかな漢字変換 IME です。\n";
        prompt += L"入力されたひらがなの読みを、もっとも自然で一般的な漢字かな混じり表記に変換します。\n";
        prompt += L"\n";
        prompt += L"ルール:\n";
        prompt += L"1. JSON のみ返す。形式: {\"candidates\":[{\"text\":\"…\"}]}\n";
        prompt += L"2. 最大 5 候補を、文脈に最も合うもの順に並べる。1 番目は最も自然な単語。\n";
        prompt += L"3. 国語辞典に載っている実在の単語のみを候補にする。造語・当て字・意味の通らない漢字の組み合わせは絶対に含めない。\n";
        prompt += L"4. 適切な漢字変換がない場合はひらがな・カタカナのまま返す。\n";
        prompt += L"\n";
        if (!pending->recentContext.empty())
        {
            prompt += L"直前までの文章 (文脈の手がかり):\n";
            prompt += L"「";
            prompt += pending->recentContext;
            prompt += L"」\n";
            prompt += L"この文脈の続きとして自然な変換を選んでください。\n";
            prompt += L"\n";
        }
        prompt += L"読み: ";
        prompt += pending->reading;
        prompt += L"\n";

        ollama::GenerateOptions opts;
        opts.model       = L"gemma4:12b";
        opts.prompt      = prompt;
        opts.jsonFormat  = true;
        opts.temperature = 0.2;
        opts.numPredict  = 256;
        opts.keepAlive   = L"30m";
        opts.think       = false;
        opts.timeoutMs   = 60000;

        auto resp = ollama::Generate(opts);
        pending->hr         = resp.hr;
        pending->httpStatus = resp.httpStatus;
        if (SUCCEEDED(resp.hr) && !resp.response.empty())
        {
            pending->candidates = ExtractAllCandidates(resp.response);
        }

        if (!PostMessageW(hwnd, WM_OLLAMA_DONE, 0, (LPARAM)pending))
        {
            // Posting failed (e.g. window already destroyed). Free here to
            // avoid leaking the request + its AddRef'd context.
            delete pending;
        }
    }).detach();
}

void CTextService::HandleOllamaDone(PendingOllamaRequest* pending)
{
    if (!pending) return;
    wchar_t logbuf[200];
    swprintf_s(logbuf, L"[GenerativeIME] Ollama: hr=0x%08X http=%u candidates=%zu\n",
               (unsigned)pending->hr, (unsigned)pending->httpStatus, pending->candidates.size());
    OutputDebugStringW(logbuf);

    // Only apply if the composition is still live. If the user typed more
    // romaji or hit Backspace since Space, m_pComposition is probably still
    // valid but the candidates we got are now stale relative to the new
    // reading — we still show them since the user explicitly asked.
    if (m_pComposition && SUCCEEDED(pending->hr) && !pending->candidates.empty() && m_pCandWnd)
    {
        auto cands = pending->candidates;
        if (m_pLearning) cands = m_pLearning->Reorder(pending->reading, cands);
        m_lastReading = pending->reading;
        m_pCandWnd->SetCandidates(cands);
        POINT pt = QueryCandidateAnchorPos(pending->context);
        m_pCandWnd->ShowAt(pt);
        ApplyCandidateSelection(pending->context);
    }
    delete pending;
}

void CTextService::StartReorderAsync(ITfContext* pContext,
                                     const std::wstring& reading,
                                     const std::vector<std::wstring>& candidates)
{
    if (!m_hwndMsg || candidates.size() < 2) return;

    // Bump the sequence; the worker pins the new value into its request, and
    // anything older that comes back later is rejected as stale.
    unsigned seq = ++m_reorderSeq;
    auto* req = new PendingOllamaReorderRequest(this, pContext, reading, m_recentContext,
                                                candidates, seq);
    HWND hwnd = m_hwndMsg;

    OutputDebugStringW(L"[GenerativeIME] Ollama reorder: begin (async)\n");

    std::thread([req, hwnd]()
    {
        std::wstring prompt;
        prompt += L"あなたは日本語の IME のリランカーです。\n";
        prompt += L"下記の変換候補リストを、直前までの文章の流れに最も合うよう並び替えてください。\n";
        prompt += L"候補テキストは絶対に変更しないでください。順序の並び替えのみ行います。\n";
        prompt += L"JSON のみ返す。形式: {\"order\":[0始まりの新しい順序のインデックス配列]}\n";
        prompt += L"配列にはすべての候補のインデックスを過不足なく含めてください。\n";
        prompt += L"\n";
        prompt += L"直前までの文章: 「";
        prompt += req->recentContext;
        prompt += L"」\n";
        prompt += L"読み: ";
        prompt += req->reading;
        prompt += L"\n";
        prompt += L"候補:\n";
        for (size_t i = 0; i < req->original.size(); ++i)
        {
            wchar_t buf[16];
            swprintf_s(buf, L"%zu: ", i);
            prompt += buf;
            prompt += req->original[i];
            prompt += L"\n";
        }

        ollama::GenerateOptions opts;
        opts.model       = L"gemma4:12b";
        opts.prompt      = prompt;
        opts.jsonFormat  = true;
        opts.temperature = 0.1;
        opts.numPredict  = 128;          // we only need a tiny index array
        opts.keepAlive   = L"30m";
        opts.think       = false;
        opts.timeoutMs   = 30000;

        auto resp = ollama::Generate(opts);
        if (SUCCEEDED(resp.hr) && !resp.response.empty())
        {
            auto order = ExtractIntArray(resp.response, L"order");
            // Build the reordered list; drop out-of-range / duplicate indices.
            std::vector<bool> seen(req->original.size(), false);
            std::vector<std::wstring> out;
            out.reserve(req->original.size());
            for (int idx : order)
            {
                if (idx < 0 || (size_t)idx >= req->original.size()) continue;
                if (seen[idx]) continue;
                seen[idx] = true;
                out.push_back(req->original[idx]);
            }
            // Append anything the model omitted so we never lose candidates.
            for (size_t i = 0; i < req->original.size(); ++i)
            {
                if (!seen[i]) out.push_back(req->original[i]);
            }
            // Only adopt the reorder if it actually changed something. Saves
            // a redundant SetCandidates round-trip and the UI flicker that
            // would come with it.
            if (out != req->original) req->reordered = std::move(out);
        }

        if (!PostMessageW(hwnd, WM_OLLAMA_REORDER_DONE, 0, (LPARAM)req))
        {
            delete req;
        }
    }).detach();
}

void CTextService::HandleOllamaReorderDone(PendingOllamaReorderRequest* pending)
{
    if (!pending) return;

    // Stale: another lookup started while we were thinking. Drop silently.
    if (pending->seq != m_reorderSeq)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama reorder: stale (seq mismatch)\n");
        delete pending;
        return;
    }

    if (pending->reordered.empty())
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama reorder: no-op or failed\n");
        delete pending;
        return;
    }

    // Stale by content: user typed more / committed / opened a new conversion.
    // The candidate window may still be visible but for a different reading.
    if (!m_pCandWnd || !m_pCandWnd->IsVisible() || m_lastReading != pending->reading)
    {
        delete pending;
        return;
    }

    // Don't yank the highlight out from under the user mid-selection.
    if (m_pCandWnd->GetSelectedIndex() != 0)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama reorder: user already moved, skipping\n");
        delete pending;
        return;
    }

    OutputDebugStringW(L"[GenerativeIME] Ollama reorder: applying\n");
    m_pCandWnd->SetCandidates(pending->reordered);
    // The 0-th candidate changed; repaint the composition so the inline
    // preview matches the new "1st" candidate the user is implicitly hovering.
    if (pending->tfContext) ApplyCandidateSelection(pending->tfContext);
    delete pending;
}

void CTextService::StartMecabSupplementAsync(ITfContext* pContext,
                                             const std::wstring& reading,
                                             const std::wstring& mecabTop)
{
    if (!m_hwndMsg || reading.empty()) return;

    unsigned seq = ++m_reorderSeq;
    auto* req = new PendingOllamaFallbackRequest(this, pContext, reading, m_recentContext,
                                                 mecabTop, seq);
    HWND hwnd = m_hwndMsg;

    std::thread([req, hwnd]()
    {
        // Prompt note: we tell the model MeCab's answer and ask it to do
        // better, rather than asking blind. This lets gemma compare against
        // the literal-but-wrong choice and rule it out — empirically that
        // gives noticeably less "model just repeats what MeCab said"
        // behavior than a vanilla convert prompt.
        std::wstring prompt;
        prompt += L"あなたは日本語のかな漢字変換 IME の補助モデルです。\n";
        prompt += L"形態素解析器が以下の読みを変換した結果は文脈上不自然な可能性が高いです。\n";
        prompt += L"もっと自然な変換候補を最大 3 つ提案してください。\n";
        prompt += L"\n";
        prompt += L"ルール:\n";
        prompt += L"1. JSON のみ返す。形式: {\"candidates\":[{\"text\":\"…\"}]}\n";
        prompt += L"2. 国語辞典に載っている実在の単語・自然な複合語のみ。\n";
        prompt += L"3. 「所為」「為」「居る」「出来る」「御」「様」など、現代日本語であまり書かない漢字表記は避ける。\n";
        prompt += L"4. 形態素解析器の答えと同じ提案はしない。\n";
        prompt += L"\n";
        if (!req->recentContext.empty())
        {
            prompt += L"直前までの文章: 「";
            prompt += req->recentContext;
            prompt += L"」\n";
        }
        prompt += L"読み: ";
        prompt += req->reading;
        prompt += L"\n";
        prompt += L"形態素解析器の答え: ";
        prompt += req->mecabTop;
        prompt += L"\n";

        ollama::GenerateOptions opts;
        opts.model       = L"gemma4:12b";
        opts.prompt      = prompt;
        opts.jsonFormat  = true;
        opts.temperature = 0.2;
        opts.numPredict  = 192;
        opts.keepAlive   = L"30m";
        opts.think       = false;
        // Generous timeout: gemma4:12b cold-load is ~90s on CPU-only boxes
        // and we'd rather have the user see a late candidate-list update
        // than silently drop the request after 30s. The Activate-time
        // warmup keeps subsequent calls in the sub-second range.
        opts.timeoutMs   = 120000;

        auto resp = ollama::Generate(opts);
        req->hr = resp.hr;
        if (SUCCEEDED(resp.hr) && !resp.response.empty())
        {
            req->candidates = ExtractAllCandidates(resp.response);
        }

        if (!PostMessageW(hwnd, WM_OLLAMA_FALLBACK_DONE, 0, (LPARAM)req))
        {
            delete req;
        }
    }).detach();
}

// Fire-and-forget warmup of the Ollama model. We don't care about the
// response — the whole point is to make the model resident before the
// user's first real query, so the per-request opts.timeoutMs doesn't
// have to swallow a 90s cold-load. Called from Activate.
void CTextService::StartOllamaWarmupAsync()
{
    std::thread([]()
    {
        ollama::GenerateOptions opts;
        opts.model       = L"gemma4:12b";
        opts.prompt      = L"warmup";
        opts.jsonFormat  = false;
        opts.temperature = 0.0;
        opts.numPredict  = 4;
        opts.keepAlive   = L"30m";
        opts.think       = false;
        opts.timeoutMs   = 180000;

        OutputDebugStringW(L"[GenerativeIME] Ollama: warmup begin (async)\n");
        auto resp = ollama::Generate(opts);
        wchar_t buf[128];
        swprintf_s(buf,
                   L"[GenerativeIME] Ollama: warmup done hr=0x%08X http=%u\n",
                   (unsigned)resp.hr, (unsigned)resp.httpStatus);
        OutputDebugStringW(buf);
    }).detach();
}

void CTextService::HandleOllamaFallbackDone(PendingOllamaFallbackRequest* pending)
{
    if (!pending) return;

    // Stale: another async op (reorder or another fallback) raced ahead.
    if (pending->seq != m_reorderSeq)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama fallback: stale (seq mismatch)\n");
        delete pending;
        return;
    }

    if (FAILED(pending->hr) || pending->candidates.empty())
    {
        wchar_t logbuf[160];
        swprintf_s(logbuf,
                   L"[GenerativeIME] Ollama fallback: hr=0x%08X candidates=%zu — dropping\n",
                   (unsigned)pending->hr, pending->candidates.size());
        OutputDebugStringW(logbuf);
        delete pending;
        return;
    }

    // Stale by content: composition / reading changed since we kicked off.
    if (!m_pCandWnd || !m_pCandWnd->IsVisible() || m_lastReading != pending->reading)
    {
        delete pending;
        return;
    }

    // Don't override a candidate the user is actively selecting.
    if (m_pCandWnd->GetSelectedIndex() != 0)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama fallback: user already moved, skipping\n");
        delete pending;
        return;
    }

    // Prepend Ollama's suggestions ahead of MeCab's answer. We dedup against
    // mecabTop (model was told not to repeat it but sometimes does anyway)
    // and keep mecabTop as a trailing fallback the user can still scroll to.
    std::vector<std::wstring> merged;
    merged.reserve(pending->candidates.size() + 1);
    for (auto& c : pending->candidates)
    {
        if (c == pending->mecabTop) continue;
        if (std::find(merged.begin(), merged.end(), c) == merged.end())
        {
            merged.push_back(std::move(c));
        }
    }
    if (!pending->mecabTop.empty() &&
        std::find(merged.begin(), merged.end(), pending->mecabTop) == merged.end())
    {
        merged.push_back(pending->mecabTop);
    }

    if (merged.empty())
    {
        delete pending;
        return;
    }

    wchar_t logbuf[200];
    swprintf_s(logbuf,
               L"[GenerativeIME] Ollama fallback: applying %zu cands, top=%s (was %s)\n",
               merged.size(), merged.front().c_str(), pending->mecabTop.c_str());
    OutputDebugStringW(logbuf);

    if (m_pLearning) merged = m_pLearning->Reorder(pending->reading, merged);
    m_pCandWnd->SetCandidates(merged);
    if (pending->tfContext) ApplyCandidateSelection(pending->tfContext);
    delete pending;
}

// Best-effort screen position for the candidate popup. Tries three sources
// in order: (1) TSF's GetTextExt on the live composition range (most accurate,
// works in modern apps that don't expose a Win32 caret), (2) Win32 caret via
// GUITHREADINFO, (3) mouse cursor as last-ditch fallback.
POINT CTextService::QueryCandidateAnchorPos(ITfContext* pContext)
{
    POINT pt = { 0, 0 };
    if (pContext && m_pComposition)
    {
        CGetRectSession* sess = new CGetRectSession(pContext, m_pComposition, &pt);
        HRESULT hrSession = S_OK;
        HRESULT hr = pContext->RequestEditSession(m_tfClientId, sess,
            TF_ES_SYNC | TF_ES_READ, &hrSession);
        sess->Release();
        if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && (pt.x != 0 || pt.y != 0))
        {
            return pt;
        }
    }
    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(0, &gti) && gti.hwndCaret)
    {
        pt.x = gti.rcCaret.left;
        pt.y = gti.rcCaret.bottom + 2;
        ClientToScreen(gti.hwndCaret, &pt);
        return pt;
    }
    GetCursorPos(&pt);
    pt.y += 20;
    return pt;
}

// Replace the composition range with whatever's currently selected in the
// candidate window. Called on initial show and whenever Up/Down moves the cursor.
void CTextService::ApplyCandidateSelection(ITfContext* pContext)
{
    if (!pContext || !m_pCandWnd) return;
    std::wstring picked = m_pCandWnd->GetSelected();
    if (picked.empty()) return;
    RequestEditSession(pContext, EditAction::Update, picked);
    m_compositionConverted = TRUE;
}

// If the user picked a candidate (via Space/↓/Tab) and then starts typing
// the next chunk (alpha / symbol / etc.) without explicitly hitting Enter,
// auto-commit the converted text first so the new keystroke begins a fresh
// composition. Without this, the new keystroke would extend m_romajiBuffer
// and BuildCompositionDisplay would re-derive hiragana from it, throwing
// away the chosen kanji.
void CTextService::CommitConvertedIfAny(ITfContext* pContext)
{
    if (!m_compositionConverted || !m_pComposition) return;
    if (m_pCandWnd)
    {
        std::wstring picked = m_pCandWnd->GetSelected();
        if (m_pLearning && !m_lastReading.empty())
        {
            m_pLearning->Record(m_lastReading, picked);
        }
        AppendCommittedText(picked);
    }
    if (pContext) RequestEditSession(pContext, EditAction::EndCommit, L"");
    m_romajiBuffer.clear();
    m_compositionConverted = FALSE;
    m_lastReading.clear();
    if (m_pCandWnd) m_pCandWnd->Hide();
}

// Append `text` to the rolling context buffer and clamp to the recent window.
// We keep the buffer bounded so a hours-long session doesn't bloat memory and
// so the LLM context we eventually send stays short (latency-sensitive path).
void CTextService::AppendCommittedText(const std::wstring& text)
{
    if (text.empty()) return;
    m_recentContext.append(text);
    if (m_recentContext.size() > kRecentContextMax)
    {
        m_recentContext.erase(0, m_recentContext.size() - kRecentContextMax);
    }
}

LRESULT CALLBACK CTextService::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<CTextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self)
    {
        if (msg == WM_OLLAMA_DONE)
        {
            self->HandleOllamaDone(reinterpret_cast<PendingOllamaRequest*>(lParam));
            return 0;
        }
        if (msg == WM_OLLAMA_REORDER_DONE)
        {
            self->HandleOllamaReorderDone(reinterpret_cast<PendingOllamaReorderRequest*>(lParam));
            return 0;
        }
        if (msg == WM_OLLAMA_FALLBACK_DONE)
        {
            self->HandleOllamaFallbackDone(reinterpret_cast<PendingOllamaFallbackRequest*>(lParam));
            return 0;
        }
        if (msg == WM_LANGBAR_MENU)
        {
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            self->ShowLangBarMenu(x, y);
            return 0;
        }
        if (msg == WM_SET_IME_MODE)
        {
            // wParam encodes ImeMode (0=Off, 1=Hiragana, 2=FullKatakana,
            // 3=HalfKatakana, 4=FullAlnum). Clamp to known values.
            int v = (int)wParam;
            {
                wchar_t buf[128];
                swprintf_s(buf, L"[GenerativeIME] WM_SET_IME_MODE this=%p hwnd=%p wParam=%d\n",
                           self, hwnd, v);
                OutputDebugStringW(buf);
            }
            if (v < 0 || v > 4) v = 1;
            self->SetImeMode(static_cast<ImeMode>(v));
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Disabled in the rollback — see CLangBarItemButton::OnClick comment.
// Kept as a no-op so the WM_LANGBAR_MENU handler in StaticWndProc still has
// a target; we'll re-enable when the menu hosting is redesigned.
void CTextService::ShowLangBarMenu(int /*x*/, int /*y*/) {}

HWND CTextService::GetPopupOwnerHwnd() const
{
    return m_pCandWnd ? m_pCandWnd->GetHwnd() : nullptr;
}

// Changes the shaping mode (affects how the romaji buffer is rendered into
// the composition) AND syncs the IME on/off state to match (Off mode disables
// the IME entirely; any other mode keeps it on).
void CTextService::SetImeMode(ImeMode mode)
{
    {
        wchar_t buf[160];
        swprintf_s(buf, L"[GenerativeIME] SetImeMode enter this=%p mode=%d cur isImeOn=%d cur imeMode=%d\n",
                   this, (int)mode, (int)m_isImeOn, (int)m_imeMode);
        OutputDebugStringW(buf);
    }

    m_imeMode = mode;
    BOOL wantOn = (mode != ImeMode::Off);

    // Drive on/off + LangBar repaint directly. We don't rely on the OPENCLOSE
    // OnChange callback to propagate the new state to the pill — that route
    // is observably late when the change originates from the tray menu
    // (off → on with a non-default mode showed the old icon until the next
    // 半角/全角 key tap). Updating in-process state synchronously fixes that;
    // OnChange will still fire and SyncImeStateFromCompartments will no-op
    // because the values already match.
    if (wantOn != m_isImeOn)
    {
        m_isImeOn = wantOn;
        if (!m_isImeOn) m_romajiBuffer.clear();
        SetImeOpenClose(wantOn);
    }
    if (m_pLangBarItem) m_pLangBarItem->UpdateMode();

    {
        wchar_t buf[128];
        swprintf_s(buf, L"[GenerativeIME] SetImeMode exit isImeOn=%d imeMode=%d\n",
                   (int)m_isImeOn, (int)m_imeMode);
        OutputDebugStringW(buf);
    }
}

HRESULT CTextService::InitMessageWindow()
{
    if (m_hwndMsg) return S_OK;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kMsgWndClass;
    RegisterClassExW(&wc); // ignore "already registered" error from re-Activate

    m_hwndMsg = CreateWindowExW(0, kMsgWndClass, nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, this);
    return m_hwndMsg ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

void CTextService::UninitMessageWindow()
{
    if (m_hwndMsg)
    {
        DestroyWindow(m_hwndMsg);
        m_hwndMsg = nullptr;
    }
    // Intentionally do not UnregisterClass: another CTextService instance
    // may still be alive in the same process.
}

STDMETHODIMP CTextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum)
{
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new CEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CTextService::GetDisplayAttributeInfo(REFGUID guidInfo, ITfDisplayAttributeInfo** ppInfo)
{
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    if (!IsEqualGUID(guidInfo, c_guidDisplayAttributeInput)) return E_INVALIDARG;
    *ppInfo = new CDisplayAttributeInfoInput();
    return *ppInfo ? S_OK : E_OUTOFMEMORY;
}

// wParam in TSF OnKey* is a virtual-key code. VK 'A'-'Z' map to 0x41-0x5A.
static bool IsAlphaKey(WPARAM wParam)
{
    return (wParam >= 'A' && wParam <= 'Z');
}

// OEM-* virtual keys that produce printable ASCII punctuation on the
// standard JIS / US layouts. We claim them while the IME is on so we can
// route the resolved character through the kana table (",", "." → "、", "。").
static bool IsSymbolKey(WPARAM wParam)
{
    if (wParam >= VK_OEM_1 && wParam <= VK_OEM_3) return true;   // 0xBA-0xC0
    if (wParam >= VK_OEM_4 && wParam <= VK_OEM_8) return true;   // 0xDB-0xDF
    if (wParam == VK_OEM_PLUS || wParam == VK_OEM_COMMA
     || wParam == VK_OEM_MINUS || wParam == VK_OEM_PERIOD) return true;
    return false;
}

// Resolves a (vk + scan code + modifier state) tuple to the literal char the
// keyboard layout would produce. Used so we can map " ?" / "<" / etc. through
// the same kana table without hard-coding shift behavior.
static wchar_t ResolveSymbolChar(WPARAM wParam, LPARAM lParam)
{
    BYTE keyState[256] = {};
    GetKeyboardState(keyState);
    UINT scanCode = (lParam >> 16) & 0xFF;
    wchar_t buf[8] = {};
    // flags=2 (Win10+): "don't change kernel keyboard state" — leaves dead-key
    // sequences alone so a stray accent doesn't get consumed by our peek.
    int n = ToUnicode((UINT)wParam, scanCode, keyState, buf, 8, 2);
    return (n > 0) ? buf[0] : L'\0';
}

// Half/Full-width key handling (VK_KANJI / VK_OEM_AUTO / VK_OEM_ENLW) was
// removed: intercepting those keys destabilized the whole shell — explorer.exe
// and even DebugView went unresponsive, likely because eating the OS-level
// mode-switch VKs broke some downstream listener / re-entered our OnChange.
// IME on/off can be done via LangBar click later; the key-flip route needs
// the proper PreserveKey / Tip-aware approach instead of brute-force eating.
// Eaten keys while the IME is on:
//  - alpha keys (romaji input)
//  - OEM symbol keys (punctuation, will become 「、」「。」 etc.)
//  - Backspace while a romaji buffer exists to walk back through
//  - Enter / Escape / Space while a composition is live
//  - Up / Down / 1-9 while the candidate window is up
bool CTextService::ShouldEat(WPARAM wParam) const
{
    if (!m_isImeOn) return false;
    if (IsAlphaKey(wParam)) return true;
    if (IsSymbolKey(wParam)) return true;
    if (wParam == VK_BACK && !m_romajiBuffer.empty()) return true;
    if ((wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_SPACE) && m_pComposition) return true;
    if (m_pCandWnd && m_pCandWnd->IsVisible())
    {
        if (wParam == VK_UP || wParam == VK_DOWN) return true;
        if (wParam == VK_TAB) return true;
        if (wParam == VK_NEXT || wParam == VK_PRIOR) return true;
        if (wParam >= '1' && wParam <= '9') return true;
    }
    if (m_pComposition && wParam >= VK_F6 && wParam <= VK_F10) return true;
    return false;
}

STDMETHODIMP CTextService::OnSetFocus(BOOL /*fForeground*/)
{
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = ShouldEat(wParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    // Mode-switch key handling intentionally absent — see ShouldEat() comment.
    if (!m_isImeOn) return S_OK;

    if (IsAlphaKey(wParam))
    {
        // If a candidate was already chosen (m_compositionConverted), close it
        // out first so this alpha key starts a NEW composition rather than
        // mutating m_romajiBuffer underneath the kanji.
        CommitConvertedIfAny(pic);
        m_romajiBuffer.push_back(static_cast<wchar_t>(wParam - 'A' + 'a'));
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        if (pic)
        {
            EditAction action = (m_pComposition == nullptr) ? EditAction::StartAndUpdate : EditAction::Update;
            RequestEditSession(pic, action, display);
        }
        *pfEaten = TRUE;
    }
    else if (IsSymbolKey(wParam))
    {
        wchar_t ch = ResolveSymbolChar(wParam, lParam);
        if (ch != L'\0' && ch >= 0x20 && ch < 0x7F)
        {
            // Same as alpha path: commit the previously chosen kanji first
            // so the symbol doesn't overwrite it via BuildCompositionDisplay.
            CommitConvertedIfAny(pic);
            m_romajiBuffer.push_back(ch);
            m_compositionConverted = FALSE;
            if (m_pCandWnd) m_pCandWnd->Hide();
            std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
            if (pic)
            {
                EditAction action = (m_pComposition == nullptr) ? EditAction::StartAndUpdate : EditAction::Update;
                RequestEditSession(pic, action, display);

                // Auto-commit on terminal punctuation (, . ! ?). This matches
                // typical IME behavior: the user wants to keep typing the
                // next sentence without manually pressing Enter to confirm.
                if (ch == L',' || ch == L'.' || ch == L'!' || ch == L'?')
                {
                    RequestEditSession(pic, EditAction::EndCommit, L"");
                    m_romajiBuffer.clear();
                }
            }
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_BACK && !m_romajiBuffer.empty())
    {
        m_romajiBuffer.pop_back();
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        if (pic)
        {
            EditAction action = m_romajiBuffer.empty() ? EditAction::EndCancel : EditAction::Update;
            RequestEditSession(pic, action, display);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_RETURN && m_pComposition)
    {
        if (m_compositionConverted)
        {
            // Range already holds the converted text. Remember the user's choice
            // so the same reading puts that candidate first next time.
            if (m_pCandWnd)
            {
                std::wstring picked = m_pCandWnd->GetSelected();
                if (m_pLearning && !m_lastReading.empty())
                {
                    m_pLearning->Record(m_lastReading, picked);
                }
                AppendCommittedText(picked);
            }
            if (pic) RequestEditSession(pic, EditAction::EndCommit, L"");
        }
        else
        {
            // No conversion happened: resolve any trailing lone "n" to ん and commit.
            auto r = romaji::Convert(m_romajiBuffer);
            std::wstring finalText = r.hira + romaji::FinalizeTrailingN(r.remaining);
            AppendCommittedText(finalText);
            if (pic) RequestEditSession(pic, EditAction::EndCommit, finalText);
        }
        m_romajiBuffer.clear();
        m_compositionConverted = FALSE;
        m_lastReading.clear();
        if (m_pCandWnd) m_pCandWnd->Hide();
        *pfEaten = TRUE;
    }
    else if (wParam == VK_ESCAPE && m_pComposition)
    {
        if (pic) RequestEditSession(pic, EditAction::EndCancel, L"");
        m_romajiBuffer.clear();
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        *pfEaten = TRUE;
    }
    else if (wParam == VK_SPACE && m_pComposition)
    {
        // When the popup is already up, Space cycles to the next candidate
        // (matches typical IME behavior). Otherwise kick off a fresh async
        // Ollama call.
        if (m_pCandWnd && m_pCandWnd->IsVisible())
        {
            m_pCandWnd->SelectNext();
            ApplyCandidateSelection(pic);
        }
        else if (pic)
        {
            TryOllamaConvertAsync(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_DOWN && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->SelectNext();
        if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_UP && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->SelectPrev();
        if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_TAB && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        // Tab forward; Shift+Tab backward.
        if (GetKeyState(VK_SHIFT) < 0) m_pCandWnd->SelectPrev();
        else                            m_pCandWnd->SelectNext();
        if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_NEXT && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->PageNext();
        if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_PRIOR && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->PagePrev();
        if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam >= VK_F6 && wParam <= VK_F10 && m_pComposition)
    {
        // MS-IME-compatible function-key conversions, applied to the current
        // romaji buffer. Each is treated as a final form — Enter then commits
        // the range as-is via the m_compositionConverted path.
        auto r = romaji::Convert(m_romajiBuffer);
        std::wstring hira = r.hira + romaji::FinalizeTrailingN(r.remaining);
        std::wstring text;
        switch (wParam)
        {
        case VK_F6:  text = hira; break;
        case VK_F7:  text = ToFullKatakana(hira); break;
        case VK_F8:  text = ToHalfKatakana(ToFullKatakana(hira)); break;
        case VK_F9:  text = ToFullWidthAscii(m_romajiBuffer); break;
        case VK_F10: text = m_romajiBuffer; break;
        }
        if (m_pCandWnd) m_pCandWnd->Hide();
        if (pic && !text.empty()) RequestEditSession(pic, EditAction::Update, text);
        m_compositionConverted = TRUE;
        *pfEaten = TRUE;
    }
    else if (wParam >= '1' && wParam <= '9' && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        // Number key: jump to that row of the current page (the rendered
        // index is 1-based per page, not per full candidate list).
        int idx = m_pCandWnd->GetPageStart() + (int)(wParam - '1');
        if (idx < (int)m_pCandWnd->Count())
        {
            m_pCandWnd->SelectIndex(idx);
            ApplyCandidateSelection(pic);
            std::wstring picked = m_pCandWnd->GetSelected();
            if (m_pLearning && !m_lastReading.empty())
            {
                m_pLearning->Record(m_lastReading, picked);
            }
            AppendCommittedText(picked);
            if (pic) RequestEditSession(pic, EditAction::EndCommit, L"");
            m_romajiBuffer.clear();
            m_compositionConverted = FALSE;
            m_lastReading.clear();
            m_pCandWnd->Hide();
        }
        *pfEaten = TRUE;
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = ShouldEat(wParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*pic*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = ShouldEat(wParam) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pic*/, REFGUID rguid, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    if (IsEqualGUID(rguid, c_guidKeyKanji))
    {
        SetImeOpenClose(!m_isImeOn);
        *pfEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, c_guidKeyImeOn))
    {
        SetImeOpenClose(TRUE);
        *pfEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, c_guidKeyImeOff))
    {
        SetImeOpenClose(FALSE);
        *pfEaten = TRUE;
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnCompositionTerminated(TfEditCookie /*ecWrite*/, ITfComposition* /*pComposition*/)
{
    // Called when the OS forcibly ends the composition (focus change, mouse click, etc.).
    OutputDebugStringW(L"[GenerativeIME] OnCompositionTerminated\n");
    SetComposition(nullptr);
    m_romajiBuffer.clear();
    m_compositionConverted = FALSE;
    if (m_pCandWnd) m_pCandWnd->Hide();
    return S_OK;
}

void CTextService::SetComposition(ITfComposition* pComposition)
{
    if (m_pComposition) { m_pComposition->Release(); m_pComposition = nullptr; }
    if (pComposition)
    {
        m_pComposition = pComposition;
        m_pComposition->AddRef();
    }
}

HRESULT CTextService::RequestEditSession(ITfContext* pContext, EditAction action, const std::wstring& text)
{
    CEditSession* pSession = new CEditSession(this, pContext, action, text);

    // First try synchronous read-write.
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(m_tfClientId, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    if (FAILED(hr) || FAILED(hrSession))
    {
        hrSession = S_OK;
        hr = pContext->RequestEditSession(m_tfClientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
        wchar_t buf[200];
        swprintf_s(buf, L"[GenerativeIME] RequestEditSession fell back to async hr=0x%08X session=0x%08X\n",
                   (unsigned)hr, (unsigned)hrSession);
        OutputDebugStringW(buf);
    }
    pSession->Release();
    return hr;
}
