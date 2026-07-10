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
#include "modernranking.h"
#include "masks.h"
#include "alphaspell.h"
#include <algorithm>
#include <stdio.h>
#include <thread>
#include <vector>
#include <unordered_map>
#include <ShlObj.h>   // SHGetKnownFolderPath / FOLDERID_RoamingAppData for the misconversion log

constexpr UINT WM_OLLAMA_DONE = WM_USER + 1;
constexpr UINT WM_LANGBAR_MENU = WM_USER + 2;
constexpr UINT WM_SET_IME_MODE = WM_USER + 3; // wParam = 1 to turn ON, 0 to turn OFF
constexpr UINT WM_OLLAMA_REORDER_DONE = WM_USER + 4;
constexpr UINT WM_OLLAMA_FALLBACK_DONE = WM_USER + 5;
constexpr UINT WM_ACRONYM_DONE = WM_USER + 6;
constexpr wchar_t kMsgWndClass[] = L"GenerativeIME_MsgWnd_v1";

// 前方宣言: 定義は IsAlphaKey 近く (~L3730)。使用は commit 系関数
// (CommitConvertedIfAny 等) から先に呼ばれるため、ここで宣言だけしておく。
static size_t BracketPairCaretBackShift(const std::wstring& text);
static bool IsCloseBracketChar(wchar_t c);

// Forward declaration — full body sits near LogMisconversionAttempt at
// the bottom of this file. Prototype up here so early callers
// (InitPreservedKeys, OnPreservedKey) can reach it.
static void AppendDebugLine(const wchar_t* line);

// Posted from worker thread back to the IME thread via PostMessage.
// Owned by the worker until PostMessage handoff; then owned by WndProc which
// deletes it after HandleOllamaDone returns.
struct PendingOllamaRequest
{
    CTextService* service;
    ITfContext* context; // AddRef'd on construction, Release'd on destruction
    std::wstring reading;
    std::wstring recentContext; // recently committed text — snapshot for the prompt
    std::vector<std::wstring> candidates; // all parsed "text" values
    HRESULT hr;
    DWORD httpStatus;

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
    CTextService* service;
    ITfContext* tfContext;
    std::wstring reading;
    std::wstring recentContext;
    std::wstring mecabTop; // MeCab's answer, passed to the prompt as "what NOT to repeat"
    std::vector<std::wstring> candidates; // filled by worker
    unsigned seq;
    HRESULT hr;

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
    CTextService* service;
    ITfContext* tfContext; // AddRef'd; needed for composition repaint after reorder
    std::wstring reading;
    std::wstring recentContext;
    std::vector<std::wstring> original; // candidates we showed the user immediately
    std::vector<std::wstring> reordered; // filled by worker; empty if reorder failed
    unsigned seq; // discarded on arrival if service's seq has moved on

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

// Async LLM acronym expansion. Fired when an all-uppercase alnum composition
// ("ＩＭＦ") isn't in the built-in AcronymExpansions table. The worker asks
// Ollama for the meaning; on arrival the IME appends the answers behind the
// width/case candidates already shown. `base` is that on-screen list, snapshot
// at fire time; `display` is m_lastReading for staleness matching.
struct PendingAcronymRequest
{
    CTextService* service;
    ITfContext* tfContext;
    std::wstring acronym; // half-width uppercase key ("IMF")
    std::wstring display; // composition text when fired (== m_lastReading)
    std::vector<std::wstring> base; // candidates already shown, to append behind
    std::vector<std::wstring> candidates; // filled by worker
    unsigned seq;
    HRESULT hr;

    PendingAcronymRequest(CTextService* s, ITfContext* c, std::wstring a,
                          std::wstring disp, std::vector<std::wstring> b, unsigned sequence)
        : service(s), tfContext(c), acronym(std::move(a)), display(std::move(disp)),
          base(std::move(b)), seq(sequence), hr(E_FAIL)
    {
        if (tfContext) tfContext->AddRef();
    }

    ~PendingAcronymRequest()
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
            if (c >= 0x3041 && c <= 0x3096) out.push_back(static_cast<wchar_t>(c + 0x60));
            else out.push_back(c);
        }
        return out;
    }

    // Full-width Katakana -> Half-width Katakana. Voiced/semi-voiced sounds
    // expand to two code units (base + ﾞ / ﾟ) as is standard for halfwidth.
    std::wstring ToHalfKatakana(const std::wstring& kata)
    {
        static const std::unordered_map<wchar_t, std::wstring> map = {
            {L'ア', L"ｱ"}, {L'イ', L"ｲ"}, {L'ウ', L"ｳ"}, {L'エ', L"ｴ"}, {L'オ', L"ｵ"},
            {L'カ', L"ｶ"}, {L'キ', L"ｷ"}, {L'ク', L"ｸ"}, {L'ケ', L"ｹ"}, {L'コ', L"ｺ"},
            {L'サ', L"ｻ"}, {L'シ', L"ｼ"}, {L'ス', L"ｽ"}, {L'セ', L"ｾ"}, {L'ソ', L"ｿ"},
            {L'タ', L"ﾀ"}, {L'チ', L"ﾁ"}, {L'ツ', L"ﾂ"}, {L'テ', L"ﾃ"}, {L'ト', L"ﾄ"},
            {L'ナ', L"ﾅ"}, {L'ニ', L"ﾆ"}, {L'ヌ', L"ﾇ"}, {L'ネ', L"ﾈ"}, {L'ノ', L"ﾉ"},
            {L'ハ', L"ﾊ"}, {L'ヒ', L"ﾋ"}, {L'フ', L"ﾌ"}, {L'ヘ', L"ﾍ"}, {L'ホ', L"ﾎ"},
            {L'マ', L"ﾏ"}, {L'ミ', L"ﾐ"}, {L'ム', L"ﾑ"}, {L'メ', L"ﾒ"}, {L'モ', L"ﾓ"},
            {L'ヤ', L"ﾔ"}, {L'ユ', L"ﾕ"}, {L'ヨ', L"ﾖ"},
            {L'ラ', L"ﾗ"}, {L'リ', L"ﾘ"}, {L'ル', L"ﾙ"}, {L'レ', L"ﾚ"}, {L'ロ', L"ﾛ"},
            {L'ワ', L"ﾜ"}, {L'ヲ', L"ｦ"}, {L'ン', L"ﾝ"},
            {L'ガ', L"ｶﾞ"}, {L'ギ', L"ｷﾞ"}, {L'グ', L"ｸﾞ"}, {L'ゲ', L"ｹﾞ"}, {L'ゴ', L"ｺﾞ"},
            {L'ザ', L"ｻﾞ"}, {L'ジ', L"ｼﾞ"}, {L'ズ', L"ｽﾞ"}, {L'ゼ', L"ｾﾞ"}, {L'ゾ', L"ｿﾞ"},
            {L'ダ', L"ﾀﾞ"}, {L'ヂ', L"ﾁﾞ"}, {L'ヅ', L"ﾂﾞ"}, {L'デ', L"ﾃﾞ"}, {L'ド', L"ﾄﾞ"},
            {L'バ', L"ﾊﾞ"}, {L'ビ', L"ﾋﾞ"}, {L'ブ', L"ﾌﾞ"}, {L'ベ', L"ﾍﾞ"}, {L'ボ', L"ﾎﾞ"},
            {L'パ', L"ﾊﾟ"}, {L'ピ', L"ﾋﾟ"}, {L'プ', L"ﾌﾟ"}, {L'ペ', L"ﾍﾟ"}, {L'ポ', L"ﾎﾟ"},
            {L'ァ', L"ｧ"}, {L'ィ', L"ｨ"}, {L'ゥ', L"ｩ"}, {L'ェ', L"ｪ"}, {L'ォ', L"ｫ"},
            {L'ャ', L"ｬ"}, {L'ュ', L"ｭ"}, {L'ョ', L"ｮ"}, {L'ッ', L"ｯ"},
            {L'ヴ', L"ｳﾞ"},
            {L'ー', L"ｰ"}, {L'、', L"､"}, {L'。', L"｡"}, {L'「', L"｢"}, {L'」', L"｣"}, {L'・', L"･"},
        };
        std::wstring out;
        out.reserve(kata.size());
        for (wchar_t c : kata)
        {
            auto it = map.find(c);
            if (it != map.end()) out += it->second;
            else out.push_back(c);
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
            if (c >= 0x21 && c <= 0x7E) out.push_back(static_cast<wchar_t>(c + 0xFEE0));
            else if (c == L' ') out.push_back(L'　');
            else out.push_back(c);
        }
        return out;
    }

    // Widen ONLY ASCII digits '0'-'9' to '０'-'９'. Used by DisplayForMode
    // in 全角ひらがな/全角カタカナ so a bare digit renders full-width, while
    // the romaji buffer keeps the half-width char so symbols::LookupAll("1")
    // still finds ①/一/Ⅰ etc. on Space.
    std::wstring WidenAsciiDigits(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            if (c >= L'0' && c <= L'9') out.push_back(static_cast<wchar_t>(c + 0xFEE0));
            else out.push_back(c);
        }
        return out;
    }

    // Split an LLM acronym answer of the shape "日本語 (English)" /
    // "日本語（English）" into its two halves so they land as separate
    // candidates, matching the built-in dictionary's {日本語, 英語フル}
    // layout. gemma tends to fuse the two despite the prompt asking for
    // separate entries. Returns the string unchanged (single element) when
    // it isn't a clean "head (tail)" with a trailing close paren.
    std::vector<std::wstring> SplitAcronymPiece(const std::wstring& s)
    {
        if (s.size() < 3) return {s};
        wchar_t open = 0;
        if (s.back() == L')') open = L'(';
        else if (s.back() == L'）') open = L'（';
        else return {s};

        size_t pos = s.rfind(open);
        if (pos == std::wstring::npos || pos == 0) return {s};

        auto trim = [](std::wstring x)
        {
            auto isws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == 0x3000; };
            size_t b = 0, e = x.size();
            while (b < e && isws(x[b])) ++b;
            while (e > b && isws(x[e - 1])) --e;
            return x.substr(b, e - b);
        };
        std::wstring head = trim(s.substr(0, pos));
        std::wstring inner = trim(s.substr(pos + 1, s.size() - pos - 2));
        if (head.empty() || inner.empty()) return {s};
        return {head, inner};
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
                // Widen digits so a full-width-katakana-mode digit shows 全角.
                return WidenAsciiDigits(ToFullKatakana(r.hira) + ToFullKatakana(r.remaining));
            }
        case ImeMode::HalfKatakana:
            {
                auto r = romaji::Convert(romaji);
                return ToHalfKatakana(ToFullKatakana(r.hira)) + r.remaining;
            }
        case ImeMode::FullAlnum:
            return ToFullWidthAscii(romaji);
        case ImeMode::Hiragana:
            // 全角ひらがな mode: bare digits render full-width ('１') to match
            // MS-IME, but the buffer keeps the half-width char so Space still
            // finds the ①/一/Ⅰ candidates keyed on "1" in the symbol dict.
            return WidenAsciiDigits(BuildHiraganaDisplay(romaji));
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
                case L'"': out += L'"';
                    break;
                case L'\\': out += L'\\';
                    break;
                case L'/': out += L'/';
                    break;
                case L'n': out += L'\n';
                    break;
                case L't': out += L'\t';
                    break;
                case L'r': out += L'\r';
                    break;
                default: out += e;
                    break;
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
            if (jsonBody[pos] == L'-')
            {
                num += L'-';
                pos++;
            }
            while (pos < close && iswdigit(jsonBody[pos])) { num += jsonBody[pos++]; }
            if (!num.empty())
            {
                try { result.push_back(std::stoi(num)); }
                catch (...)
                {
                    /* skip malformed entries */
                }
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
    if (m_pComposition)
    {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
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
        swprintf_s(buf, L"[GenerativeIME] InitKeyEventSink failed hr=0x%08X\n", static_cast<unsigned>(hr));
        OutputDebugStringW(buf);
        Deactivate();
        return hr;
    }

    HRESULT hrPk = InitPreservedKeys();
    wchar_t pkbuf[96];
    swprintf_s(pkbuf, L"[GenerativeIME] InitPreservedKeys hr=0x%08X\n", static_cast<unsigned>(hrPk));
    OutputDebugStringW(pkbuf);

    HRESULT hrComp = SetIMEStateCompartments(TRUE);
    wchar_t cbuf[96];
    swprintf_s(cbuf, L"[GenerativeIME] SetIMEStateCompartments(TRUE) hr=0x%08X\n", static_cast<unsigned>(hrComp));
    OutputDebugStringW(cbuf);

    HRESULT hrAtom = InitDisplayAttributeGuidAtom();
    wchar_t abuf[128];
    swprintf_s(abuf, L"[GenerativeIME] InitDisplayAttributeGuidAtom hr=0x%08X atom=0x%08X\n",
               static_cast<unsigned>(hrAtom), static_cast<unsigned>(g_gaDisplayAttributeInput));
    OutputDebugStringW(abuf);

    HRESULT hrLb = InitLangBarItem();
    wchar_t lbuf[96];
    swprintf_s(lbuf, L"[GenerativeIME] InitLangBarItem hr=0x%08X\n", static_cast<unsigned>(hrLb));
    OutputDebugStringW(lbuf);

    HRESULT hrSink = InitCompartmentSinks();
    wchar_t sbuf[96];
    swprintf_s(sbuf, L"[GenerativeIME] InitCompartmentSinks hr=0x%08X\n", static_cast<unsigned>(hrSink));
    OutputDebugStringW(sbuf);
    SyncImeStateFromCompartments();

    HRESULT hrMsg = InitMessageWindow();
    wchar_t mbuf[96];
    swprintf_s(mbuf, L"[GenerativeIME] InitMessageWindow hr=0x%08X\n", static_cast<unsigned>(hrMsg));
    OutputDebugStringW(mbuf);

    if (!m_pCandWnd) m_pCandWnd = new CCandidateWindow();
    HRESULT hrCw = m_pCandWnd->Create();
    wchar_t cwbuf[96];
    swprintf_s(cwbuf, L"[GenerativeIME] CandidateWindow.Create hr=0x%08X\n", static_cast<unsigned>(hrCw));
    OutputDebugStringW(cwbuf);

    if (!m_pLearning) m_pLearning = new LearningStore();
    HRESULT hrLs = m_pLearning->Load();
    wchar_t lsbuf[96];
    swprintf_s(lsbuf, L"[GenerativeIME] LearningStore.Load hr=0x%08X\n", static_cast<unsigned>(hrLs));
    OutputDebugStringW(lsbuf);

    // Warm the dictionaries in the background. Doing this inline blocks
    // Activate for ~10 seconds (SKK = 4–6 MB text parse, MeCab UniDic-Lite
    // sys.dic = ~188 MB) and the user sees the IME pill but can't
    // actually convert until it finishes. Background warmup lets the
    // mode UI come up immediately; the first Space-conversion that needs
    // the dict blocks on std::call_once inside GetGlobal (same total
    // latency on first use, but at least romaji-mode typing works
    // straight away).
    std::thread([]()
    {
        SkkDictionary::GetGlobal();
        MecabAnalyzer::GetGlobal();
        OutputDebugStringW(L"[GenerativeIME] dict warmup complete\n");
    }).detach();

    // Also kick Ollama: gemma4:12b cold-loads in ~90s on a CPU-only box,
    // which would blow past the per-request timeout in the fallback path.
    // Fire it now (fire-and-forget) so by the time the user types the
    // model is resident and subsequent calls return in <1s.
    StartOllamaWarmupAsync();

    {
        wchar_t buf[128];
        swprintf_s(buf, L"[GenerativeIME] Activated this=%p msgWnd=%p clientId=%u\n",
                   this, m_hwndMsg, static_cast<unsigned>(m_tfClientId));
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
    if (m_pCandWnd)
    {
        m_pCandWnd->Destroy();
        delete m_pCandWnd;
        m_pCandWnd = nullptr;
    }
    if (m_pLearning)
    {
        delete m_pLearning;
        m_pLearning = nullptr;
    }
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

    hr = pKeystrokeMgr->AdviseKeyEventSink(m_tfClientId, this, TRUE);
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

    // TF_MOD_IGNORE_ALL_MODIFIER (0x80000000) makes the preserved-key
    // hit fire regardless of modifier state. The Japanese 半角/全角 key
    // physically emits VK_KANJI on some layouts and VK_OEM_AUTO on
    // others, and Windows can synthesize an implicit Alt modifier for
    // the grave-accent scan code that carries VK_KANJI on 106/109
    // keyboards. Without IGNORE_ALL_MODIFIER the preserved-key entry
    // never matches on those boxes and 半角/全角 silently no-ops.
    constexpr UINT kIgnoreAllMods = 0x80000000;
    auto preserve = [&](REFGUID guid, UINT vk, UINT mod, const wchar_t* desc)
    {
        TF_PRESERVEDKEY pk = {vk, mod};
        pMgr->PreserveKey(m_tfClientId, guid, &pk, desc, static_cast<ULONG>(wcslen(desc)));
    };
    preserve(c_guidKeyKanji, VK_KANJI, kIgnoreAllMods, L"GenerativeIME Toggle");
    preserve(c_guidKeyImeOn, VK_OEM_AUTO, kIgnoreAllMods, L"GenerativeIME ON");
    preserve(c_guidKeyImeOff, VK_OEM_ENLW, kIgnoreAllMods, L"GenerativeIME OFF");
    // Ctrl+Shift+F5 → misconversion logger. We used to register this as
    // a preserved key too, but observationally OnPreservedKey never fired
    // (nor did OnKeyDown, because preserved-key registration blocks the
    // regular key-sink path). Rely on ShouldEat + OnKeyDown alone —
    // that's already working for other Ctrl-modified keys we claim.
    //
    // Modifier journey:
    //   - Ctrl+F5 alone: conflicts with browser hard-refresh; a preserved
    //     key would permanently steal that shortcut.
    //   - Ctrl+Alt+F5: preserved-key registration succeeded (HRESULT 0)
    //     but delivery silently failed — Alt is a menu-accelerator
    //     modifier the OS starts interpreting before TSF matches, and
    //     Ctrl+Alt can fold into AltGr on some paths.
    //   - Ctrl+Shift+F5 via preserved key: same silent-drop symptom.
    //     Some hosts appear to swallow Ctrl+Shift combos before TSF's
    //     InputProcessorProfiles gets a look at them.
    //   - Ctrl+Shift+F5 via OnKeyDown (ShouldEat claim): the ordinary
    //     key-sink path this project already uses for every other Ctrl-
    //     eaten combo. Works.

    pMgr->Release();
    return S_OK;
}

void CTextService::UninitPreservedKeys()
{
    if (!m_pThreadMgr || m_tfClientId == TF_CLIENTID_NULL) return;

    ITfKeystrokeMgr* pMgr = nullptr;
    if (FAILED(m_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pMgr)) || !pMgr) return;

    TF_PRESERVEDKEY pk;
    pk = {VK_KANJI, 0};
    pMgr->UnpreserveKey(c_guidKeyKanji, &pk);
    pk = {VK_OEM_AUTO, 0};
    pMgr->UnpreserveKey(c_guidKeyImeOn, &pk);
    pk = {VK_OEM_ENLW, 0};
    pMgr->UnpreserveKey(c_guidKeyImeOff, &pk);
    // Best-effort UnpreserveKey in case an older DLL had it registered.
    pk = {VK_F5, TF_MOD_CONTROL | TF_MOD_SHIFT};
    pMgr->UnpreserveKey(c_guidKeyDebugLog, &pk);
    pk = {VK_F5, TF_MOD_CONTROL | TF_MOD_ALT};
    pMgr->UnpreserveKey(c_guidKeyDebugLog, &pk);
    pk = {VK_F5, TF_MOD_CONTROL};
    pMgr->UnpreserveKey(c_guidKeyDebugLog, &pk);

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
               static_cast<unsigned>(hrOpenClose), static_cast<unsigned>(hrConvMode));
    OutputDebugStringW(buf);
    return SUCCEEDED(hrOpenClose) ? S_OK : hrOpenClose;
}

// Maps our display attribute GUID to a TfGuidAtom for cheap reuse in every edit
// session. Cached in g_gaDisplayAttributeInput. Doing this once on Activate
// rather than per-composition keeps the hot path off CoCreateInstance.
HRESULT CTextService::InitDisplayAttributeGuidAtom()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, reinterpret_cast<void**>(&pCategoryMgr));
    if (FAILED(hr) || !pCategoryMgr) return hr;

    hr = pCategoryMgr->RegisterGUID(c_guidDisplayAttributeInput, &g_gaDisplayAttributeInput);
    if (SUCCEEDED(hr))
    {
        // Best-effort: BunsetsuFocus is only used by Phase B and we'd rather
        // run without per-bunsetsu highlighting than fail Activate.
        HRESULT hr2 = pCategoryMgr->RegisterGUID(c_guidDisplayAttributeBunsetsuFocus,
                                                 &g_gaDisplayAttributeBunsetsuFocus);
        wchar_t buf[160];
        swprintf_s(buf,
                   L"[GenerativeIME] RegisterGUID(BunsetsuFocus) hr=0x%08X atom=0x%08X\n",
                   static_cast<unsigned>(hr2), static_cast<unsigned>(g_gaDisplayAttributeBunsetsuFocus));
        OutputDebugStringW(buf);
    }
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
    if (m_pLangBarItem)
    {
        OutputDebugStringW(L"[GenerativeIME] LangBar: already initialized, skip\n");
        return S_OK;
    }

    ITfLangBarItemMgr* pMgr = nullptr;
    HRESULT hr = m_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pMgr);
    if (FAILED(hr) || !pMgr)
    {
        wchar_t b[120];
        swprintf_s(b, L"[GenerativeIME] LangBar: QI(ITfLangBarItemMgr) failed hr=0x%08X\n", static_cast<unsigned>(hr));
        OutputDebugStringW(b);
        return hr;
    }

    m_pLangBarItem = new CLangBarItemButton(this);
    if (!m_pLangBarItem)
    {
        pMgr->Release();
        return E_OUTOFMEMORY;
    }

    hr = pMgr->AddItem(m_pLangBarItem);
    wchar_t b[120];
    swprintf_s(b, L"[GenerativeIME] LangBar: AddItem hr=0x%08X (item=%p)\n", static_cast<unsigned>(hr), m_pLangBarItem);
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
        if (FAILED(h) || !pSource)
        {
            pComp->Release();
            return h;
        }

        h = pSource->AdviseSink(IID_ITfCompartmentEventSink,
                                static_cast<ITfCompartmentEventSink*>(this), pCookie);
        pSource->Release();

        if (FAILED(h))
        {
            pComp->Release();
            return h;
        }
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
    unadvise(&m_pCompConvMode, &m_dwCookieConvMode);
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
    if (!pContext || !m_hwndMsg) return;

    // An explicit conversion supersedes any speculative predictions — the
    // candidate window is about to show conversion results, so key routing
    // must fall back to the normal cycle-on-Space behavior.
    m_predictionActive = false;
    m_predictionReadings.clear();

    // Normal path: derive the reading from the romaji typed so far.
    // Reconvert path: the caller pre-loads m_lastReading with the host's
    // selected morpheme reading and clears m_romajiBuffer, so we fall
    // back to m_lastReading when the buffer is empty.
    std::wstring reading;
    if (!m_romajiBuffer.empty())
    {
        auto r = romaji::Convert(m_romajiBuffer);
        reading = r.hira + romaji::FinalizeTrailingN(r.remaining);
    }
    if (reading.empty()) reading = m_lastReading;
    if (reading.empty()) return;

    // Punctuation pair fast path. If the composition currently shows a
    // single full/half-width punctuation char (typed via IsSymbolKey
    // earlier), Space opens a tiny 2-item candidate window so the user
    // can swap between the typed form and its counterpart (「！」⇔「!」).
    // The typed form sits at index 0 so a plain Enter keeps what the
    // user got; ↓/Space picks the other one.
    if (m_pCandWnd)
    {
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        auto puncts = symbols::PunctPairs(display);
        if (!puncts.empty())
        {
            // Emoji forms of the punctuation (！ → ❗/❕, ？ → ❓/❔ via
            // SKK-JISYO.emoji) join behind the width pair. The typed form
            // keeps index 0, so a bare Enter still commits what was typed.
            if (auto* skk = SkkDictionary::GetGlobal(); skk && skk->IsLoaded())
            {
                for (auto& c : skk->Lookup(display))
                {
                    if (std::find(puncts.begin(), puncts.end(), c) == puncts.end())
                        puncts.push_back(std::move(c));
                }
            }
            m_lastReading = display;
            m_pCandWnd->SetCandidates(puncts);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);
            return;
        }
    }

    // Alphanumeric width/case fast path. When the composition is an all-
    // letter/digit run — e.g. 「ＩＭＥ」 typed via Shift+alpha in 全角ひらがな
    // mode — Space opens a candidate window offering the full/half-width and
    // upper/lower-case forms (ＩＭＥ / IME / ime / ｉｍｅ). The typed form sits
    // at index 0 so a bare Enter keeps it; ↓/Space cycles the alternates.
    // Runs before the Ollama/MeCab path because these have no kanji reading.
    if (m_pCandWnd)
    {
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        auto variants = symbols::AsciiWidthCaseVariants(display);
        if (variants.size() > 1)
        {
            // Also offer the kana the letters would have produced as romaji,
            // so someone who typed Shift+I,M,E can still reach 「いめ / イメ /
            // ｲﾒ」 without retyping. Only when the whole run parses cleanly to
            // kana (no leftover consonants) — otherwise the letters aren't a
            // valid reading and we skip the kana forms.
            std::wstring ascii;
            ascii.reserve(display.size());
            for (wchar_t c : display)
            {
                if (c >= 0xFF21 && c <= 0xFF3A) ascii.push_back(static_cast<wchar_t>(c - 0xFF21 + L'a'));
                else if (c >= 0xFF41 && c <= 0xFF5A) ascii.push_back(static_cast<wchar_t>(c - 0xFF41 + L'a'));
                else if (c >= L'A' && c <= L'Z') ascii.push_back(static_cast<wchar_t>(c - L'A' + L'a'));
                else if (c >= 0xFF10 && c <= 0xFF19) ascii.push_back(static_cast<wchar_t>(c - 0xFF10 + L'0'));
                else ascii.push_back(c);
            }
            auto r = romaji::Convert(ascii);
            if (r.remaining.empty() && !r.hira.empty())
            {
                std::wstring hira = r.hira;
                std::wstring kata = ToFullKatakana(hira);
                std::wstring half = ToHalfKatakana(kata);
                for (const std::wstring& k : {hira, kata, half})
                    if (std::find(variants.begin(), variants.end(), k) == variants.end())
                        variants.push_back(k);
            }
            // Acronym expansion: 「ＩＭＦ」→ 国際通貨基金 / International
            // Monetary Fund. Keyed on the half-width UPPERCASE form so both
            // 「ＩＭＦ」and 「ｉｍｆ」reach the same entry.
            std::wstring upper = ascii;
            for (auto& c : upper)
                if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
            auto dictExpansions = symbols::AcronymExpansions(upper);
            for (const auto& e : dictExpansions)
                if (std::find(variants.begin(), variants.end(), e) == variants.end())
                    variants.push_back(e);
            // Single-letter glyph variants (🇯/Ⓙ/ⓙ for "j"). The alnum
            // path is where a bare "j" lands (it's a valid alphanumeric
            // run of length 1), so this is where the regional-indicator
            // form has to be surfaced — the whole-reading fav path below
            // never fires for a fresh "j".
            for (auto& lv : symbols::LetterVariants(ascii))
            {
                if (std::find(variants.begin(), variants.end(), lv) == variants.end())
                    variants.push_back(lv);
            }
            // Two-letter ISO code → country flag emoji ("jp" → 🇯🇵).
            // Runs after AcronymExpansions so "us" still shows US /
            // United States above the flag, matching the "typed form is
            // the default" contract; the flag rides at the tail as an
            // extra option.
            for (auto& fl : symbols::FlagFromIso2(ascii))
            {
                if (std::find(variants.begin(), variants.end(), fl) == variants.end())
                    variants.push_back(fl);
            }
            m_lastReading = display;
            m_pCandWnd->SetCandidates(variants);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);

            // LLM fallback: no built-in entry for what looks like an acronym
            // (2-8 pure ASCII letters) → ask Ollama asynchronously and append
            // its expansions when they arrive. Dictionary hits skip the LLM so
            // known acronyms stay instant and offline. Digits or over-long
            // runs are almost never acronyms, so we don't spend a query on
            // them.
            if (dictExpansions.empty())
            {
                bool allLetters = upper.size() >= 2 && upper.size() <= 8;
                for (wchar_t c : upper)
                    if (c < L'A' || c > L'Z')
                    {
                        allLetters = false;
                        break;
                    }
                if (allLetters)
                    StartAcronymExpandAsync(pContext, upper, display, variants);
            }
            return;
        }
    }

    // Whole-reading learning fav fast path. If the user previously
    // committed something for this EXACT composite reading (e.g.
    // "こうほうぃんどう" → "候補ウィンドウ" after picking the Ollama
    // fallback), show it as the top candidate immediately — no MeCab
    // fragmentation, no Ollama latency.
    //
    // The fav sits at position 0, but we ALSO stack the SKK direct hits
    // behind it so the user can Space through to alternate homophones
    // without having to blacklist the fav first. Without this, a single
    // learned pick like 「かんじ→感じ」 permanently blocks reaching
    // 漢字/幹事/監事 for that reading. Only fav is guaranteed at 0.
    if (m_pLearning && m_pCandWnd)
    {
        // AppContext scopes the fav to the current process+window so a
        // learned「かんじ→感じ」in a chat window doesn't shadow
        //「かんじ→漢字」 in a code editor. Empty ctx (e.g. no
        // foreground window) falls back to the pre-2026-07-02 global
        // behavior via the cascade in LearningStore::GetFav.
        AppContext ctx = AppContext::Capture();
        std::wstring fav = m_pLearning->GetFav(reading, ctx);
        if (!fav.empty())
        {
            std::vector<std::wstring> cands = {fav};
            auto addUnique = [&cands](std::wstring c)
            {
                if (c.empty()) return;
                if (std::find(cands.begin(), cands.end(), c) == cands.end())
                    cands.push_back(std::move(c));
            };

            // Symbol dictionary hits (arrows, ℃, ㎢, bracket-pair variants,
            // etc.). Without this the fav path swallows non-SKK-covered
            // candidates the normal path would have shown — a fav learned
            // for a bracket-pair reading, for example, would hide the other
            // 8 bracket-pair variants entirely.
            for (auto& c : symbols::LookupAll(reading)) addUnique(std::move(c));

            if (auto* skk = SkkDictionary::GetGlobal(); skk && skk->IsLoaded())
            {
                auto hits = skk->Lookup(reading);
                // Same ReadsAs guard as the SKK direct-hit path below: drop
                // okuri-ari-synthesized garbage unless the reading has an
                // explicit direct entry the dict maintainer wrote by hand.
                if (!hits.empty() && !skk->HasDirectEntry(reading))
                {
                    if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
                    {
                        std::vector<std::wstring> clean;
                        clean.reserve(hits.size());
                        for (auto& c : hits)
                        {
                            if (bunsetsu::ReadsAs(c, reading, *mecab))
                                clean.push_back(std::move(c));
                        }
                        hits = std::move(clean);
                    }
                }
                // Promote corpus-preferred alternate to position 1 so Space
                // through the fav lands on the modern-preferred candidate
                // before random SKK homophones. e.g. fav=会 for かい keeps
                // 会 at 0 but reorders the rest so 回 (from ModernRanking)
                // appears BEFORE 快/介/回転… of raw SKK order.
                std::vector<std::wstring> tail = modernranking::PromoteToTop(
                    reading, std::vector<std::wstring>(hits));
                // Reconstruct conjugated verb/adjective forms from SKK
                // okuri-stem entries via MeCab, matching the SKK-only path
                // (line 1369). e.g. ちいさい has no direct SKK entry, only
                // ちいさi /小さ/; MergeMecabVerbForms glues that back into
                // 「小さい」. Without this the fav path stops at the raw SKK
                // stem and the user never sees the conjugated kanji form.
                if (auto* mecab = MecabAnalyzer::GetGlobal();
                    mecab && mecab->IsReady() && !skk->IsUserDictReading(reading))
                {
                    tail = bunsetsu::MergeMecabVerbForms(reading, *mecab, tail);
                }
                for (auto& c : tail) addUnique(std::move(c));
            }
            // Sensitive-reading mask variants (opt-in per-reading via
            // masks::Variants) appended at the end so a user can pick
            // ち〇ぽ / 〇んぽ / ちん〇 without leaving the composition.
            // Non-sensitive readings return empty and are unaffected.
            for (auto& m : masks::Variants(reading)) addUnique(std::move(m));
            for (auto& a : alphaspell::Spell(reading)) addUnique(std::move(a));
            // Letter-glyph variants (ｗ/W/Ｗ/🇼/Ⓦ/…) so a learned pick like
            // 「w→🇼」 doesn't lock the other forms out of reach.
            for (auto& lv : symbols::LetterVariants(reading)) addUnique(std::move(lv));
            m_lastReading = reading;
            m_pCandWnd->SetCandidates(cands);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);
            wchar_t logbuf[200];
            swprintf_s(logbuf,
                       L"[GenerativeIME] Whole-reading fav fast path: reading=%s fav=%s (%zu total cands)\n",
                       reading.c_str(), fav.c_str(), cands.size());
            OutputDebugStringW(logbuf);
            return;
        }
    }

    // ドキュメント文脈バイアス fast path: the reading matches a word that
    // already appears in the text around the caret — the user is very
    // likely retyping it, so surface the document's form first. Sits BELOW
    // the learning fav (an explicit past pick beats ambient document
    // vocabulary) and ABOVE the symbol / SKK paths. The map itself is
    // volatile per-document data; a pick here reaches LearningStore only
    // through the normal commit path, like any other candidate.
    if (m_pCandWnd && !m_docVocab.empty())
    {
        auto dv = m_docVocab.find(reading);
        if (dv != m_docVocab.end() && dv->second != reading)
        {
            std::vector<std::wstring> cands = {dv->second};
            // Stack SKK hits behind the document word so Space can still
            // reach the ordinary homophones (same shape as the fav path).
            if (auto* skk = SkkDictionary::GetGlobal(); skk && skk->IsLoaded())
            {
                auto hits = skk->Lookup(reading);
                if (!hits.empty() && !skk->HasDirectEntry(reading))
                {
                    if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
                    {
                        std::vector<std::wstring> clean;
                        clean.reserve(hits.size());
                        for (auto& c : hits)
                        {
                            if (bunsetsu::ReadsAs(c, reading, *mecab))
                                clean.push_back(std::move(c));
                        }
                        hits = std::move(clean);
                    }
                }
                std::vector<std::wstring> tail = modernranking::PromoteToTop(
                    reading, std::vector<std::wstring>(hits));
                for (auto& c : tail)
                {
                    if (std::find(cands.begin(), cands.end(), c) == cands.end())
                        cands.push_back(std::move(c));
                }
            }
            for (auto& mv : masks::Variants(reading))
            {
                if (std::find(cands.begin(), cands.end(), mv) == cands.end())
                    cands.push_back(std::move(mv));
            }
            for (auto& a : alphaspell::Spell(reading))
            {
                if (std::find(cands.begin(), cands.end(), a) == cands.end())
                    cands.push_back(std::move(a));
            }
            m_lastReading = reading;
            m_pCandWnd->SetCandidates(cands);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);
            wchar_t logbuf[200];
            swprintf_s(logbuf,
                       L"[GenerativeIME] DocVocab fast path: reading=%s word=%s (%zu total cands)\n",
                       reading.c_str(), dv->second.c_str(), cands.size());
            OutputDebugStringW(logbuf);
            return;
        }
    }

    // Local symbol dictionary first: instant, no LLM round-trip. If we get
    // a hit, show that and skip Ollama — the user typed "やじるし" because
    // they want an arrow, not whatever the model would guess at.
    auto symHits = symbols::LookupAll(reading);
    if (!symHits.empty() && m_pCandWnd)
    {
        // Kanji-containing homophones outrank the symbol. ちいさい typed
        // by a user in prose is 小さい 99% of the time; the "<" alternate
        // still needs to be reachable via ↓/Space, but should not sit at
        // position 0. Same for おおきい/大きい, みまん/未満, たかい/高い etc.
        // Kanji-check: a candidate contains at least one CJK Unified char.
        // Non-kanji SKK hits (ぷらす → ＋ is the SKK entry for ぷらす —
        // fullwidth, same glyph family as the symbol dict's ＋) stay OFF
        // the priority track so the ASCII + at symHits[0] remains default.
        auto hasKanji = [](const std::wstring& s) -> bool
        {
            for (wchar_t c : s)
            {
                if ((c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
                    (c >= 0x3400 && c <= 0x4DBF)) // CJK Extension A
                    return true;
            }
            return false;
        };

        std::vector<std::wstring> kanjiHead;
        std::vector<std::wstring> otherTail;

        // (a) MeCab conjugation reconstruction — handles adjectives / verbs
        //     that SKK stores as an okuri stem (ちいさい has no direct SKK
        //     entry, only ちいさi /小さ/; MergeMecabVerbForms glues that
        //     back into the kanji 「小さい」).
        if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
        {
            auto mecabForms = bunsetsu::MergeMecabVerbForms(reading, *mecab, {});
            for (auto& c : mecabForms)
            {
                if (hasKanji(c) && std::find(kanjiHead.begin(), kanjiHead.end(), c) == kanjiHead.end())
                    kanjiHead.push_back(std::move(c));
            }
        }

        // (b) SKK direct hits (大きい, 未満, 語/碁/後 for ご, 山/産 for さん,
        //     etc.). Same okuri-ari garbage filter as the main SKK path.
        if (auto* skk = SkkDictionary::GetGlobal(); skk && skk->IsLoaded())
        {
            auto hits = skk->Lookup(reading);
            if (!hits.empty() && !skk->HasDirectEntry(reading))
            {
                if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
                {
                    std::vector<std::wstring> clean;
                    clean.reserve(hits.size());
                    for (auto& c : hits)
                    {
                        if (bunsetsu::ReadsAs(c, reading, *mecab))
                            clean.push_back(std::move(c));
                    }
                    hits = std::move(clean);
                }
            }
            for (auto& c : hits)
            {
                auto& dst = hasKanji(c) ? kanjiHead : otherTail;
                if (std::find(dst.begin(), dst.end(), c) == dst.end())
                    dst.push_back(std::move(c));
            }
        }

        // (c) Symbols keep the middle. Dedup vs kanjiHead / otherTail so
        //     a symbol form the SKK dict already emitted (ぷらす → ＋)
        //     doesn't get re-added.
        std::vector<std::wstring> cands;
        cands.reserve(kanjiHead.size() + symHits.size() + otherTail.size());
        for (auto& c : kanjiHead) cands.push_back(std::move(c));
        for (auto& c : symHits)
        {
            if (std::find(cands.begin(), cands.end(), c) == cands.end())
                cands.push_back(std::move(c));
        }
        for (auto& c : otherTail)
        {
            if (std::find(cands.begin(), cands.end(), c) == cands.end())
                cands.push_back(std::move(c));
        }

        if (m_pLearning) cands = m_pLearning->Reorder(reading, cands);
        m_lastReading = reading;
        m_pCandWnd->SetCandidates(cands);
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

        // Filter out okuri-ari-synthesized entries whose kanji doesn't
        // read back as `reading` via MeCab. SkkDictionary::Load flattens
        // 「ですg /出過/」and「あかるi /明/」into m_entries so a bare
        // Lookup("です") returns [出過] — but 出過 reads as ですぎ
        // (出/で + 過/すぎ), not です. Without this filter the whole-
        // reading SKK path pins 出過 at bare-Enter default for「です」
        // and blocks MeCab's saner「です」kana-passthrough. If NO hits
        // read cleanly, drop the whole set and fall through to MeCab.
        //
        // Exception: if `reading` has an explicit okuri-nashi entry in
        // SKK-JISYO, skip the filter — the dict maintainer wrote it on
        // purpose. Otherwise greetings like 「こんにちわ /今日は/」 get
        // dropped because MeCab reads 今日は as きょうは, not こんにちわ.
        if (!skkHits.empty() && !skk->HasDirectEntry(reading))
        {
            if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
            {
                std::vector<std::wstring> clean;
                clean.reserve(skkHits.size());
                for (auto& c : skkHits)
                {
                    if (bunsetsu::ReadsAs(c, reading, *mecab))
                        clean.push_back(std::move(c));
                }
                skkHits = std::move(clean);
            }
        }

        // Corpus-derived top-candidate override. Applied here (before the
        // !empty check) so no-SKK-entry readings that we DO have a modern
        // preferred kanji for (行わ, 呼ば, に対して, ...) get promoted
        // via front-insert instead of falling through to MeCab bunsetsu.
        skkHits = modernranking::PromoteToTop(reading, std::move(skkHits));

        if (!skkHits.empty() && m_pCandWnd)
        {
            // SKK indexes uninflected readings, so a hit on a conjugated
            // verb form ("みた", "もえた", "おりた") finds proper-noun
            // homophones or okuri-ari leftovers ("三田" / "燃え立" /
            // "下り立") instead of the obvious "見た" / "燃えた" / "下りた".
            // MeCab can rebuild the inflected kanji form; prepend it.
            //
            // Skip this for a reading the user defined in their own
            // dictionary: that mapping is authoritative and must stay at the
            // head. Otherwise MeCab parses e.g. 「ふろった」 as a verb form
            // and prepends a spurious 「振ろった」 above the user's 「風呂った」.
            if (auto* mecab = MecabAnalyzer::GetGlobal();
                mecab && mecab->IsReady() && !skk->IsUserDictReading(reading))
            {
                std::wstring prevTop = skkHits.front();
                size_t beforeN = skkHits.size();
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
            // Append sensitive-reading mask variants after learning/reorder
            // so masks land at the tail of the candidate list, out of the way
            // of the primary conversion but reachable via ↓/Space cycling.
            for (auto& m : masks::Variants(reading))
            {
                if (std::find(skkHits.begin(), skkHits.end(), m) == skkHits.end())
                    skkHits.push_back(std::move(m));
            }
            // Acronym forms (あいえむいー → IME/ime) likewise ride at the
            // tail: dictionary words stay primary, the spelled-out letters
            // are one ↓ away.
            for (auto& a : alphaspell::Spell(reading))
            {
                if (std::find(skkHits.begin(), skkHits.end(), a) == skkHits.end())
                    skkHits.push_back(std::move(a));
            }
            m_lastReading = reading;
            m_pCandWnd->SetCandidates(skkHits);
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);

            // Kick off an async context-aware reorder when there's something
            // to reorder. The candidate window stays usable in the meantime;
            // if the user makes a selection before reorder lands, we drop
            // the result on arrival to avoid surprising them. Context now
            // includes the caret-window document slice, so this fires even
            // when nothing was committed through the IME this session
            // (e.g. resuming edits in an existing document).
            if (skkHits.size() >= 2 && !BuildLlmContext().empty())
            {
                StartReorderAsync(pContext, reading, skkHits);
            }
            return;
        }

        // Sensitive-reading fallback. For adult-vocabulary readings
        // (masks::Variants returns non-empty) whose whole-reading SKK
        // lookup came back empty, offer a minimal candidate window built
        // from hira surface + katakana + mask variants. This gives
        // 「ちんぽ」 / 「チンポ」 / 「ち〇ぽ」 / 「〇んぽ」 / 「ちん〇」
        // + katakana masks even though the SKK dict has no direct entry
        // for these words (adding them explicitly would land in a data
        // file we'd rather keep license-clean).
        {
            auto maskCands = masks::Variants(reading);
            if (!maskCands.empty() && m_pCandWnd)
            {
                std::vector<std::wstring> cands = {reading};
                // Katakana equivalent as second option, then all masks.
                std::wstring kata;
                kata.reserve(reading.size());
                for (wchar_t c : reading)
                {
                    int u = c;
                    if (u >= 0x3041 && u <= 0x3096) kata.push_back(static_cast<wchar_t>(u + 0x60));
                    else kata.push_back(c);
                }
                if (kata != reading) cands.push_back(kata);
                for (auto& m : maskCands)
                {
                    if (std::find(cands.begin(), cands.end(), m) == cands.end())
                        cands.push_back(std::move(m));
                }
                m_lastReading = reading;
                m_pCandWnd->SetCandidates(cands);
                POINT pt = QueryCandidateAnchorPos(pContext);
                m_pCandWnd->ShowAt(pt);
                ApplyCandidateSelection(pContext);
                return;
            }
        }

        // Acronym fallback. A reading that parses entirely as English
        // letter names (ゆーあーるえる, えーぴーあい, …) and has no SKK
        // entry is almost certainly a spelled-out acronym — MeCab's
        // morphological split of letter-name kana is garbage in comparison.
        // UPPER first (acronyms are usually written uppercase), then lower,
        // then the kana surfaces as escape hatches.
        {
            auto spelled = alphaspell::Spell(reading);
            if (!spelled.empty() && m_pCandWnd)
            {
                std::vector<std::wstring> cands = std::move(spelled);
                cands.push_back(reading);
                std::wstring kata;
                kata.reserve(reading.size());
                for (wchar_t c : reading)
                {
                    int u = c;
                    if (u >= 0x3041 && u <= 0x3096) kata.push_back(static_cast<wchar_t>(u + 0x60));
                    else kata.push_back(c);
                }
                if (kata != reading &&
                    std::find(cands.begin(), cands.end(), kata) == cands.end())
                    cands.push_back(std::move(kata));
                if (m_pLearning) cands = m_pLearning->Reorder(reading, cands);
                m_lastReading = reading;
                m_pCandWnd->SetCandidates(cands);
                POINT pt = QueryCandidateAnchorPos(pContext);
                m_pCandWnd->ShowAt(pt);
                ApplyCandidateSelection(pContext);
                return;
            }
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
                               parts[0].candidates.empty()
                                   ? L"(none)"
                                   : parts[0].candidates[0].c_str());
                    OutputDebugStringW(logbuf);
                    m_pCandWnd->SetCandidates(parts[0].candidates);
                    shownTop = parts[0].candidates.empty() ? std::wstring{} : parts[0].candidates[0];
                }
                else
                {
                    // Multi-morpheme: always enter Phase B. The previous
                    // "suspect split" branch showed a short seed + spinner
                    // and swapped the candidate list when Ollama returned,
                    // producing two visually different UIs for what the user
                    // perceived as "the same thing". Unify on Phase B:
                    // consistent split-and-navigate UX regardless of how
                    // MeCab feels about the input.
                    //
                    // Ollama still runs in the background — HandleOllamaFallbackDone
                    // now splits each whole-phrase suggestion along
                    // m_bunsetsuList's reading boundaries (via SplitByReadings)
                    // and merges the per-piece surfaces into the corresponding
                    // bunsetsu's candidate list. Suggestions that don't align
                    // with the current split are silently dropped, so a bad
                    // MeCab split can be rescued by resizing (Shift+←/→) and
                    // waiting for the LLM catch-up. No spinner — the UI stays
                    // stable and Ollama's picks trickle in behind the scenes.
                    std::wstring combined = bunsetsu::JoinSelected(parts);
                    swprintf_s(logbuf,
                               L"[GenerativeIME] MeCab split: %zu parts -> %s\n",
                               parts.size(), combined.c_str());
                    OutputDebugStringW(logbuf);
                    EnterBunsetsuMode(std::move(parts), pContext);
                    StartMecabSupplementAsync(pContext, reading, combined);
                    return;
                }

                POINT pt = QueryCandidateAnchorPos(pContext);
                m_pCandWnd->ShowAt(pt);
                ApplyCandidateSelection(pContext);
                // Single-morpheme cases used to fire an Ollama supplement
                // when the lemma looked rare (顎所為-tier); removed for the
                // same UX-unification reason as the multi-morpheme branch
                // above. Reorder (SKK candidate ranking) still runs behind
                // the scenes and is silent — no spinner, no list swap.
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
            m_pCandWnd->SetCandidates({combined});
            POINT pt = QueryCandidateAnchorPos(pContext);
            m_pCandWnd->ShowAt(pt);
            ApplyCandidateSelection(pContext);
            return;
        }
    }

    auto* pending = new PendingOllamaRequest(this, pContext, reading, BuildLlmContext());
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
            prompt += L"文脈 (「〔入力位置〕」は変換結果が挿入される位置):\n";
            prompt += L"「";
            prompt += pending->recentContext;
            prompt += L"」\n";
            prompt += L"この文脈に最も合う変換を選んでください。\n";
            prompt += L"\n";
        }
        prompt += L"読み: ";
        prompt += pending->reading;
        prompt += L"\n";

        ollama::GenerateOptions opts;
        opts.model = L"gemma4:12b";
        opts.prompt = prompt;
        opts.jsonFormat = true;
        opts.temperature = 0.2;
        opts.numPredict = 256;
        opts.keepAlive = L"30m";
        opts.think = false;
        opts.timeoutMs = 60000;

        auto resp = ollama::Generate(opts);
        pending->hr = resp.hr;
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
               static_cast<unsigned>(pending->hr), static_cast<unsigned>(pending->httpStatus),
               pending->candidates.size());
    OutputDebugStringW(logbuf);

    // Only apply if the composition is still live. If the user typed more
    // romaji or hit Backspace since Space, m_pComposition is probably still
    // valid but the candidates we got are now stale relative to the new
    // reading — we still show them since the user explicitly asked.
    // Exception: an active prediction popup means keystrokes DID land after
    // the Space that queued this request; clobbering the fresher speculative
    // list (and rewriting the composition via ApplyCandidateSelection) with
    // stale conversion results would corrupt what the user is mid-typing.
    if (m_predictionActive)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama: prediction active, dropping stale conversion result\n");
        delete pending;
        return;
    }
    // Phase B is showing the focused bunsetsu's list; replacing the window
    // with a whole-reading answer desyncs the window index from the
    // bunsetsu's own candidates and JoinSelected would run off the vector
    // (this is how the 🇼-hunting Tab-cycle crash took Chrome down). Same
    // policy as HandleOllamaFallbackDone: drop the response.
    if (InBunsetsuMode())
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama: skipping in Phase B mode\n");
        delete pending;
        return;
    }
    if (m_pComposition && SUCCEEDED(pending->hr) && !pending->candidates.empty() && m_pCandWnd)
    {
        auto cands = pending->candidates;
        // Drop suggestions whose reading drifted from the user's input
        // (gemma4:12b occasionally answers "だから" for a "せいで" prompt).
        // SKK / MeCab don't need this filter; only Ollama paths.
        //
        // Fallback: if the filter would empty the candidate list, keep
        // the original. UniDic-Lite assigns ONE reading per surface and
        // gets it wrong for common multi-reading kanji (私 → ワタクシ,
        // 明日 → アス), so a correct "私は学生" suggestion would fail
        // ReadsAs against typed "わたしはがくせい". Showing the unfiltered
        // LLM answer beats showing nothing.
        if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
        {
            std::vector<std::wstring> filtered = cands;
            std::erase_if(filtered,
                          [&](const std::wstring& c)
                          {
                              return !bunsetsu::ReadsAs(c, pending->reading, *mecab);
                          });
            if (filtered.empty())
            {
                OutputDebugStringW(
                    L"[GenerativeIME] Ollama: all filtered (UniDic vocab mismatch?), keeping unfiltered\n");
            }
            else if (filtered.size() != cands.size())
            {
                wchar_t logbuf[160];
                swprintf_s(logbuf,
                           L"[GenerativeIME] Ollama: dropped %zu off-reading candidates (%zu→%zu)\n",
                           cands.size() - filtered.size(), cands.size(), filtered.size());
                OutputDebugStringW(logbuf);
                cands = std::move(filtered);
            }
            else
            {
                cands = std::move(filtered);
            }
        }
        if (cands.empty())
        {
            OutputDebugStringW(L"[GenerativeIME] Ollama: no candidates, no update\n");
            delete pending;
            return;
        }
        if (m_pLearning) cands = m_pLearning->Reorder(pending->reading, cands);
        m_lastReading = pending->reading;
        m_pCandWnd->SetCandidates(cands);
        POINT pt = QueryCandidateAnchorPos(pending->context);
        m_pCandWnd->ShowAt(pt);
        ApplyCandidateSelection(pending->context);
    }
    delete pending;
}

std::wstring CTextService::BuildLlmContext() const
{
    if (!m_docContext.empty()) return m_docContext;
    return m_recentContext;
}

void CTextService::StartReorderAsync(ITfContext* pContext,
                                     const std::wstring& reading,
                                     const std::vector<std::wstring>& candidates)
{
    if (!m_hwndMsg || candidates.size() < 2) return;

    // Bump the sequence; the worker pins the new value into its request, and
    // anything older that comes back later is rejected as stale.
    unsigned seq = ++m_reorderSeq;
    auto* req = new PendingOllamaReorderRequest(this, pContext, reading, BuildLlmContext(),
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
        prompt += L"文脈 (「〔入力位置〕」は変換結果が挿入される位置): 「";
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
        opts.model = L"gemma4:12b";
        opts.prompt = prompt;
        opts.jsonFormat = true;
        opts.temperature = 0.1;
        opts.numPredict = 128; // we only need a tiny index array
        opts.keepAlive = L"30m";
        opts.think = false;
        opts.timeoutMs = 30000;

        auto resp = ollama::Generate(opts);
        if (SUCCEEDED(resp.hr) && !resp.response.empty())
        {
            auto order = ExtractIntArray(resp.response, L"order");
            const size_t N = req->original.size();

            // Permutation-validity gate. gemma4:12b was observed truncating
            // the returned index list (P1: 4/9, P12: 4/5) and duplicating an
            // index while dropping another (P4: idx 4 twice, idx 6 missing)
            // deterministically for specific prompts. The prior "forgiving
            // merger" (dedup + append missing at tail) silently promoted the
            // model's wrong pick to top-1 in those cases. Strict-reject keeps
            // structural safety: bad output leaves req->reordered empty and
            // HandleOllamaReorderDone drops it — user sees SKK order.
            bool valid_perm = (order.size() == N);
            if (valid_perm)
            {
                std::vector<bool> check(N, false);
                for (int idx : order)
                {
                    if (idx < 0 || static_cast<size_t>(idx) >= N || check[idx])
                    {
                        valid_perm = false;
                        break;
                    }
                    check[idx] = true;
                }
            }

            // RAW-invalidity log (before guard). Reflects the model's true
            // error rate; independent of whether we adopt the reorder.
            // Grep DebugView for "reorder:raw=" to compute broken rate.
            {
                auto reason = L"valid";
                if (!valid_perm)
                {
                    if (order.size() < N) reason = L"truncation";
                    else if (order.size() > N) reason = L"overrun";
                    else reason = L"dup-or-oor";
                }
                wchar_t buf[160];
                swprintf_s(buf, L"[GenerativeIME] Ollama reorder:raw=%s returned=%zu expected=%zu\n",
                           reason, order.size(), N);
                OutputDebugStringW(buf);
            }

            if (valid_perm)
            {
                std::vector<std::wstring> out;
                out.reserve(N);
                for (int idx : order) out.push_back(req->original[idx]);
                // Only adopt the reorder if it actually changed something.
                // Saves a redundant SetCandidates round-trip and UI flicker.
                if (out != req->original)
                {
                    req->reordered = std::move(out);
                    OutputDebugStringW(L"[GenerativeIME] Ollama reorder:adopted=1\n");
                }
                else
                {
                    OutputDebugStringW(L"[GenerativeIME] Ollama reorder:adopted=0 (identity)\n");
                }
            }
            else
            {
                // Strict reject. req->reordered stays empty; HandleOllamaReorderDone
                // takes the "no-op or failed" branch and drops silently.
                OutputDebugStringW(L"[GenerativeIME] Ollama reorder:adopted=0 (invalid)\n");
            }
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
    // A prediction popup counts as stale too — the reorder was for a
    // conversion list the speculative candidates have since replaced.
    if (!m_pCandWnd || !m_pCandWnd->IsVisible() || m_predictionActive ||
        m_lastReading != pending->reading)
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
    auto* req = new PendingOllamaFallbackRequest(this, pContext, reading, BuildLlmContext(),
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
        prompt +=
            L"5. 読みに「うぃ/うぇ/うぉ/ヴ/ふぁ/ふぃ/ふぇ/ふぉ/てぃ/でぃ/とぅ/どぅ/つぁ/いぇ/しぇ/じぇ/ちぇ」 等の外来音表記が含まれる場合は、対応するカタカナ (ウィ/ウェ/ウォ/ヴ/ファ/フィ/フェ/フォ/ティ/ディ/トゥ/ドゥ/ツァ/イェ/シェ/ジェ/チェ) を使った外来語の候補 (例: 「うぃんどう」→「ウィンドウ」、「こうほうぃんどう」→「候補ウィンドウ」) も積極的に検討してください。\n";
        prompt += L"6. 部分的なひらがな + カタカナ混じり (例:「候補」+「ウィンドウ」) は OK。\n";
        prompt += L"\n";
        if (!req->recentContext.empty())
        {
            prompt += L"文脈 (「〔入力位置〕」は変換結果が挿入される位置): 「";
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
        opts.model = L"gemma4:12b";
        opts.prompt = prompt;
        opts.jsonFormat = true;
        opts.temperature = 0.2;
        opts.numPredict = 192;
        opts.keepAlive = L"30m";
        opts.think = false;
        // Generous timeout: gemma4:12b cold-load is ~90s on CPU-only boxes
        // and we'd rather have the user see a late candidate-list update
        // than silently drop the request after 30s. The Activate-time
        // warmup keeps subsequent calls in the sub-second range.
        opts.timeoutMs = 120000;

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
        opts.model = L"gemma4:12b";
        opts.prompt = L"warmup";
        opts.jsonFormat = false;
        opts.temperature = 0.0;
        opts.numPredict = 4;
        opts.keepAlive = L"30m";
        opts.think = false;
        opts.timeoutMs = 180000;

        OutputDebugStringW(L"[GenerativeIME] Ollama: warmup begin (async)\n");
        auto resp = ollama::Generate(opts);
        wchar_t buf[128];
        swprintf_s(buf,
                   L"[GenerativeIME] Ollama: warmup done hr=0x%08X http=%u\n",
                   static_cast<unsigned>(resp.hr), static_cast<unsigned>(resp.httpStatus));
        OutputDebugStringW(buf);
    }).detach();
}

void CTextService::HandleOllamaFallbackDone(PendingOllamaFallbackRequest* pending)
{
    if (!pending) return;

    // Always clear the spinner once a response (or failure) is in hand.
    // Even the stale-drop / skip branches below should kill the animation
    // so the user doesn't think we're still thinking.
    if (m_pCandWnd) m_pCandWnd->SetOllamaPending(false);

    // Stale: another async op (reorder or another fallback) raced ahead.
    if (pending->seq != m_reorderSeq)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama fallback: stale (seq mismatch)\n");
        delete pending;
        return;
    }

    // The user typed past the conversion that queued this request and the
    // popup now shows speculative predictions for the newer input — don't
    // clobber those with a stale supplement.
    if (m_predictionActive)
    {
        OutputDebugStringW(L"[GenerativeIME] Ollama fallback: prediction active, dropping\n");
        delete pending;
        return;
    }

    // Phase B integration: split each whole-phrase suggestion into pieces
    // along m_bunsetsuList's reading boundaries and merge each piece into
    // the corresponding bunsetsu's candidate list. Suggestions that don't
    // cleanly split (misaligned boundaries or drifted reading) are dropped.
    // Reorder + repaint if the focused bunsetsu picked up a new head so
    // the user sees the LLM's take without any visible spinner or list-swap.
    if (InBunsetsuMode())
    {
        auto* mecabForSplit = MecabAnalyzer::GetGlobal();
        if (mecabForSplit && mecabForSplit->IsReady() && !m_bunsetsuList.empty())
        {
            std::vector<std::wstring> readings;
            readings.reserve(m_bunsetsuList.size());
            for (const auto& b : m_bunsetsuList) readings.push_back(b.reading);

            bool anyMerged = false;
            for (const auto& cand : pending->candidates)
            {
                auto pieces = bunsetsu::SplitByReadings(cand, readings, *mecabForSplit);
                if (pieces.size() != m_bunsetsuList.size()) continue;
                for (size_t i = 0; i < pieces.size(); ++i)
                {
                    if (pieces[i].empty()) continue;
                    auto& cur = m_bunsetsuList[i];
                    if (std::find(cur.candidates.begin(), cur.candidates.end(), pieces[i])
                        != cur.candidates.end())
                        continue;
                    // Prepend Ollama pieces to the head so the LLM's take
                    // beats the SKK / MeCab defaults; learning fav still
                    // reorders on top of this via the next Reorder pass.
                    cur.candidates.insert(cur.candidates.begin(), pieces[i]);
                    anyMerged = true;
                }
            }
            if (anyMerged)
            {
                if (m_pLearning)
                {
                    for (auto& b : m_bunsetsuList)
                    {
                        if (b.candidates.empty()) continue;
                        b.candidates = m_pLearning->Reorder(b.reading, b.candidates);
                        b.selected = 0;
                    }
                }
                RepaintBunsetsu(pending->tfContext);
            }
        }
        delete pending;
        return;
    }

    if (FAILED(pending->hr) || pending->candidates.empty())
    {
        wchar_t logbuf[160];
        swprintf_s(logbuf,
                   L"[GenerativeIME] Ollama fallback: hr=0x%08X candidates=%zu — dropping\n",
                   static_cast<unsigned>(pending->hr), pending->candidates.size());
        OutputDebugStringW(logbuf);
        delete pending;
        return;
    }

    // Drop suggestions whose reading drifted from the user's input. Same
    // rationale as the main-path filter — gemma sometimes answers with
    // a related word ("そのため" for "せいで") instead of a reading-
    // matched alternate ("せいで" / "精で"). Same fallback too: keep
    // unfiltered if everything would drop, to survive UniDic vocab
    // mismatches (私 → ワタクシ vs typed わたし).
    if (auto* mecab = MecabAnalyzer::GetGlobal(); mecab && mecab->IsReady())
    {
        std::vector<std::wstring> filtered = pending->candidates;
        std::erase_if(filtered,
                      [&](const std::wstring& c)
                      {
                          return !bunsetsu::ReadsAs(c, pending->reading, *mecab);
                      });
        if (filtered.empty())
        {
            OutputDebugStringW(
                L"[GenerativeIME] Ollama fallback: all filtered (UniDic vocab mismatch?), keeping unfiltered\n");
        }
        else if (filtered.size() != pending->candidates.size())
        {
            wchar_t logbuf[180];
            swprintf_s(logbuf,
                       L"[GenerativeIME] Ollama fallback: dropped %zu off-reading (%zu→%zu)\n",
                       pending->candidates.size() - filtered.size(),
                       pending->candidates.size(), filtered.size());
            OutputDebugStringW(logbuf);
            pending->candidates = std::move(filtered);
        }
        else
        {
            pending->candidates = std::move(filtered);
        }
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

void CTextService::StartAcronymExpandAsync(ITfContext* pContext,
                                           const std::wstring& acronym,
                                           const std::wstring& display,
                                           const std::vector<std::wstring>& base)
{
    if (!m_hwndMsg || acronym.empty()) return;

    unsigned seq = ++m_reorderSeq;
    auto* req = new PendingAcronymRequest(this, pContext, acronym, display, base, seq);
    HWND hwnd = m_hwndMsg;
    if (m_pCandWnd) m_pCandWnd->SetOllamaPending(true);

    std::thread([req, hwnd]()
    {
        std::wstring prompt;
        prompt += L"あなたは英語の略語（頭字語）辞典です。\n";
        prompt += L"次の略語が一般的に表す意味を展開してください。\n";
        prompt += L"\n";
        prompt += L"ルール:\n";
        prompt += L"1. JSON のみ返す。形式: {\"candidates\":[{\"text\":\"…\"}]}\n";
        prompt += L"2. 実在する一般的な意味のみ。日本語の正式名称と英語のフルスペルの両方を候補に含める（日本語→英語の順）。\n";
        prompt += L"3. 候補は最大 4 つ。最も一般的な意味を優先する。\n";
        prompt += L"4. 意味が分からない、または一般的な略語でない場合は candidates を空配列にする。\n";
        prompt += L"5. 説明や補足文は書かない。text は名称のみ。\n";
        prompt += L"\n";
        prompt += L"略語: ";
        prompt += req->acronym;
        prompt += L"\n";

        ollama::GenerateOptions opts;
        opts.model = L"gemma4:12b";
        opts.prompt = prompt;
        opts.jsonFormat = true;
        opts.temperature = 0.1;
        opts.numPredict = 192;
        opts.keepAlive = L"30m";
        opts.think = false;
        opts.timeoutMs = 120000;

        auto resp = ollama::Generate(opts);
        req->hr = resp.hr;
        if (SUCCEEDED(resp.hr) && !resp.response.empty())
        {
            req->candidates = ExtractAllCandidates(resp.response);
        }

        if (!PostMessageW(hwnd, WM_ACRONYM_DONE, 0, (LPARAM)req))
        {
            delete req;
        }
    }).detach();
}

void CTextService::HandleAcronymDone(PendingAcronymRequest* pending)
{
    if (!pending) return;

    if (m_pCandWnd) m_pCandWnd->SetOllamaPending(false);

    // Stale: another async candidate edit raced ahead, or the user typed on /
    // committed / switched conversions since we fired.
    if (pending->seq != m_reorderSeq)
    {
        delete pending;
        return;
    }
    if (m_predictionActive || InBunsetsuMode() ||
        !m_pCandWnd || !m_pCandWnd->IsVisible() ||
        m_lastReading != pending->display)
    {
        delete pending;
        return;
    }
    if (FAILED(pending->hr) || pending->candidates.empty())
    {
        delete pending;
        return;
    }
    // Don't reshuffle the list out from under a user who has already navigated.
    if (m_pCandWnd->GetSelectedIndex() != 0)
    {
        delete pending;
        return;
    }

    // Append the LLM's expansions behind the width/case forms already shown,
    // deduped against them (and against the acronym itself, which the model
    // sometimes echoes back).
    std::vector<std::wstring> merged = pending->base;
    for (auto& c : pending->candidates)
    {
        if (c.empty() || c == pending->acronym) continue;
        // Split "日本語 (English)" into two candidates so LLM answers line up
        // with the built-in dictionary's separate 日本語 / 英語フル entries.
        for (auto& piece : SplitAcronymPiece(c))
        {
            if (piece.empty() || piece == pending->acronym) continue;
            if (std::find(merged.begin(), merged.end(), piece) == merged.end())
                merged.push_back(std::move(piece));
        }
    }
    if (merged.size() == pending->base.size())
    {
        delete pending;
        return; // nothing new to add
    }

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
    POINT pt = {0, 0};
    if (pContext && m_pComposition)
    {
        auto sess = new CGetRectSession(pContext, m_pComposition, &pt);
        HRESULT hrSession = S_OK;
        HRESULT hr = pContext->RequestEditSession(m_tfClientId, sess,
                                                  TF_ES_SYNC | TF_ES_READ, &hrSession);
        sess->Release();
        if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && (pt.x != 0 || pt.y != 0))
        {
            return pt;
        }
    }
    GUITHREADINFO gti = {sizeof(gti)};
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

// Phase B: rect of a substring of the composition. Used to anchor the
// candidate window under the currently focused bunsetsu instead of the
// composition's left edge. Falls back to the composition-left anchor if
// the substring rect query came back empty.
POINT CTextService::QueryBunsetsuAnchorPos(ITfContext* pContext, size_t offset, size_t length)
{
    POINT pt = {0, 0};
    if (pContext && m_pComposition && length > 0)
    {
        auto sess = new CGetBunsetsuRectSession(
            pContext, m_pComposition, static_cast<ULONG>(offset), static_cast<ULONG>(length), &pt);
        HRESULT hrSession = S_OK;
        HRESULT hr = pContext->RequestEditSession(m_tfClientId, sess,
                                                  TF_ES_SYNC | TF_ES_READ, &hrSession);
        sess->Release();
        if (SUCCEEDED(hr) && SUCCEEDED(hrSession) && (pt.x != 0 || pt.y != 0))
        {
            return pt;
        }
    }
    return QueryCandidateAnchorPos(pContext);
}

// Replace the composition range with whatever's currently selected in the
// candidate window. Called on initial show and whenever Up/Down moves the cursor.
void CTextService::SyncFocusedBunsetsuSelection()
{
    if (!m_pCandWnd || !InBunsetsuMode()) return;
    int sel = m_pCandWnd->GetSelectedIndex();
    if (sel < 0) return;
    auto& b = m_bunsetsuList[m_focusedBunsetsu];
    if (static_cast<size_t>(sel) < b.candidates.size())
        b.selected = static_cast<size_t>(sel);
}

void CTextService::ApplyCandidateSelection(ITfContext* pContext)
{
    if (!pContext || !m_pCandWnd) return;

    // Phase B: the window's selected index is for ONE bunsetsu only.
    // Mirror it into m_bunsetsuList and re-render the composition by
    // joining every bunsetsu's currently-selected candidate.
    if (InBunsetsuMode())
    {
        SyncFocusedBunsetsuSelection();
        std::wstring combined = bunsetsu::JoinSelected(m_bunsetsuList);
        if (!combined.empty())
        {
            RequestEditSession(pContext, EditAction::Update, combined);
            m_compositionConverted = TRUE;
        }
        return;
    }

    std::wstring picked = m_pCandWnd->GetSelected();
    if (picked.empty()) return;
    RequestEditSession(pContext, EditAction::Update, picked);
    m_compositionConverted = TRUE;
}

// 投機的変換: run a prefix search over the kana typed so far and pop the
// candidate window with completed words while the user is still typing
// (こんに → 今日は / 今日わ / …). Called after every buffer-mutating
// keystroke; the caller has already hidden the candidate window, so a
// no-match simply leaves it hidden.
void CTextService::UpdatePrediction(ITfContext* pContext)
{
    m_predictionActive = false;
    m_predictionReadings.clear();
    // The buffer changed, so whatever candidate list m_lastReading produced
    // is stale. Clearing it also makes the async reorder / fallback handlers
    // reject their now-outdated results via the reading-mismatch check. The
    // same goes for a leftover F-key form — Enter must not resurrect it
    // after the user resumed typing.
    m_lastReading.clear();
    m_fkeyConvertedText.clear();

    if (!m_pCandWnd || !pContext) return;
    if (m_imeMode != ImeMode::Hiragana) return;
    if (InBunsetsuMode()) return;

    auto r = romaji::Convert(m_romajiBuffer);
    const std::wstring& prefix = r.hira;
    // A single kana matches half the dictionary — noise, not speculation.
    if (prefix.size() < 2) return;

    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) return;

    // Document vocabulary outranks generic SKK completions: a word that
    // already appears around the caret is the most likely thing a
    // just-started reading is heading toward. Both lists are merged
    // (dedup by displayed word) under the same 9-item cap.
    std::vector<SkkDictionary::Prediction> preds;
    for (const auto& kv : m_docVocab)
    {
        if (kv.first.size() <= prefix.size()) continue;
        if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
        preds.push_back({kv.first, kv.second});
    }
    std::stable_sort(preds.begin(), preds.end(),
                     [](const SkkDictionary::Prediction& a,
                        const SkkDictionary::Prediction& b)
                     {
                         return a.reading.size() < b.reading.size();
                     });
    if (preds.size() > 9) preds.resize(9);

    for (auto& p : skk->PredictCompletions(prefix, 9))
    {
        if (preds.size() >= 9) break;
        bool dup = false;
        for (const auto& q : preds)
        {
            if (q.word == p.word)
            {
                dup = true;
                break;
            }
        }
        if (!dup) preds.push_back(std::move(p));
    }
    if (preds.empty()) return;

    // The user's own history beats SKK's first candidate: if they've
    // committed a word for a predicted reading before, show that form.
    AppContext ctx = AppContext::Capture();
    std::vector<std::wstring> words;
    words.reserve(preds.size());
    m_predictionReadings.reserve(preds.size());
    for (auto& p : preds)
    {
        std::wstring w = p.word;
        if (m_pLearning)
        {
            std::wstring fav = m_pLearning->GetFav(p.reading, ctx);
            if (!fav.empty()) w = std::move(fav);
        }
        words.push_back(std::move(w));
        m_predictionReadings.push_back(std::move(p.reading));
    }

    m_pCandWnd->SetCandidates(words);
    POINT pt = QueryCandidateAnchorPos(pContext);
    m_pCandWnd->ShowAt(pt);
    m_predictionActive = true;
}

// Move the prediction selection and mirror the pick into the composition.
// Until the user enters the list, the composition still shows the raw kana
// (m_compositionConverted == FALSE); the first ↓/Tab adopts the highlighted
// top entry rather than skipping past it. m_lastReading tracks the picked
// prediction's FULL reading so Enter's learning Record and a follow-up
// keystroke's CommitConvertedIfAny attribute the choice correctly.
void CTextService::NavigatePrediction(int delta, ITfContext* pContext)
{
    if (!m_pCandWnd) return;
    if (m_compositionConverted)
    {
        if (delta > 0) m_pCandWnd->SelectNext();
        else if (delta < 0) m_pCandWnd->SelectPrev();
    }
    int sel = m_pCandWnd->GetSelectedIndex();
    if (sel >= 0 && sel < static_cast<int>(m_predictionReadings.size()))
        m_lastReading = m_predictionReadings[sel];
    if (pContext) ApplyCandidateSelection(pContext);
}

namespace
{
    bool ContainsKanjiOrKatakana(const std::wstring& s)
    {
        for (wchar_t c : s)
        {
            if ((c >= 0x4E00 && c <= 0x9FFF) || c == L'々') return true;
            if (c >= 0x30A1 && c <= 0x30FA) return true;
        }
        return false;
    }

    // MecabMorpheme::pronunciation is hiragana for dictionary words, but the
    // unknown-word fallback copies the katakana surface verbatim. Kana-case
    // it down so document-vocab keys always compare against typed hiragana.
    std::wstring KanaToHira(const std::wstring& s)
    {
        std::wstring out(s);
        for (auto& c : out)
            if (c >= 0x30A1 && c <= 0x30F6) c = static_cast<wchar_t>(c - 0x60);
        return out;
    }
}

// ドキュメント文脈バイアス: read ~500 chars either side of the caret and
// harvest the kanji / katakana words (unigrams + adjacent-pair compounds)
// into m_docVocab, keyed by hiragana pronunciation. Called on composition
// start, throttled so rapid short compositions don't pay a MeCab pass each
// time. The map is rebuilt from scratch every scan — it is a snapshot of
// the CURRENT document, not an accumulating history (see textservice.h).
void CTextService::ScanDocumentVocab(ITfContext* pContext)
{
    if (!pContext) return;
    ULONGLONG now = GetTickCount64();
    if (m_docVocabTick != 0 && now - m_docVocabTick < 1500) return;
    m_docVocabTick = now;

    auto* mecab = MecabAnalyzer::GetGlobal();
    if (!mecab || !mecab->IsReady()) return;

    struct GetDocSlice : ITfEditSession
    {
        LONG m_cRef = 1;
        ITfContext* m_ctx;
        LONG m_max;
        std::wstring* m_outPrefix;
        std::wstring* m_outSuffix;

        GetDocSlice(ITfContext* c, LONG max, std::wstring* p, std::wstring* s)
            : m_ctx(c), m_max(max), m_outPrefix(p), m_outSuffix(s)
        {
            if (m_ctx) m_ctx->AddRef();
        }

        ~GetDocSlice() { if (m_ctx) m_ctx->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
        {
            if (!pp) return E_INVALIDARG;
            *pp = nullptr;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
            {
                *pp = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG c = InterlockedDecrement(&m_cRef);
            if (c == 0) delete this;
            return c;
        }

        STDMETHODIMP DoEditSession(TfEditCookie ec) override
        {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            HRESULT hr = m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
            if (FAILED(hr) || fetched == 0 || !sel.range) return hr;
            std::vector<wchar_t> buf(static_cast<size_t>(m_max) + 1, L'\0');
            ITfRange* preR = nullptr;
            sel.range->Clone(&preR);
            if (preR)
            {
                LONG shifted = 0;
                preR->ShiftStart(ec, -m_max, &shifted, nullptr);
                ULONG got = 0;
                preR->GetText(ec, 0, buf.data(), static_cast<ULONG>(m_max), &got);
                if (got > 0) m_outPrefix->assign(buf.data(), got);
                preR->Release();
            }
            ITfRange* sufR = nullptr;
            sel.range->Clone(&sufR);
            if (sufR)
            {
                LONG shifted = 0;
                sufR->ShiftEnd(ec, m_max, &shifted, nullptr);
                ULONG got = 0;
                sufR->GetText(ec, 0, buf.data(), static_cast<ULONG>(m_max), &got);
                if (got > 0) m_outSuffix->assign(buf.data(), got);
                sufR->Release();
            }
            sel.range->Release();
            return S_OK;
        }
    };

    std::wstring prefix, suffix;
    auto s = new GetDocSlice(pContext, 500, &prefix, &suffix);
    HRESULT hrS = S_OK;
    pContext->RequestEditSession(m_tfClientId, s, TF_ES_SYNC | TF_ES_READ, &hrS);
    s->Release();

    // Bounded caret-window slice for the LLM context. Prefix (what the
    // sentence said so far) matters more than suffix, so it gets the
    // bigger share of the budget.
    m_docContext.clear();
    {
        std::wstring pre = prefix.size() > 300 ? prefix.substr(prefix.size() - 300) : prefix;
        std::wstring suf = suffix.size() > 100 ? suffix.substr(0, 100) : suffix;
        if (!pre.empty() || !suf.empty())
            m_docContext = pre + L"〔入力位置〕" + suf;
    }

    m_docVocab.clear();
    std::wstring text = prefix + suffix;
    if (text.empty()) return;

    auto morphemes = mecab->Analyze(text);
    // Unigrams: any kanji/katakana-bearing morpheme whose written form
    // differs from its reading. Bigrams: adjacent pairs of such morphemes,
    // so compounds like 変換+窓 land as へんかんまど→変換窓 even though
    // no dictionary carries the combination.
    std::wstring prevReading, prevSurface;
    for (const auto& m : morphemes)
    {
        bool content = ContainsKanjiOrKatakana(m.surface) && !m.pronunciation.empty();
        if (content)
        {
            std::wstring reading = KanaToHira(m.pronunciation);
            if (reading != m.surface)
            {
                m_docVocab[reading] = m.surface;
                if (!prevReading.empty())
                    m_docVocab[prevReading + reading] = prevSurface + m.surface;
            }
            prevReading = std::move(reading);
            prevSurface = m.surface;
        }
        else
        {
            prevReading.clear();
            prevSurface.clear();
        }
        if (m_docVocab.size() >= 200) break;
    }

    wchar_t logbuf[120];
    swprintf_s(logbuf, L"[GenerativeIME] DocVocab: %zu entries from %zu chars\n",
               m_docVocab.size(), text.size());
    OutputDebugStringW(logbuf);
}

// 新規コンポジション開始直前、caret 直後の 1 文字が閉じ括弧なら doc から
// 削除して m_absorbedCloseBracket に stash する。以降のコンポジション表示は
// 自動的に「buffer + 」」の形になり、caret は「」の直前に置かれる。
// 変換確定時にはそのまま反映され、Escape キャンセル時には「」が元位置に戻る。
// 戻り値: true なら吸収した (m_absorbedCloseBracket に文字が入っている)。
bool CTextService::TryAbsorbCloseBracket(ITfContext* pContext)
{
    m_absorbedCloseBracket = 0;
    if (!pContext) return false;

    class AbsorbSession : public ITfEditSession
    {
    public:
        AbsorbSession(ITfContext* c, wchar_t* out)
            : m_ctx(c), m_out(out) { if (m_ctx) m_ctx->AddRef(); }

        STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
        {
            if (!pp) return E_INVALIDARG;
            *pp = nullptr;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
            {
                *pp = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG c = InterlockedDecrement(&m_cRef);
            if (c == 0) delete this;
            return c;
        }

        STDMETHODIMP DoEditSession(TfEditCookie ec) override
        {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            HRESULT hr = m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
            if (FAILED(hr) || fetched == 0 || !sel.range) return hr;
            ITfRange* peek = nullptr;
            sel.range->Clone(&peek);
            sel.range->Release();
            if (!peek) return S_OK;
            // Caret 位置から 1 char 分右に伸ばして GetText。
            LONG shifted = 0;
            peek->Collapse(ec, TF_ANCHOR_START);
            peek->ShiftEnd(ec, 1, &shifted, nullptr);
            if (shifted == 1)
            {
                wchar_t ch = 0;
                ULONG got = 0;
                peek->GetText(ec, 0, &ch, 1, &got);
                if (got == 1 && IsCloseBracketChar(ch))
                {
                    // 対象 char を doc から削除。以後、この位置に caret があるまま
                    // コンポジションが開始される。
                    peek->SetText(ec, 0, L"", 0);
                    *m_out = ch;
                }
            }
            peek->Release();
            return S_OK;
        }

    private:
        ~AbsorbSession() { if (m_ctx) m_ctx->Release(); }
        LONG m_cRef = 1;
        ITfContext* m_ctx;
        wchar_t* m_out;
    };

    auto s = new AbsorbSession(pContext, &m_absorbedCloseBracket);
    HRESULT hrS = S_OK;
    pContext->RequestEditSession(m_tfClientId, s, TF_ES_SYNC | TF_ES_READWRITE, &hrS);
    s->Release();
    return m_absorbedCloseBracket != 0;
}

// If the user picked a candidate (via Space/↓/Tab) and then starts typing
// the next chunk (alpha / symbol / etc.) without explicitly hitting Enter,
// auto-commit the converted text first so the new keystroke begins a fresh
// composition. Without this, the new keystroke would extend m_romajiBuffer
// and BuildCompositionDisplay would re-derive hiragana from it, throwing
// away the chosen kanji.
bool CTextService::CommitConvertedIfAny(ITfContext* pContext)
{
    // Defensive: clear stale bunsetsu state that leaked from an earlier
    // commit path (e.g. number-key candidate pick before the Phase-B fix,
    // or any future path that forgets LeaveBunsetsuMode). Without this,
    // the next fresh composition's ApplyCandidateSelection hits its
    // InBunsetsuMode branch and paints JoinSelected of the OLD clauses
    // over what the user just typed — the「使って良い」→「１」bug where
    // Space showed digit candidates in the popup but stale bunsetsu text
    // in the composition. Only trigger when there's no live conversion
    // to commit, so the normal commit-and-cleanup path below still fires.
    if (InBunsetsuMode() && (!m_compositionConverted || !m_pComposition))
    {
        LeaveBunsetsuMode();
    }
    if (!m_compositionConverted || !m_pComposition) return false;
    if (InBunsetsuMode())
    {
        // The user started typing the next chunk without hitting Enter on
        // the multi-bunsetsu composition. Commit the current join as a
        // single block, learning each bunsetsu's pick, then drop Phase B
        // state so the new keystroke starts a fresh composition.
        SyncFocusedBunsetsuSelection();
        std::wstring text = bunsetsu::JoinSelected(m_bunsetsuList);
        std::wstring joinedReading;
        std::vector<std::wstring> clauseReadings;
        if (m_pLearning)
        {
            AppContext ctx = AppContext::Capture();
            for (const auto& b : m_bunsetsuList)
            {
                if (b.reading.empty() || b.candidates.empty()) continue;
                if (b.selected >= b.candidates.size()) continue;
                m_pLearning->Record(b.reading, b.candidates[b.selected], ctx);
                joinedReading += b.reading;
                clauseReadings.push_back(b.reading);
            }
            // Also seed the WHOLE-phrase learning. Per-clause records above
            // let Phase B's Reorder promote the right pick on each clause
            // next time, but that still routes the user through the split-
            // and-navigate UI. Recording (joinedReading → joined text) too
            // means the fav fast path in TryOllamaConvertAsync grabs the
            // corrected sentence in a single Space, skipping Phase B
            // entirely for inputs the user has already resolved. Guarded on
            // ≥2 clauses so we don't clobber single-clause records with
            // duplicates of the same (reading, text).
            if (!joinedReading.empty() && !text.empty() && m_bunsetsuList.size() >= 2)
            {
                m_pLearning->Record(joinedReading, text, ctx);
            }
        }
        AppendCommittedText(text);
        if (!joinedReading.empty()) m_lastCommittedReading = joinedReading;
        m_lastCommittedClauseReadings = std::move(clauseReadings);
        if (pContext) RequestEditSession(pContext, EditAction::EndCommit, text);
        LeaveBunsetsuMode();
    }
    else if (m_pCandWnd)
    {
        // Prefer the F-key-converted form: F6-F10 hide the candidate window
        // and stash their result in m_fkeyConvertedText, so reading the
        // (now stale/empty) candidate-window selection here would commit the
        // wrong text AND record the wrong learning pair. Fall back to the
        // candidate-window selection for ordinary Space/↓ picks. Mirrors the
        // VK_RETURN commit path; without it, an F-key conversion auto-
        // committed by typing the next chunk instead of Enter loses both the
        // form and its learning.
        std::wstring picked = !m_fkeyConvertedText.empty()
                                  ? m_fkeyConvertedText
                                  : m_pCandWnd->GetSelected();
        if (m_pLearning && !m_lastReading.empty() && !picked.empty())
        {
            m_pLearning->Record(m_lastReading, picked, AppContext::Capture());
        }
        AppendCommittedText(picked);
        if (!m_lastReading.empty()) m_lastCommittedReading = m_lastReading;
        size_t caretShift = BracketPairCaretBackShift(picked);
        // Explicitly SetText(picked) on commit rather than trusting the
        // composition range to still hold what the previous Update wrote.
        // notepad respects range content across Update+EndCommit, but
        // Chromium contenteditable (x.com, etc.) may truncate or replace
        // the range mid-sequence, leaving only the opening char of a
        // 2-char pair like 『』. Passing the text explicitly makes the
        // final write authoritative regardless of host quirks.
        if (pContext) RequestEditSession(pContext, EditAction::EndCommit, picked, caretShift);
    }
    m_romajiBuffer.clear();
    m_compositionConverted = FALSE;
    m_lastReading.clear();
    m_fkeyConvertedText.clear();
    m_nonconvertCycle = 0;
    m_predictionActive = false;
    m_predictionReadings.clear();
    if (m_pCandWnd) m_pCandWnd->Hide();
    return true;
}

// Append `text` to the rolling context buffer and clamp to the recent window.
// We keep the buffer bounded so a hours-long session doesn't bloat memory and
// so the LLM context we eventually send stays short (latency-sensitive path).
void CTextService::EnterBunsetsuMode(std::vector<Bunsetsu> parts,
                                     ITfContext* pContext)
{
    if (parts.empty()) return;
    // Apply per-bunsetsu learning so the previously-picked kanji form
    // for each reading lands at index 0. Without this, VK_RETURN's
    // m_pLearning->Record on commit was a one-way street — the data
    // was being written but never read back at conversion time.
    if (m_pLearning)
    {
        for (auto& b : parts)
        {
            if (b.reading.empty() || b.candidates.empty()) continue;
            b.candidates = m_pLearning->Reorder(b.reading, b.candidates);
            b.selected = 0;
        }
    }
    m_bunsetsuList = std::move(parts);
    m_focusedBunsetsu = 0;
    RepaintBunsetsu(pContext);
}

void CTextService::LeaveBunsetsuMode()
{
    m_bunsetsuList.clear();
    m_focusedBunsetsu = 0;
}

void CTextService::RepaintBunsetsu(ITfContext* pContext)
{
    if (!InBunsetsuMode() || !m_pCandWnd) return;
    auto& cur = m_bunsetsuList[m_focusedBunsetsu];

    // Candidate window shows the focused bunsetsu's options. m_lastReading
    // tracks the focused bunsetsu's reading so per-bunsetsu learning on
    // Enter records each piece independently.
    m_pCandWnd->SetCandidates(cur.candidates);
    if (cur.selected < cur.candidates.size())
        m_pCandWnd->SelectIndex(static_cast<int>(cur.selected));
    m_lastReading = cur.reading;

    if (pContext)
    {
        // Update composition FIRST so the bunsetsu offsets we hand to
        // GetTextExt match the post-update text. RequestEditSession also
        // sees we're in Phase B and splits the display attribute so the
        // focused clause renders with the highlight style.
        std::wstring combined = bunsetsu::JoinSelected(m_bunsetsuList);
        RequestEditSession(pContext, EditAction::Update, combined);

        // Anchor the candidate window at the focused bunsetsu's starting
        // column instead of the composition's left edge. Falls back to
        // the legacy anchor if the rect query failed.
        size_t offset = 0;
        for (size_t i = 0; i < m_focusedBunsetsu; ++i)
            offset += m_bunsetsuList[i].Selected().size();
        POINT pt = QueryBunsetsuAnchorPos(pContext, offset, cur.Selected().size());
        m_pCandWnd->ShowAt(pt);
    }
    m_compositionConverted = TRUE;
}

void CTextService::CycleNonconvertForm(ITfContext* pContext)
{
    if (!m_pComposition || !pContext) return;

    // Resolve the current romaji buffer to a canonical hiragana form
    // (same FinalizeTrailingN treatment the F-key path uses, so a lone
    // trailing "n" becomes ん instead of disappearing on the cycle).
    auto r = romaji::Convert(m_romajiBuffer);
    std::wstring hira = r.hira + romaji::FinalizeTrailingN(r.remaining);
    if (hira.empty() && m_romajiBuffer.empty()) return;

    // Cycle: 0 = hiragana, 1 = 全角カナ, 2 = 半角カナ, 3 = ローマ字.
    m_nonconvertCycle = (m_nonconvertCycle + 1) % 4;
    std::wstring text;
    switch (m_nonconvertCycle)
    {
    case 0: text = hira;
        break;
    case 1: text = ToFullKatakana(hira);
        break;
    case 2: text = ToHalfKatakana(ToFullKatakana(hira));
        break;
    case 3: text = m_romajiBuffer;
        break;
    }

    if (m_pCandWnd) m_pCandWnd->Hide();
    if (!text.empty()) RequestEditSession(pContext, EditAction::Update, text);
    m_compositionConverted = TRUE;
    m_fkeyConvertedText = text;
    m_lastReading = hira;
}

void CTextService::TryReconvertFromSelection(ITfContext* pContext)
{
    if (!pContext) return;

    auto* mecab = MecabAnalyzer::GetGlobal();

    // Pull the host's current selection text via a read-only edit session.
    // Without a selection (or with a zero-width caret) there's nothing to
    // re-convert, so we bail out quietly.
    struct GetSelText : ITfEditSession
    {
        LONG m_cRef = 1;
        ITfContext* m_ctx;
        std::wstring* m_out;
        GetSelText(ITfContext* c, std::wstring* o) : m_ctx(c), m_out(o) { if (m_ctx) m_ctx->AddRef(); }
        ~GetSelText() { if (m_ctx) m_ctx->Release(); }
        STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
        {
            if (!pp) return E_INVALIDARG;
            *pp = nullptr;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
            {
                *pp = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG c = InterlockedDecrement(&m_cRef);
            if (c == 0) delete this;
            return c;
        }

        STDMETHODIMP DoEditSession(TfEditCookie ec) override
        {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            HRESULT hr = m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
            if (FAILED(hr) || fetched == 0 || !sel.range) return hr;
            // Probe up to 256 chars — anything longer is almost certainly
            // not a re-convert target (would blow past Ollama timeout too).
            wchar_t buf[256] = {};
            ULONG got = 0;
            sel.range->GetText(ec, 0, buf, 255, &got);
            if (got > 0) m_out->assign(buf, got);
            sel.range->Release();
            return S_OK;
        }
    };

    std::wstring selected;
    auto sess = new GetSelText(pContext, &selected);
    HRESULT hrSession = S_OK;
    pContext->RequestEditSession(m_tfClientId, sess, TF_ES_SYNC | TF_ES_READ, &hrSession);
    sess->Release();

    // Fallback when the host has no explicit selection (just a caret):
    // MS-IME's re-convert picks up the morpheme the caret is inside (or
    // immediately after). We grab text on both sides of the caret, run
    // MeCab on the combined slice, find the morpheme that contains the
    // caret offset, and extend the host selection across that morpheme.
    LONG targetBack = 0;
    LONG targetForward = 0;
    if (selected.empty())
    {
        struct GetCaretSlice : ITfEditSession
        {
            LONG m_cRef = 1;
            ITfContext* m_ctx;
            LONG m_max;
            std::wstring* m_outPrefix;
            std::wstring* m_outSuffix;

            GetCaretSlice(ITfContext* c, LONG max, std::wstring* p, std::wstring* s)
                : m_ctx(c), m_max(max), m_outPrefix(p), m_outSuffix(s)
            {
                if (m_ctx) m_ctx->AddRef();
            }

            ~GetCaretSlice() { if (m_ctx) m_ctx->Release(); }
            STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
            {
                if (!pp) return E_INVALIDARG;
                *pp = nullptr;
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
                {
                    *pp = static_cast<ITfEditSession*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }

            STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
            STDMETHODIMP_(ULONG) Release() override
            {
                LONG c = InterlockedDecrement(&m_cRef);
                if (c == 0) delete this;
                return c;
            }

            STDMETHODIMP DoEditSession(TfEditCookie ec) override
            {
                TF_SELECTION sel = {};
                ULONG fetched = 0;
                HRESULT hr = m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
                if (FAILED(hr) || fetched == 0 || !sel.range) return hr;
                // Prefix: clone selection range, walk start back m_max.
                ITfRange* preR = nullptr;
                sel.range->Clone(&preR);
                if (preR)
                {
                    LONG shifted = 0;
                    preR->ShiftStart(ec, -m_max, &shifted, nullptr);
                    wchar_t buf[256] = {};
                    ULONG got = 0;
                    preR->GetText(ec, 0, buf, 255, &got);
                    if (got > 0) m_outPrefix->assign(buf, got);
                    preR->Release();
                }
                // Suffix: clone, walk end forward m_max.
                ITfRange* sufR = nullptr;
                sel.range->Clone(&sufR);
                if (sufR)
                {
                    LONG shifted = 0;
                    sufR->ShiftEnd(ec, m_max, &shifted, nullptr);
                    wchar_t buf[256] = {};
                    ULONG got = 0;
                    sufR->GetText(ec, 0, buf, 255, &got);
                    if (got > 0) m_outSuffix->assign(buf, got);
                    sufR->Release();
                }
                sel.range->Release();
                return S_OK;
            }
        };
        std::wstring prefix, suffix;
        auto gp = new GetCaretSlice(pContext, 20, &prefix, &suffix);
        HRESULT hrGp = S_OK;
        pContext->RequestEditSession(m_tfClientId, gp,
                                     TF_ES_SYNC | TF_ES_READ, &hrGp);
        gp->Release();

        std::wstring combined = prefix + suffix;
        if (!combined.empty() && mecab && mecab->IsReady())
        {
            auto morphemes = mecab->Analyze(combined);
            size_t caretOffset = prefix.size();
            size_t cum = 0;
            for (const auto& m : morphemes)
            {
                size_t end = cum + m.surface.size();
                // Morpheme spans [cum, end). The caret is between chars
                // at position caretOffset (0-based). Adopt this morpheme
                // when the caret falls strictly inside, at its end edge,
                // or — for the first morpheme only — exactly at offset 0.
                bool inside = (caretOffset > cum && caretOffset <= end);
                bool atStart = (cum == 0 && caretOffset == 0);
                if (inside || atStart)
                {
                    selected = m.surface;
                    targetBack = static_cast<LONG>(caretOffset - cum);
                    targetForward = static_cast<LONG>(end - caretOffset);
                    break;
                }
                cum = end;
            }
        }
    }

    if (selected.empty())
    {
        OutputDebugStringW(L"[GenerativeIME] Reconvert: no selection and caret morpheme lookup failed\n");
        return;
    }

    // Caret-fallback case: extend the host selection so it covers the
    // identified morpheme on both sides of the caret. The composition
    // then overwrites that range instead of inserting after the caret.
    if (targetBack > 0 || targetForward > 0)
    {
        struct ExtendRange : ITfEditSession
        {
            LONG m_cRef = 1;
            ITfContext* m_ctx;
            LONG m_back;
            LONG m_forward;

            ExtendRange(ITfContext* c, LONG b, LONG f)
                : m_ctx(c), m_back(b), m_forward(f) { if (m_ctx) m_ctx->AddRef(); }

            ~ExtendRange() { if (m_ctx) m_ctx->Release(); }
            STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
            {
                if (!pp) return E_INVALIDARG;
                *pp = nullptr;
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
                {
                    *pp = static_cast<ITfEditSession*>(this);
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }

            STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
            STDMETHODIMP_(ULONG) Release() override
            {
                LONG c = InterlockedDecrement(&m_cRef);
                if (c == 0) delete this;
                return c;
            }

            STDMETHODIMP DoEditSession(TfEditCookie ec) override
            {
                TF_SELECTION sel = {};
                ULONG fetched = 0;
                HRESULT hr = m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
                if (FAILED(hr) || fetched == 0 || !sel.range) return hr;
                LONG shifted = 0;
                if (m_back > 0)
                    sel.range->ShiftStart(ec, -m_back, &shifted, nullptr);
                if (m_forward > 0)
                    sel.range->ShiftEnd(ec, m_forward, &shifted, nullptr);
                m_ctx->SetSelection(ec, 1, &sel);
                sel.range->Release();
                return S_OK;
            }
        };
        auto ext = new ExtendRange(pContext, targetBack, targetForward);
        HRESULT hrExt = S_OK;
        pContext->RequestEditSession(m_tfClientId, ext,
                                     TF_ES_SYNC | TF_ES_READWRITE, &hrExt);
        ext->Release();
    }

    // Recover the reading. Pure-kana selections are already the reading
    // we'd want; mixed kanji selections go through MeCab's pronunciation
    // field. Falls back to the literal selection if MeCab can't parse it.
    std::wstring reading;
    if (mecab && mecab->IsReady())
    {
        auto morphemes = mecab->Analyze(selected);
        for (const auto& m : morphemes)
        {
            if (!m.pronunciation.empty()) reading += m.pronunciation;
            else reading += m.surface;
        }
    }
    if (reading.empty()) reading = selected;

    wchar_t logbuf[300];
    swprintf_s(logbuf,
               L"[GenerativeIME] Reconvert: selection=\"%s\" reading=\"%s\"\n",
               selected.c_str(), reading.c_str());
    OutputDebugStringW(logbuf);

    // Replace the host selection with the recovered reading as a fresh
    // composition. From here the normal Space / candidate flow takes
    // over; commit will overwrite the selected range with the user's
    // pick.
    m_romajiBuffer.clear();
    m_compositionConverted = FALSE;
    m_fkeyConvertedText.clear();
    m_nonconvertCycle = 0;
    // m_lastReading is the SOURCE that TryOllamaConvertAsync reads from
    // on the reconvert path (m_romajiBuffer is empty). Set it before
    // starting the composition.
    m_lastReading = reading;
    LeaveBunsetsuMode();
    if (m_pCandWnd) m_pCandWnd->Hide();

    RequestEditSession(pContext, EditAction::StartAndUpdate, reading);
    TryOllamaConvertAsync(pContext);
}

void CTextService::ResizeFocusedBunsetsu(int delta, ITfContext* pContext)
{
    if (!InBunsetsuMode() || delta == 0) return;
    if (m_focusedBunsetsu >= m_bunsetsuList.size()) return;

    auto* mecab = MecabAnalyzer::GetGlobal();
    auto* skk = SkkDictionary::GetGlobal();

    // NB: reference-into-vector semantics. We used to hold `auto& cur =
    // m_bunsetsuList[m_focusedBunsetsu]` across mutations of the same
    // vector — every push_back/erase can reallocate the backing storage
    // and turn `cur` into a dangling reference. Writing to it then leaves
    // the actual m_bunsetsuList[m_focusedBunsetsu] unchanged and the join
    // ended up with the pre-shrink reading followed by the popped-off
    // character (「したじっそう」→「したじっそうう」on Shift+← was the
    // concrete case). Snapshot the current reading as a plain string,
    // mutate the vector freely, and index into it afresh at the very
    // end to write the new focused bunsetsu.
    const std::wstring curReading = m_bunsetsuList[m_focusedBunsetsu].reading;

    if (delta > 0)
    {
        // Grow: pull the first character of the next bunsetsu onto the
        // end of the focused one. No-op when there's no next bunsetsu
        // to draw from.
        if (m_focusedBunsetsu + 1 >= m_bunsetsuList.size()) return;
        const std::wstring nxtReading = m_bunsetsuList[m_focusedBunsetsu + 1].reading;
        if (nxtReading.empty()) return;

        std::wstring newCur = curReading + nxtReading.substr(0, 1);
        std::wstring newNxt = nxtReading.substr(1);

        Bunsetsu newFocused = bunsetsu::MakeBunsetsuFromReading(newCur, mecab, skk);
        if (m_pLearning && !newFocused.candidates.empty())
        {
            newFocused.candidates = m_pLearning->Reorder(newFocused.reading, newFocused.candidates);
            newFocused.selected = 0;
        }
        if (newNxt.empty())
        {
            // The next bunsetsu's reading was fully absorbed. Erase FIRST
            // so the focused-slot write at the end doesn't need to shift.
            m_bunsetsuList.erase(m_bunsetsuList.begin() + m_focusedBunsetsu + 1);
        }
        else
        {
            auto rebuilt = bunsetsu::MakeBunsetsuFromReading(newNxt, mecab, skk);
            if (m_pLearning && !rebuilt.candidates.empty())
            {
                rebuilt.candidates = m_pLearning->Reorder(rebuilt.reading, rebuilt.candidates);
                rebuilt.selected = 0;
            }
            m_bunsetsuList[m_focusedBunsetsu + 1] = std::move(rebuilt);
        }
        m_bunsetsuList[m_focusedBunsetsu] = std::move(newFocused);
    }
    else
    {
        // Shrink: peel the last character off the focused bunsetsu's
        // reading and prepend it to the next bunsetsu (creating one if
        // there isn't a next). No-op if focused is already one char —
        // we can't shrink to zero.
        if (curReading.size() <= 1) return;
        wchar_t moved = curReading.back();
        std::wstring newCur = curReading.substr(0, curReading.size() - 1);

        if (m_focusedBunsetsu + 1 < m_bunsetsuList.size())
        {
            std::wstring nxtReading = m_bunsetsuList[m_focusedBunsetsu + 1].reading;
            std::wstring newNxt;
            newNxt.push_back(moved);
            newNxt += nxtReading;
            m_bunsetsuList[m_focusedBunsetsu + 1] =
                bunsetsu::MakeBunsetsuFromReading(newNxt, mecab, skk);
        }
        else
        {
            // No tail bunsetsu — create one for the orphaned character so
            // the user can still navigate to it with → / Tab. push_back
            // can reallocate the vector, which is exactly why we snapshot
            // curReading up top and index the focused-slot write at the
            // very end instead of holding a reference across this call.
            Bunsetsu tail;
            tail.reading = std::wstring(1, moved);
            tail = bunsetsu::MakeBunsetsuFromReading(tail.reading, mecab, skk);
            m_bunsetsuList.push_back(std::move(tail));
        }
        m_bunsetsuList[m_focusedBunsetsu] = bunsetsu::MakeBunsetsuFromReading(newCur, mecab, skk);
    }

    wchar_t logbuf[200];
    swprintf_s(logbuf,
               L"[GenerativeIME] Phase B resize: delta=%+d focused=%zu reading=%s parts=%zu\n",
               delta, m_focusedBunsetsu,
               m_bunsetsuList[m_focusedBunsetsu].reading.c_str(),
               m_bunsetsuList.size());
    OutputDebugStringW(logbuf);

    RepaintBunsetsu(pContext);
}

void CTextService::AppendCommittedText(const std::wstring& text)
{
    if (text.empty()) return;
    // Every commit path funnels through here, which makes it the one spot
    // that reliably sees "the previous conversion result" for F4 repeat.
    m_lastCommittedText = text;
    m_recentContext.append(text);
    if (m_recentContext.size() > kRecentContextMax)
    {
        m_recentContext.erase(0, m_recentContext.size() - kRecentContextMax);
    }
}

// Writes a short debug line to a log file so we can diagnose things
// like preserved-key routing without needing DebugView. UTF-8, CRLF.
// Two-path strategy: prefer %APPDATA%\GenerativeIME\ime-debug.log; on
// any failure (COM not initialized in the loading process, ACL denies
// the write, whatever) fall back to %TEMP%\GenerativeIME-debug.log.
// TIP DLLs load into arbitrary host processes and %APPDATA% resolution
// via SHGetKnownFolderPath needs COM in a state we can't count on;
// %TEMP% only needs the plain env-var and is writable from every user
// process. Silently no-ops if BOTH targets are unreachable — a log
// helper must never break IME input.
static void AppendDebugLine(const wchar_t* line)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamped[512];
    swprintf_s(stamped, L"%04d-%02d-%02dT%02d:%02d:%02d [pid=%lu] %s\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               GetCurrentProcessId(), line);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, stamped, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1) return;
    std::string utf8(static_cast<size_t>(utf8Len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, stamped, -1, utf8.data(), utf8Len - 1, nullptr, nullptr);

    auto tryWrite = [&](const std::wstring& path) -> bool
    {
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0;
        BOOL ok = WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &w, nullptr);
        CloseHandle(h);
        return ok != FALSE;
    };

    // Primary path via env var (avoids SHGetKnownFolderPath / COM dep).
    wchar_t appdata[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, _countof(appdata));
    if (n > 0 && n < _countof(appdata))
    {
        std::wstring dir = appdata;
        dir += L"\\GenerativeIME";
        CreateDirectoryW(dir.c_str(), nullptr);
        if (tryWrite(dir + L"\\ime-debug.log")) return;
    }

    // Fallback path.
    wchar_t tmp[MAX_PATH * 2];
    n = GetEnvironmentVariableW(L"TEMP", tmp, _countof(tmp));
    if (n > 0 && n < _countof(tmp))
    {
        std::wstring path = tmp;
        path += L"\\GenerativeIME-debug.log";
        tryWrite(path);
    }
}

void CTextService::LogMisconversionAttempt()
{
    // Resolve %APPDATA%\GenerativeIME\misconversions.log using the env
    // var (SHGetKnownFolderPath needs COM initialized in the host
    // process — some TIP hosts don't guarantee that at load time, and
    // AppendDebugLine hit exactly that failure earlier). Reuses the
    // parent directory of SkkDictionary::UserDictDir so users already
    // looking there for their dict files find the log alongside.
    wchar_t appdata[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, _countof(appdata));
    if (n == 0 || n >= _countof(appdata)) return;
    std::wstring dir = appdata;
    dir += L"\\GenerativeIME";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\misconversions.log";

    // Snapshot the state we care about. Reading DisplayForMode's output
    // gives us what the user was actually SEEING in the composition,
    // which is often more informative than the raw ASCII buffer alone
    // (a lone "j" in Hiragana mode still shows as "j"; a "watashi" shows
    // as「わたし」).
    std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);

    std::wstring cands;
    std::wstring selected;
    if (m_pCandWnd)
    {
        selected = m_pCandWnd->GetSelected();
        const auto& list = m_pCandWnd->GetCandidates();
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (i > 0) cands += L" | ";
            cands += list[i];
        }
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    swprintf_s(timestamp, L"%04d-%02d-%02dT%02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // Build the entry. One block per Ctrl+F5 tap, separated by blank
    // line, ASCII field names so `grep buffer:` etc. works on the file.
    // CRLF line endings so it opens cleanly in Notepad without turning
    // into one long line.
    auto boolStr = [](bool b) { return b ? L"true" : L"false"; };
    std::wstring entry;
    entry += L"--- ";
    entry += timestamp;
    entry += L" ---\r\n";
    entry += L"buffer: ";
    entry += m_romajiBuffer;
    entry += L"\r\n";
    entry += L"display: ";
    entry += display;
    entry += L"\r\n";
    entry += L"lastReading: ";
    entry += m_lastReading;
    entry += L"\r\n";
    entry += L"selected: ";
    entry += selected;
    entry += L"\r\n";
    entry += L"candidates: ";
    entry += cands;
    entry += L"\r\n";
    entry += L"lastCommitted: ";
    entry += m_lastCommittedText;
    entry += L"\r\n";
    entry += L"context: ";
    entry += m_recentContext;
    entry += L"\r\n";
    entry += L"imeMode: ";
    entry += std::to_wstring(static_cast<int>(m_imeMode));
    entry += L"\r\n";
    entry += L"converted: ";
    entry += boolStr(m_compositionConverted != 0);
    entry += L"\r\n";
    entry += L"predictionActive: ";
    entry += boolStr(m_predictionActive);
    entry += L"\r\n";
    entry += L"bunsetsuMode: ";
    entry += boolStr(InBunsetsuMode());
    entry += L"\r\n";
    entry += L"forgetReading: ";
    entry += m_lastCommittedReading;
    entry += L"\r\n";
    entry += L"\r\n";

    // Convert to UTF-8 for cross-tool friendliness (grep / rg / VS Code
    // default to UTF-8; wchar_t writes would need a BOM and produce a
    // file most Unix tools mishandle).
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, entry.c_str(), static_cast<int>(entry.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return;
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, entry.c_str(), static_cast<int>(entry.size()),
                        utf8.data(), utf8Len, nullptr, nullptr);

    HANDLE h = CreateFileW(path.c_str(),
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    // Write a UTF-8 BOM only if the file was just created (size==0).
    // OPEN_ALWAYS returns the existing file if it was already there, so
    // this preserves the BOM state for repeated appends.
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    if (size.QuadPart == 0)
    {
        constexpr unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        WriteFile(h, bom, 3, &written, nullptr);
    }

    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);

    // Forget the bad pick — the commit that just went to disk (and its
    // scoped variants) gets stripped so it stops overriding the dict
    // head next time this reading comes up. Without this, dictionary
    // fixes to promote the correct kanji have no effect until the user
    // manually greps learning.txt because GetFav's cascade returns the
    // wrong learned candidate before Reorder ever sees the fresh SKK
    // ordering.
    //
    // For a bunsetsu commit we ALSO forget each clause reading
    // individually. The join covers the whole-phrase fav entry, but
    // Record wrote a separate per-clause row for each 文節 in the same
    // pass, and if we don't remove those the wrong per-clause pick
    // still resurfaces on future compositions that split the same way.
    // Concrete case: ついかしたじっそう → 付いか下実装 committed both
    //「ついかした/付いかした」 AND 「じっそう/下実装」 as per-clause
    // learnings; forgetting only the whole reading left the two rows
    // behind.
    //
    // Clears both trackers so a second Ctrl+Shift+F5 tap in a row
    // doesn't attempt to re-forget readings that are already gone.
    if (m_pLearning)
    {
        if (!m_lastCommittedReading.empty())
            m_pLearning->ForgetReading(m_lastCommittedReading);
        for (const auto& r : m_lastCommittedClauseReadings)
            if (!r.empty()) m_pLearning->ForgetReading(r);
        m_lastCommittedReading.clear();
        m_lastCommittedClauseReadings.clear();
    }

    wchar_t debugbuf[300];
    swprintf_s(debugbuf, L"[GenerativeIME] Ctrl+F5: logged misconversion to %s (%zu bytes)\n",
               path.c_str(), utf8.size());
    OutputDebugStringW(debugbuf);
}

bool CTextService::ToggleCodepointInPlace(ITfContext* pContext)
{
    if (!pContext) return false;

    // Decide direction based on the LAST commit we saw. Two shapes flip:
    //   Hex → Char: 4-6 hex chars representing a valid scalar → the char
    //   Char → Hex: 1-2 wchars (single BMP char or a surrogate pair) → hex
    // Any other last-commit shape (empty, longer than 6, non-hex etc.)
    // means we can't do a clean toggle — return false so the caller can
    // fall back or just no-op.
    const std::wstring& last = m_lastCommittedText;
    if (last.empty()) return false;

    std::wstring rep;
    LONG replaceLen = 0;

    auto hexVal = [](wchar_t c) -> int
    {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
        if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
        return -1;
    };

    // Hex → Char: check ONLY when the entire last-commit is 4-6 hex chars,
    // to avoid grabbing arbitrary trailing hex-looking characters the user
    // may have typed for other reasons (e.g. "cafe" ends in valid hex but
    // is a word). The 4-6 length gate is the same one the forward F5 branch
    // uses to parse a hex codepoint.
    bool hexToChar = false;
    if (last.size() >= 4 && last.size() <= 6)
    {
        unsigned cp = 0;
        bool ok = true;
        for (wchar_t c : last)
        {
            int v = hexVal(c);
            if (v < 0)
            {
                ok = false;
                break;
            }
            cp = (cp << 4) | static_cast<unsigned>(v);
        }
        if (ok && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF))
        {
            if (cp <= 0xFFFF)
            {
                rep.push_back(static_cast<wchar_t>(cp));
            }
            else
            {
                unsigned s = cp - 0x10000;
                rep.push_back(static_cast<wchar_t>(0xD800 | (s >> 10)));
                rep.push_back(static_cast<wchar_t>(0xDC00 | (s & 0x3FF)));
            }
            replaceLen = static_cast<LONG>(last.size());
            hexToChar = true;
        }
    }

    // Char → Hex: fall back when it wasn't clean hex. Take the last one
    // or two wchars off the commit — surrogate pairs go together — and
    // emit the U+xxxx form as hex text. Digits render 4 wide for BMP,
    // 5-6 wide for supplementary planes; the width matches what the
    // forward F5 branch expects when the user hits F5 again.
    if (!hexToChar)
    {
        unsigned cp = 0;
        LONG take = 1;
        if (last.size() >= 2)
        {
            wchar_t hi = last[last.size() - 2];
            wchar_t lo = last[last.size() - 1];
            if (hi >= 0xD800 && hi <= 0xDBFF && lo >= 0xDC00 && lo <= 0xDFFF)
            {
                cp = 0x10000 + (static_cast<unsigned>(hi - 0xD800) << 10)
                    + static_cast<unsigned>(lo - 0xDC00);
                take = 2;
            }
            else
            {
                cp = static_cast<unsigned>(lo);
            }
        }
        else
        {
            cp = static_cast<unsigned>(last.back());
        }
        wchar_t hex[8];
        swprintf_s(hex, cp <= 0xFFFF ? L"%04X" : L"%05X", cp);
        rep = hex;
        replaceLen = take;
    }

    // Sync edit session: extend the current selection backwards `replaceLen`
    // chars (the tail of the commit we want to replace) and SetText it with
    // `rep`. We snapshotted the last-commit shape from m_lastCommittedText
    // above; the session runs on the same UI thread so no host input can
    // have moved the tail between now and DoEditSession.
    struct ToggleSession : ITfEditSession
    {
        LONG m_cRef = 1;
        ITfContext* m_ctx;
        LONG m_len;
        const wchar_t* m_repText;
        LONG m_repLen;
        bool m_ok = false;

        ToggleSession(ITfContext* c, LONG len, const wchar_t* rep, LONG repLen)
            : m_ctx(c), m_len(len), m_repText(rep), m_repLen(repLen)
        {
            if (m_ctx) m_ctx->AddRef();
        }

        ~ToggleSession() { if (m_ctx) m_ctx->Release(); }

        STDMETHODIMP QueryInterface(REFIID riid, void** pp) override
        {
            if (!pp) return E_INVALIDARG;
            *pp = nullptr;
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
            {
                *pp = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
        STDMETHODIMP_(ULONG) Release() override
        {
            LONG c = InterlockedDecrement(&m_cRef);
            if (c == 0) delete this;
            return c;
        }

        STDMETHODIMP DoEditSession(TfEditCookie ec) override
        {
            TF_SELECTION sel = {};
            ULONG fetched = 0;
            if (FAILED(m_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched))
                || fetched == 0 || !sel.range)
                return S_OK;

            ITfRange* pRange = nullptr;
            sel.range->Clone(&pRange);
            sel.range->Release();
            if (!pRange) return S_OK;

            LONG shifted = 0;
            pRange->ShiftStart(ec, -m_len, &shifted, nullptr);
            if (shifted != -m_len)
            {
                // Fewer chars before the caret than we thought — the host
                // moved the caret or edited between our commit snapshot and
                // now. Bail without touching the document.
                pRange->Release();
                return S_OK;
            }

            HRESULT hr = pRange->SetText(ec, 0, m_repText, m_repLen);
            if (SUCCEEDED(hr))
            {
                pRange->Collapse(ec, TF_ANCHOR_END);
                TF_SELECTION newSel = {};
                newSel.range = pRange;
                newSel.style.ase = TF_AE_END;
                newSel.style.fInterimChar = FALSE;
                m_ctx->SetSelection(ec, 1, &newSel);
                m_ok = true;
            }
            pRange->Release();
            return S_OK;
        }
    };

    auto* s = new ToggleSession(pContext, replaceLen, rep.c_str(), static_cast<LONG>(rep.length()));
    HRESULT hrSess = S_OK;
    HRESULT hr = pContext->RequestEditSession(m_tfClientId, s,
                                              TF_ES_SYNC | TF_ES_READWRITE, &hrSess);
    bool ok = s->m_ok;
    s->Release();
    if (FAILED(hr) || FAILED(hrSess) || !ok) return false;

    // Refresh our commit snapshot so a subsequent F5 tap toggles again
    // from the newly-inserted form, not the old one. Also patch
    // m_recentContext (LLM prompt context) so we don't send an
    // inconsistent tail.
    m_lastCommittedText = rep;
    if (m_recentContext.size() >= static_cast<size_t>(replaceLen))
        m_recentContext.erase(m_recentContext.size() - static_cast<size_t>(replaceLen));
    m_recentContext.append(rep);
    if (m_recentContext.size() > kRecentContextMax)
        m_recentContext.erase(0, m_recentContext.size() - kRecentContextMax);
    return true;
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
        if (msg == WM_ACRONYM_DONE)
        {
            self->HandleAcronymDone(reinterpret_cast<PendingAcronymRequest*>(lParam));
            return 0;
        }
        if (msg == WM_LANGBAR_MENU)
        {
            int x = static_cast<short>(LOWORD(lParam));
            int y = static_cast<short>(HIWORD(lParam));
            self->ShowLangBarMenu(x, y);
            return 0;
        }
        if (msg == WM_SET_IME_MODE)
        {
            // wParam encodes ImeMode (0=Off, 1=Hiragana, 2=FullKatakana,
            // 3=HalfKatakana, 4=FullAlnum). Clamp to known values.
            int v = static_cast<int>(wParam);
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
void CTextService::ShowLangBarMenu(int /*x*/, int /*y*/)
{
}

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
                   this, mode, static_cast<int>(m_isImeOn), m_imeMode);
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
                   static_cast<int>(m_isImeOn), m_imeMode);
        OutputDebugStringW(buf);
    }
}

HRESULT CTextService::InitMessageWindow()
{
    if (m_hwndMsg) return S_OK;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = g_hInst;
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
    if (IsEqualGUID(guidInfo, c_guidDisplayAttributeInput))
    {
        *ppInfo = new CDisplayAttributeInfoInput();
        return *ppInfo ? S_OK : E_OUTOFMEMORY;
    }
    if (IsEqualGUID(guidInfo, c_guidDisplayAttributeBunsetsuFocus))
    {
        *ppInfo = new CDisplayAttributeInfoBunsetsuFocus();
        return *ppInfo ? S_OK : E_OUTOFMEMORY;
    }
    return E_INVALIDARG;
}

// wParam in TSF OnKey* is a virtual-key code. VK 'A'-'Z' map to 0x41-0x5A.
static bool IsAlphaKey(WPARAM wParam)
{
    return (wParam >= 'A' && wParam <= 'Z');
}

// 「これは閉じ括弧か」判定。caret 直後にこれが居たらコンポジション開始時に
// 吸収してよい。BracketPairCaretBackShift の閉じ側と対応させる。
static bool IsCloseBracketChar(wchar_t c)
{
    switch (c)
    {
    case L'」':
    case L'』':
    case L'）':
    case L'〕':
    case L'】':
    case L'］':
    case L'｝':
    case L'〉':
    case L'》':
    case L'〙':
    case L'〛':
    case L')':
    case L']':
    case L'}':
    case L'>':
    case L'”':
    case L'’':
        return true;
    default:
        return false;
    }
}

// 括弧ペアを閉じ括弧の直前で確定するためのキャレット後退幅を返す。
// text が「開き括弧 + 閉じ括弧」だけの 2 文字なら 1、そうでなければ 0。
// 「」『』（）〔〕【】［］《》〈〉〘〙〚〛など CJK 系括弧に加え、
// ASCII の () [] {} <> と各種引用符も面倒見る (全部 BMP 1 wchar_t)。
static size_t BracketPairCaretBackShift(const std::wstring& text)
{
    if (text.size() != 2) return 0;
    wchar_t o = text[0], c = text[1];
    switch (o)
    {
    case L'「': return (c == L'」') ? 1 : 0; // U+300C / U+300D
    case L'『': return (c == L'』') ? 1 : 0; // U+300E / U+300F
    case L'（': return (c == L'）') ? 1 : 0; // U+FF08 / U+FF09
    case L'〔': return (c == L'〕') ? 1 : 0; // U+3014 / U+3015
    case L'【': return (c == L'】') ? 1 : 0; // U+3010 / U+3011
    case L'［': return (c == L'］') ? 1 : 0; // U+FF3B / U+FF3D
    case L'｛': return (c == L'｝') ? 1 : 0; // U+FF5B / U+FF5D
    case L'〈': return (c == L'〉') ? 1 : 0; // U+3008 / U+3009
    case L'《': return (c == L'》') ? 1 : 0; // U+300A / U+300B
    case L'〘': return (c == L'〙') ? 1 : 0; // U+3018 / U+3019
    case L'〚': return (c == L'〛') ? 1 : 0; // U+301A / U+301B
    case L'(': return (c == L')') ? 1 : 0;
    case L'[': return (c == L']') ? 1 : 0;
    case L'{': return (c == L'}') ? 1 : 0;
    case L'<': return (c == L'>') ? 1 : 0;
    case L'"': return (c == L'"') ? 1 : 0;
    case L'\'': return (c == L'\'') ? 1 : 0;
    case L'“': return (c == L'”') ? 1 : 0; // U+201C / U+201D
    case L'‘': return (c == L'’') ? 1 : 0; // U+2018 / U+2019
    default: return 0;
    }
}

// OEM-* virtual keys that produce printable ASCII punctuation on the
// standard JIS / US layouts. We claim them while the IME is on so we can
// route the resolved character through the kana table (",", "." → "、", "。").
static bool IsSymbolKey(WPARAM wParam)
{
    if (wParam >= VK_OEM_1 && wParam <= VK_OEM_3) return true; // 0xBA-0xC0
    if (wParam >= VK_OEM_4 && wParam <= VK_OEM_8) return true; // 0xDB-0xDF
    if (wParam == VK_OEM_PLUS || wParam == VK_OEM_COMMA
        || wParam == VK_OEM_MINUS || wParam == VK_OEM_PERIOD)
        return true;
    // Number row when held with Shift produces ! @ # $ % ^ & * ( ) on a
    // US/JP layout — let the IME claim those so we can render them as
    // 全角「！」「＠」… in the composition. Unshifted digits stay with
    // the host (regular numeric input).
    if (wParam >= '0' && wParam <= '9' && (GetKeyState(VK_SHIFT) < 0)) return true;
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
    int n = ToUnicode(static_cast<UINT>(wParam), scanCode, keyState, buf, 8, 2);
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
    // カタカナひらがなローマ字 key on JP keyboards. The physical key emits
    // different virtual codes depending on shift state — VK_DBE_HIRAGANA
    // (0xF2) for the unshifted form (and for Ctrl+), VK_DBE_KATAKANA
    // (0xF1) for Shift+. (VK_KANA = 0x15 is left in as a defensive catch
    // for layouts that report it instead.) These are mode-switch keys we
    // want even when the IME is off, since one of their jobs is to turn
    // the IME on.
    if (wParam == 0xF2 /* VK_DBE_HIRAGANA */ ||
        wParam == 0xF1 /* VK_DBE_KATAKANA */ ||
        wParam == VK_KANA)
        return true;
    if (!m_isImeOn) return false;
    // Ctrl+Shift+F5 fallback claim. Preserved-key registration should
    // already have TSF routing this to OnPreservedKey, but observationally
    // that path fails in some hosts (browsers with their own key hooks,
    // certain Windows layouts) and the log never fires. Claiming it via
    // ShouldEat + OnKeyDown gives a second chance — whichever fires first
    // is fine, the misconversion logger is idempotent per tap.
    if (wParam == VK_F5
        && (GetKeyState(VK_CONTROL) < 0)
        && (GetKeyState(VK_SHIFT) < 0))
        return true;
    // Ctrl-modified keys are host shortcuts (Ctrl+X / C / V / A / Z / S /
    // arrow / etc) and belong to the application, not the IME. Without
    // this passthrough, IsAlphaKey would eat Ctrl+V and silently drop
    // the paste instead of letting Notepad / VS Code / browsers handle
    // it. Romaji input never uses the Ctrl chord, so giving up the whole
    // class is safe. Composition-in-progress is no exception: a Ctrl+V
    // mid-composition still goes to the app, which then either pastes
    // around the composition or replaces it — we let the host decide.
    if (GetKeyState(VK_CONTROL) < 0) return false;
    // 全角英数 mode: every printable key is committed directly as its
    // full-width form (ＩＭＥ / １２３ / ！＠＃ / 全角スペース), with no
    // composition or conversion. Shift/CapsLock therefore produce full-width
    // uppercase letters. Claim alpha, digits (shifted OR not), OEM symbols
    // and Space here so OnKeyDown's FullAlnum branch can resolve them; other
    // keys (Enter, Backspace, arrows) fall through to the host.
    if (m_imeMode == ImeMode::FullAlnum)
    {
        if (IsAlphaKey(wParam)) return true;
        if (wParam >= '0' && wParam <= '9') return true;
        if (IsSymbolKey(wParam)) return true;
        if (wParam == VK_SPACE) return true;
    }
    if (IsAlphaKey(wParam)) return true;
    if (IsSymbolKey(wParam)) return true;
    // Digit keys during an active composition stay in the buffer so
    // mixed input like「dai1kai」→「だい1かい」→ Space → 第1回 works.
    // In the full-width kana modes we ALSO claim a fresh (empty-buffer)
    // digit so it opens a composition as a full-width digit that Space can
    // convert to ①/一/Ⅰ (candidates keyed on "1" in the symbol dict).
    // Half-width / Off modes still pass a bare digit through to the app so
    // plain number typing in a document isn't intercepted. (Shift+digit is
    // number-row punctuation, already claimed by IsSymbolKey above, so only
    // unshifted digits reach here.) Candidate-window digit-select handling
    // (1-9 picks a candidate) lives further down.
    if (wParam >= '0' && wParam <= '9')
    {
        if (!m_romajiBuffer.empty()) return true;
        if (m_imeMode == ImeMode::Hiragana || m_imeMode == ImeMode::FullKatakana) return true;
    }
    if (wParam == VK_BACK && !m_romajiBuffer.empty()) return true;
    if ((wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_SPACE) && m_pComposition) return true;
    if (m_pCandWnd && m_pCandWnd->IsVisible())
    {
        if (wParam == VK_UP || wParam == VK_DOWN) return true;
        if (wParam == VK_TAB) return true;
        if (wParam == VK_NEXT || wParam == VK_PRIOR) return true;
        if (wParam >= '1' && wParam <= '9') return true;
        // Phase B: ←/→ navigate between bunsetsu. Alt+←/→ shrink/grow
        // the focused bunsetsu by one character. Without ShouldEat
        // returning true, OnKeyDown wouldn't see them and the host app
        // would move the caret inside the composition instead.
        if (!m_bunsetsuList.empty() && (wParam == VK_LEFT || wParam == VK_RIGHT))
            return true;
        // Shift+Delete: opt-out the highlighted candidate (blacklist).
        if (wParam == VK_DELETE && (GetKeyState(VK_SHIFT) < 0)) return true;
    }
    if (m_pComposition && wParam >= VK_F6 && wParam <= VK_F10) return true;
    // F5: Unicode-codepoint input. Two modes depending on composition state:
    //   - Composition live: buffer treated as 4-6 hex digits (BMP scalar
    //     for 4 digits, surrogate-pair-encoded supplementary plane scalar
    //     for 5-6 digits) → replaced with the resulting character.
    //     Combining marks: 「３０９９」+ F5 → U+3099 (dakuten),
    //     「３０９Ａ」+ F5 → U+309A (handakuten). Emoji: 「1F600」+ F5 → 😀.
    //   - Composition idle: reverse-lookup — open a fresh composition
    //     showing the hex codepoint of the LAST committed character,
    //     so pressing F5 after committing「任」opens a "4EFB" composition
    //     the user can Enter to keep or Escape to drop. Pressing F5 on
    //     that composition round-trips back to「任」via the normal path.
    // We claim the key whenever the IME could react (composition or a
    // remembered last commit) so hosts don't steal it mid-input.
    if (wParam == VK_F5 && (m_pComposition || !m_lastCommittedText.empty())) return true;
    // F4 repeat-paste: with no composition open, re-insert the previous
    // commit (symbol/emoji spam: ‼️‼️‼️…). Only claimed while there IS a
    // previous commit, so a fresh session leaves F4 to the host app.
    if (wParam == VK_F4 && !m_pComposition && !m_lastCommittedText.empty()
        && GetKeyState(VK_MENU) >= 0)
        return true; // Alt+F4 stays the host's
    // 変換 / 無変換 keys (Japanese keyboards). 変換 acts as a convert
    // trigger / re-convert; 無変換 cycles the composition's kana form.
    // Outside a live composition 変換 still applies when the host has a
    // selection (re-convert path) — we let the OnKeyDown handler decide,
    // here we just claim the key so TSF doesn't pass it to the host.
    if (wParam == VK_CONVERT || wParam == VK_NONCONVERT) return true;
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

    // カタカナひらがなローマ字 key. The physical key reports:
    //   - VK_DBE_HIRAGANA (0xF2) when pressed alone or with Ctrl
    //   - VK_DBE_KATAKANA (0xF1) when pressed with Shift
    // (VK_KANA = 0x15 is a fallback for layouts that report it.)
    // MS-IME-compatible behavior:
    //   - VK_DBE_HIRAGANA without Ctrl → ひらがな mode (and IME on)
    //   - VK_DBE_HIRAGANA with Ctrl    → IME off (romaji passthrough)
    //   - VK_DBE_KATAKANA              → 全角カタカナ mode (and IME on)
    // We treat VK_KANA the same as VK_DBE_HIRAGANA.
    if (wParam == 0xF2 /* VK_DBE_HIRAGANA */ || wParam == VK_KANA)
    {
        bool ctrl = (GetKeyState(VK_CONTROL) < 0);
        if (ctrl)
        {
            if (m_isImeOn) SetImeOpenClose(FALSE);
        }
        else
        {
            SetImeMode(ImeMode::Hiragana);
            if (!m_isImeOn) SetImeOpenClose(TRUE);
        }
        *pfEaten = TRUE;
        return S_OK;
    }
    if (wParam == 0xF1 /* VK_DBE_KATAKANA */)
    {
        SetImeMode(ImeMode::FullKatakana);
        if (!m_isImeOn) SetImeOpenClose(TRUE);
        *pfEaten = TRUE;
        return S_OK;
    }

    // Mode-switch key handling intentionally absent — see ShouldEat() comment.
    if (!m_isImeOn) return S_OK;

    // Ctrl+Shift+F5 fallback: preserved-key delivery is preferred but
    // observationally unreliable in some hosts. When ShouldEat claimed
    // this combo (matching predicate above), route it into the same
    // logger. AppendDebugLine breadcrumb makes it clear WHICH path
    // fired — preserved key vs OnKeyDown — for future diagnosis.
    if (wParam == VK_F5
        && (GetKeyState(VK_CONTROL) < 0)
        && (GetKeyState(VK_SHIFT) < 0))
    {
        AppendDebugLine(L"[GenerativeIME] Ctrl+Shift+F5 hit via OnKeyDown fallback");
        LogMisconversionAttempt();
        *pfEaten = TRUE;
        return S_OK;
    }

    // Ctrl-modified keys are host shortcuts (Ctrl+X / C / V / A / Z / S /
    // …) and never produce IME input. ShouldEat already returns false for
    // them so OnTestKeyDown signals "don't eat" to TSF — but OnKeyDown
    // still gets the call, and falling through to IsAlphaKey would push
    // the letter into m_romajiBuffer (a Ctrl+V would type "v" into the
    // composition while the host pastes around it). Bail out before any
    // input branch runs.
    if (GetKeyState(VK_CONTROL) < 0)
    {
        *pfEaten = FALSE;
        return S_OK;
    }

    // 全角英数 mode: resolve the key to the character the layout would emit
    // (ToUnicode honors both Shift and CapsLock, so Shift+I → 'I' → 'Ｉ'),
    // widen it, and drop it straight into the document with no composition.
    // This matches MS-IME's 全角英数 behavior: each keystroke commits a
    // full-width character immediately, and uppercase letters are typeable.
    if (m_imeMode == ImeMode::FullAlnum && !m_pComposition)
    {
        wchar_t ch = ResolveSymbolChar(wParam, lParam);
        if (ch >= 0x20 && ch <= 0x7E) // printable ASCII incl. space (→ 全角)
        {
            std::wstring full = ToFullWidthAscii(std::wstring(1, ch));
            if (pic) RequestEditSession(pic, EditAction::InsertDirect, full);
            AppendCommittedText(full);
            *pfEaten = TRUE;
            return S_OK;
        }
        // Non-printable keys (Enter, Backspace, arrows) fall through to the
        // host — they were never claimed by ShouldEat in this mode.
    }

    if (IsAlphaKey(wParam))
    {
        // If a candidate was already chosen (m_compositionConverted), close it
        // out first so this alpha key starts a NEW composition rather than
        // mutating m_romajiBuffer underneath the kanji.
        //
        // `committed` tracks whether the auto-commit actually fired. When it did,
        // the composition just ended (or is about to end via async EndCommit),
        // and the FOLLOWING RequestEditSession MUST be StartAndUpdate —
        // trusting `m_pComposition == nullptr` alone is unsafe because some
        // hosts defer our sync EndCommit to the async fallback path, so
        // m_pComposition can still be non-null when we reach the branch below.
        // Without this, the 2nd bunsetsu's first romaji pair landed as raw
        // ASCII ("k a") because we did Update on the stale 私 composition
        // instead of starting a fresh one.
        bool committed = CommitConvertedIfAny(pic);
        // Shift detection: GetKeyState alone is unreliable in the TSF
        // keystroke-sink context — the calling thread's cached key state
        // isn't always updated by the time OnKeyDown fires, so a physical
        // Shift+alpha in 全角ひらがな mode came through lowercase ("ime" →
        // 「いめ」). GetAsyncKeyState reads the live physical key state and
        // fills that gap; either signal counts as Shift held.
        bool shift = (GetKeyState(VK_SHIFT) < 0) || (GetAsyncKeyState(VK_SHIFT) < 0);
        wchar_t ch;
        if (!shift)
        {
            ch = static_cast<wchar_t>(wParam - 'A' + 'a'); // canonical lowercase romaji
        }
        else if (m_imeMode == ImeMode::Hiragana)
        {
            // 全角ひらがな mode: Shift+alpha injects a full-width UPPERCASE
            // letter ('A'-'Z' → 'Ａ'-'Ｚ') straight into the composition, so
            // typing Shift+I,M,E shows 「ＩＭＥ」. It stays un-converted (the
            // new full-width branch in romaji::Convert passes it through), so
            // the user can commit it as-is or open the candidate window to
            // swap to half-width before committing.
            ch = static_cast<wchar_t>(wParam + 0xFEE0);
        }
        else
        {
            // Other modes keep the historical half-width uppercase behavior
            // that feeds SKK direct entries like「Gすぽっと /Gスポット/」.
            ch = static_cast<wchar_t>(wParam); // 'A'-'Z' verbatim
        }
        m_romajiBuffer.push_back(ch);
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        if (pic)
        {
            bool fresh = committed || (m_pComposition == nullptr);
            EditAction action = fresh ? EditAction::StartAndUpdate : EditAction::Update;
            // Harvest the surrounding document text BEFORE the composition
            // opens, so the slice reflects committed content only.
            if (fresh)
            {
                ScanDocumentVocab(pic);
                // caret 直後が「」等ならコンポジションに吸収する。
                TryAbsorbCloseBracket(pic);
            }
            RequestEditSession(pic, action, display);
            UpdatePrediction(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam >= '0' && wParam <= '9'
        && GetKeyState(VK_SHIFT) >= 0
        && (!m_romajiBuffer.empty()
            || m_imeMode == ImeMode::Hiragana
            || m_imeMode == ImeMode::FullKatakana)
        && (wParam == '0' || m_predictionActive ||
            !m_pCandWnd || !m_pCandWnd->IsVisible()))
    {
        // UNSHIFTED digit inside an active composition (ShouldEat gate
        // above matches the same condition). Pushed as its literal ASCII
        // char so the romaji buffer looks like "dai1kai", which Convert()
        // then folds into「だい1かい」via its digit-passthrough branch.
        // Space afterwards runs the whole composition through the
        // SKK/MeCab conversion stack as usual. Shift+digit must NOT land
        // here — it's number-row punctuation (! " # …) and belongs to the
        // IsSymbolKey branch below; without the shift check, the second
        // key of「!!」was swallowed as「1」(「！１」bug).
        //
        // Conversion-candidate window visible (and not a prediction popup,
        // where the user is mid-typing and digits are still input): defer
        // to the 1-9 candidate-pick branch further down — this else-if
        // chain used to swallow the digit into the buffer first, so
        // number-row candidate selection never fired for romaji input.
        // '0' has no pick row, so it stays buffer input either way.
        bool committed = CommitConvertedIfAny(pic);
        m_romajiBuffer.push_back(static_cast<wchar_t>(wParam));
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        if (pic)
        {
            bool fresh = committed || (m_pComposition == nullptr);
            EditAction action = fresh ? EditAction::StartAndUpdate : EditAction::Update;
            if (fresh) TryAbsorbCloseBracket(pic);
            RequestEditSession(pic, action, display);
            UpdatePrediction(pic);
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
            bool committed = CommitConvertedIfAny(pic);
            m_romajiBuffer.push_back(ch);
            m_compositionConverted = FALSE;
            if (m_pCandWnd) m_pCandWnd->Hide();
            std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
            if (pic)
            {
                bool fresh = committed || (m_pComposition == nullptr);
                EditAction action = fresh ? EditAction::StartAndUpdate : EditAction::Update;
                if (fresh) TryAbsorbCloseBracket(pic);
                RequestEditSession(pic, action, display);
                UpdatePrediction(pic);
            }
            // Auto-commit on terminal punctuation removed: shortcut typing
            // was overriding user intent — a full-width 「？」 should stay
            // in the composition so the user can pick その他の候補 (？/?)
            // or back it out before Enter confirms. The candidate window
            // for full/half-width swapping is opened by Space via
            // TryOllamaConvertAsync's PunctPairs fast path, not here.
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_BACK && !m_romajiBuffer.empty())
    {
        // Backspace-one-kana. When the buffer is fully resolved to kana
        // ("aka" → "あか"), dropping just the last romaji char would leave
        // "あk" on screen — the user sees the previous kana visibly split
        // in half. Instead, when Convert has no unresolved tail, walk the
        // buffer back by the length of the last kana-producing chunk so
        // "あか" → "あ" (buffer "aka" → "a") in one press. When there IS
        // an unresolved tail ("aky" → "あky"), pop_back is still correct:
        // the user is fixing a typo mid-romaji and expects the single
        // trailing letter to disappear.
        auto rConv = romaji::Convert(m_romajiBuffer);
        size_t drop = 1;
        if (rConv.remaining.empty())
        {
            size_t step = romaji::LastKanaLen(m_romajiBuffer);
            if (step > 0 && step <= m_romajiBuffer.size()) drop = step;
        }
        m_romajiBuffer.erase(m_romajiBuffer.size() - drop);
        m_compositionConverted = FALSE;
        if (m_pCandWnd) m_pCandWnd->Hide();
        std::wstring display = DisplayForMode(m_romajiBuffer, m_imeMode);
        if (pic)
        {
            EditAction action = m_romajiBuffer.empty() ? EditAction::EndCancel : EditAction::Update;
            RequestEditSession(pic, action, display);
            // Empty buffer just cancelled the composition; UpdatePrediction
            // then only clears the prediction state (prefix too short).
            UpdatePrediction(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_RETURN && m_pComposition)
    {
        if (InBunsetsuMode())
        {
            // Sync the focused bunsetsu's pick from whatever the candidate
            // window is showing, then commit the joined-selected text.
            SyncFocusedBunsetsuSelection();
            std::wstring text = bunsetsu::JoinSelected(m_bunsetsuList);
            std::wstring joinedReading;
            std::vector<std::wstring> clauseReadings;
            // Per-bunsetsu learning: each (reading, chosen kanji) pair gets
            // recorded independently so future SKK/MeCab lookups of that
            // same reading promote what the user picked this time.
            if (m_pLearning)
            {
                AppContext ctx = AppContext::Capture();
                for (const auto& b : m_bunsetsuList)
                {
                    if (b.reading.empty() || b.candidates.empty()) continue;
                    if (b.selected >= b.candidates.size()) continue;
                    m_pLearning->Record(b.reading, b.candidates[b.selected], ctx);
                    joinedReading += b.reading;
                    clauseReadings.push_back(b.reading);
                }
                // Also seed the whole-phrase learning (see the matching
                // comment in CommitConvertedIfAny's Phase B branch). Next
                // time the same multi-clause reading comes in, the fav
                // fast path in TryOllamaConvertAsync hands back the
                // committed sentence directly and Phase B is skipped.
                if (!joinedReading.empty() && !text.empty() && m_bunsetsuList.size() >= 2)
                {
                    m_pLearning->Record(joinedReading, text, ctx);
                }
            }
            AppendCommittedText(text);
            if (!joinedReading.empty()) m_lastCommittedReading = joinedReading;
            m_lastCommittedClauseReadings = std::move(clauseReadings);
            if (pic) RequestEditSession(pic, EditAction::EndCommit, text);
            LeaveBunsetsuMode();
        }
        else if (m_compositionConverted)
        {
            // Range already holds the converted text. Remember the user's choice
            // so the same reading puts that candidate first next time. F6-F10
            // conversions live in m_fkeyConvertedText (the candidate window
            // was hidden when the F-key fired), normal candidate picks live
            // in m_pCandWnd's selection.
            std::wstring picked;
            if (!m_fkeyConvertedText.empty())
                picked = m_fkeyConvertedText;
            else if (m_pCandWnd)
                picked = m_pCandWnd->GetSelected();
            if (!picked.empty())
            {
                if (m_pLearning && !m_lastReading.empty())
                    m_pLearning->Record(m_lastReading, picked, AppContext::Capture());
                AppendCommittedText(picked);
                if (!m_lastReading.empty()) m_lastCommittedReading = m_lastReading;
            }
            size_t caretShift = BracketPairCaretBackShift(picked);
            // Pass picked as the final range text — see the matching commit
            // in CommitConvertedIfAny for why (Chromium contenteditable may
            // not preserve the range's Update'd content across Enter).
            if (pic) RequestEditSession(pic, EditAction::EndCommit, picked, caretShift);
        }
        else
        {
            // No conversion happened: resolve any trailing lone "n" to ん and commit.
            auto r = romaji::Convert(m_romajiBuffer);
            std::wstring finalText = r.hira + romaji::FinalizeTrailingN(r.remaining);
            // Mirror DisplayForMode: in the full-width kana modes commit the
            // form the user actually saw in the composition. Without this a
            // bare digit '1' was displayed as '１' via WidenAsciiDigits but
            // Enter committed the half-width '1' from the buffer (Bug 1).
            // FullKatakana additionally needs the kana widened to katakana.
            if (m_imeMode == ImeMode::FullKatakana)
                finalText = ToFullKatakana(finalText);
            if (m_imeMode == ImeMode::Hiragana || m_imeMode == ImeMode::FullKatakana)
                finalText = WidenAsciiDigits(finalText);
            AppendCommittedText(finalText);
            size_t caretShift = BracketPairCaretBackShift(finalText);
            if (pic) RequestEditSession(pic, EditAction::EndCommit, finalText, caretShift);
        }
        m_romajiBuffer.clear();
        m_compositionConverted = FALSE;
        m_lastReading.clear();
        m_fkeyConvertedText.clear();
        m_nonconvertCycle = 0;
        m_predictionActive = false;
        m_predictionReadings.clear();
        // Defense in depth: only the InBunsetsuMode branch above calls
        // LeaveBunsetsuMode explicitly, but m_bunsetsuList residue leaked
        // into the next composition when a converted-single-candidate or
        // no-conversion Enter path somehow left it populated. Downstream
        // ApplyCandidateSelection's InBunsetsuMode branch then overwrote
        // the new composition with JoinSelected of the OLD clauses (bug:
        // 「使って良い」変換後に「１」変換でコンポジションに「使って良い」が
        // 残る). Idempotent when the list is already empty.
        LeaveBunsetsuMode();
        if (m_pCandWnd) m_pCandWnd->Hide();
        *pfEaten = TRUE;
    }
    else if (wParam == VK_ESCAPE && m_pComposition)
    {
        if (pic) RequestEditSession(pic, EditAction::EndCancel, L"");
        m_romajiBuffer.clear();
        m_compositionConverted = FALSE;
        m_fkeyConvertedText.clear();
        m_nonconvertCycle = 0;
        m_predictionActive = false;
        m_predictionReadings.clear();
        LeaveBunsetsuMode();
        if (m_pCandWnd) m_pCandWnd->Hide();
        *pfEaten = TRUE;
    }
    else if (wParam == VK_SPACE && m_pComposition)
    {
        // When the popup is already up, Space cycles to the next candidate
        // (matches typical IME behavior). Otherwise kick off a fresh async
        // Ollama call. Speculative predictions are the exception: while the
        // popup only shows predictions the user hasn't entered (composition
        // still raw kana), Space means "convert what I typed" — same as if
        // the popup weren't there. Once they HAVE entered the prediction
        // list, Space cycles through it like any candidate list.
        if (m_pCandWnd && m_pCandWnd->IsVisible())
        {
            if (m_predictionActive && !m_compositionConverted)
            {
                m_pCandWnd->Hide();
                if (pic) TryOllamaConvertAsync(pic);
            }
            else if (m_predictionActive)
            {
                NavigatePrediction(+1, pic);
            }
            else
            {
                m_pCandWnd->SelectNext();
                ApplyCandidateSelection(pic);
            }
        }
        else if (pic)
        {
            TryOllamaConvertAsync(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_CONVERT)
    {
        // 変換 key. Three behaviors depending on state:
        //   - Composition + candidate window up → cycle next candidate
        //     (same as Space).
        //   - Composition without candidate window → start conversion
        //     (same as Space).
        //   - No composition + host has a selection → re-convert the
        //     selected text (TryReconvertFromSelection).
        if (m_pComposition)
        {
            if (m_pCandWnd && m_pCandWnd->IsVisible())
            {
                // Same prediction-aware routing as VK_SPACE above.
                if (m_predictionActive && !m_compositionConverted)
                {
                    m_pCandWnd->Hide();
                    if (pic) TryOllamaConvertAsync(pic);
                }
                else if (m_predictionActive)
                {
                    NavigatePrediction(+1, pic);
                }
                else
                {
                    m_pCandWnd->SelectNext();
                    ApplyCandidateSelection(pic);
                }
            }
            else if (pic)
            {
                TryOllamaConvertAsync(pic);
            }
        }
        else if (pic)
        {
            TryReconvertFromSelection(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_NONCONVERT && m_pComposition)
    {
        // 無変換 key. Two modes depending on whether the candidate window
        // is up:
        //   - Window visible (mid-conversion) → cancel back to the bare
        //     hiragana reading. Drops the kanji choice and dismisses the
        //     popup; Enter still commits, learning records the reading
        //     itself as the picked form.
        //   - No window / pre-conversion → cycle the composition's kana
        //     form (ひらがな / 全角カナ / 半角カナ / ローマ字).
        if (InBunsetsuMode())
        {
            // Phase B (multi-bunsetsu): revert ONLY the focused clause to its
            // bare reading and keep every other clause intact. The old code
            // called LeaveBunsetsuMode()+Update(m_lastReading), but in Phase B
            // m_lastReading is just the FOCUSED clause's reading (set by
            // RepaintBunsetsu), so that replaced the whole composition with
            // one clause's kana and dropped all the others — the "無変換で
            // 他の文節が消える" bug. Instead, point the focused clause at its
            // reading (insert it as a candidate if absent) and repaint the
            // joined composition so the other clauses survive.
            Bunsetsu& cur = m_bunsetsuList[m_focusedBunsetsu];
            auto it = std::find(cur.candidates.begin(), cur.candidates.end(), cur.reading);
            if (it != cur.candidates.end())
            {
                cur.selected = static_cast<size_t>(std::distance(cur.candidates.begin(), it));
            }
            else
            {
                cur.candidates.insert(cur.candidates.begin(), cur.reading);
                cur.selected = 0;
            }
            RepaintBunsetsu(pic);
        }
        else if (m_pCandWnd && m_pCandWnd->IsVisible() && !m_lastReading.empty())
        {
            m_pCandWnd->Hide();
            if (pic) RequestEditSession(pic, EditAction::Update, m_lastReading);
            m_compositionConverted = TRUE;
            m_fkeyConvertedText = m_lastReading;
            m_nonconvertCycle = 0;
            m_predictionActive = false;
            m_predictionReadings.clear();
        }
        else
        {
            // Dismiss an un-entered prediction popup first — the kana-form
            // cycle changes the composition text out from under it.
            if (m_predictionActive && m_pCandWnd)
            {
                m_pCandWnd->Hide();
                m_predictionActive = false;
                m_predictionReadings.clear();
            }
            CycleNonconvertForm(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_DOWN && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        if (m_predictionActive)
        {
            NavigatePrediction(+1, pic);
        }
        else
        {
            m_pCandWnd->SelectNext();
            if (pic) ApplyCandidateSelection(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_UP && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        if (m_predictionActive)
        {
            NavigatePrediction(-1, pic);
        }
        else
        {
            m_pCandWnd->SelectPrev();
            if (pic) ApplyCandidateSelection(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_TAB && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        if (m_predictionActive)
        {
            NavigatePrediction(GetKeyState(VK_SHIFT) < 0 ? -1 : +1, pic);
        }
        else if (InBunsetsuMode() && m_bunsetsuList.size() > 1)
        {
            // Phase B: Tab moves bunsetsu focus, not candidate selection.
            // Save the current focus's pick first so re-entering this
            // bunsetsu later restores what the user landed on.
            SyncFocusedBunsetsuSelection();

            size_t n = m_bunsetsuList.size();
            if (GetKeyState(VK_SHIFT) < 0)
                m_focusedBunsetsu = (m_focusedBunsetsu + n - 1) % n;
            else
                m_focusedBunsetsu = (m_focusedBunsetsu + 1) % n;

            wchar_t logbuf[160];
            swprintf_s(logbuf,
                       L"[GenerativeIME] Phase B: focus -> %zu (reading=%s)\n",
                       m_focusedBunsetsu,
                       m_bunsetsuList[m_focusedBunsetsu].reading.c_str());
            OutputDebugStringW(logbuf);
            RepaintBunsetsu(pic);
        }
        else
        {
            // Single-bunsetsu mode: Tab cycles candidate window selection.
            if (GetKeyState(VK_SHIFT) < 0) m_pCandWnd->SelectPrev();
            else m_pCandWnd->SelectNext();
            if (pic) ApplyCandidateSelection(pic);
        }
        *pfEaten = TRUE;
    }
    else if ((wParam == VK_LEFT || wParam == VK_RIGHT) &&
        !InBunsetsuMode() &&
        m_pCandWnd && m_pCandWnd->IsVisible() &&
        (GetKeyState(VK_SHIFT) < 0) &&
        !m_lastReading.empty() &&
        m_lastReading.size() >= 2)
    {
        // Shift+←/→ pressed while showing a whole-reading single candidate
        // (SKK direct hit for「パーキンソン病」/「アルツハイマー病」/loan
        // words / etc.) - auto-enter Phase B by splitting the reading into
        // two bunsetsu at (len-1, 1). ResizeFocusedBunsetsu then handles
        // any further shrink/grow the user requests. Without this, whole-
        // reading matches short-circuited past bunsetsu mode and Shift+
        // ←/→ silently no-op'd on those inputs.
        auto* mecab = MecabAnalyzer::GetGlobal();
        auto* skk = SkkDictionary::GetGlobal();
        size_t cut = m_lastReading.size() - 1;
        std::wstring headR = m_lastReading.substr(0, cut);
        std::wstring tailR = m_lastReading.substr(cut);
        std::vector<Bunsetsu> parts;
        parts.push_back(bunsetsu::MakeBunsetsuFromReading(headR, mecab, skk));
        parts.push_back(bunsetsu::MakeBunsetsuFromReading(tailR, mecab, skk));
        if (m_pLearning)
        {
            for (auto& b : parts)
            {
                if (!b.candidates.empty())
                {
                    b.candidates = m_pLearning->Reorder(b.reading, b.candidates);
                    b.selected = 0;
                }
            }
        }
        EnterBunsetsuMode(std::move(parts), pic);
        // If the user pressed Shift+Right (grow), the split we just made
        // is by 1 char too small - fold the tail char back in. Shift+Left
        // (shrink) needs no follow-up because "split off 1 to tail" IS the
        // shrink for a single-bunsetsu source.
        if (wParam == VK_RIGHT)
        {
            ResizeFocusedBunsetsu(+1, pic);
        }
        *pfEaten = TRUE;
    }
    else if ((wParam == VK_LEFT || wParam == VK_RIGHT) &&
        InBunsetsuMode() &&
        m_pCandWnd && m_pCandWnd->IsVisible())
    {
        // Shift+←/→ resize the focused bunsetsu's reading by one char,
        // pushing or pulling the character across the boundary with the
        // next bunsetsu. Plain ←/→ navigate between bunsetsu. We use
        // Shift instead of Alt because Alt-modified keys arrive as
        // WM_SYSKEYDOWN, which TSF's ITfKeyEventSink doesn't see — and
        // Shift+←/→ is the MS-IME standard for clause resizing anyway.
        bool shiftDown = (GetKeyState(VK_SHIFT) < 0);
        if (shiftDown)
        {
            ResizeFocusedBunsetsu(wParam == VK_RIGHT ? +1 : -1, pic);
        }
        else if (m_bunsetsuList.size() > 1)
        {
            SyncFocusedBunsetsuSelection();

            size_t n = m_bunsetsuList.size();
            if (wParam == VK_LEFT)
                m_focusedBunsetsu = (m_focusedBunsetsu + n - 1) % n;
            else
                m_focusedBunsetsu = (m_focusedBunsetsu + 1) % n;

            wchar_t logbuf[160];
            swprintf_s(logbuf,
                       L"[GenerativeIME] Phase B (arrow): focus -> %zu (reading=%s)\n",
                       m_focusedBunsetsu,
                       m_bunsetsuList[m_focusedBunsetsu].reading.c_str());
            OutputDebugStringW(logbuf);
            RepaintBunsetsu(pic);
        }
        *pfEaten = TRUE;
    }
    else if (wParam == VK_NEXT && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->PageNext();
        if (m_predictionActive) NavigatePrediction(0, pic);
        else if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_PRIOR && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        m_pCandWnd->PagePrev();
        if (m_predictionActive) NavigatePrediction(0, pic);
        else if (pic) ApplyCandidateSelection(pic);
        *pfEaten = TRUE;
    }
    else if (wParam == VK_DELETE)
    {
        wchar_t logbuf[200];
        bool shift = (GetKeyState(VK_SHIFT) < 0);
        bool ctrl = (GetKeyState(VK_CONTROL) < 0);
        bool wndVis = (m_pCandWnd && m_pCandWnd->IsVisible());
        swprintf_s(logbuf,
                   L"[GenerativeIME] VK_DELETE: shift=%d ctrl=%d wndVisible=%d bunsetsu=%d\n",
                   shift, ctrl, wndVis, InBunsetsuMode());
        OutputDebugStringW(logbuf);
        if (shift && wndVis)
        {
            // Shift+Delete: opt-out. Behavior depends on mode.
            if (InBunsetsuMode())
            {
                // Phase B: blacklist the CURRENT bunsetsu boundary pattern.
                // The user declared that this partition itself is wrong
                // (not just the per-bunsetsu picks). Record reading-total
                // → end-offset array so next time SplitMecab tries the
                // same shape we recognise it as forbidden and route to
                // Ollama for a fresh take.
                if (m_pLearning && !m_bunsetsuList.empty())
                {
                    std::wstring fullReading;
                    std::vector<size_t> endOffsets;
                    for (const auto& b : m_bunsetsuList)
                    {
                        fullReading += b.reading;
                        endOffsets.push_back(fullReading.size());
                    }
                    // Drop the trailing entry (always equals reading.size()
                    // by construction) so the same boundary array compares
                    // equal regardless of total length.
                    if (!endOffsets.empty()) endOffsets.pop_back();
                    m_pLearning->BlacklistBoundary(fullReading, endOffsets);
                    wchar_t logbuf[300];
                    std::wstring joined;
                    for (size_t i = 0; i < endOffsets.size(); ++i)
                    {
                        if (i) joined.push_back(L',');
                        joined += std::to_wstring(endOffsets[i]);
                    }
                    swprintf_s(logbuf,
                               L"[GenerativeIME] BlacklistBoundary: reading=%s ends=[%s]\n",
                               fullReading.c_str(), joined.c_str());
                    OutputDebugStringW(logbuf);
                }
                // Leave Phase B for now; the user will retype (or hit Esc)
                // and the next conversion will avoid this boundary shape.
                if (m_pCandWnd) m_pCandWnd->Hide();
                LeaveBunsetsuMode();
                if (pic) RequestEditSession(pic, EditAction::EndCancel, L"");
                m_romajiBuffer.clear();
                m_compositionConverted = FALSE;
                m_lastReading.clear();
            }
            else
            {
                // Single-candidate mode: opt-out the highlighted entry AND
                // the SplitMecab boundary that produced it. Recording both
                // means a re-type of the same composite reading skips Phase
                // B (boundary blacklist hits) AND skips this exact joined
                // candidate (candidate blacklist hits) so the Ollama
                // fallback's answer gets a clean shot at the top slot.
                int sel = m_pCandWnd->GetSelectedIndex();
                auto cur = m_pCandWnd->GetCandidates();
                if (sel >= 0 && sel < static_cast<int>(cur.size()) && !m_lastReading.empty())
                {
                    std::wstring victim = cur[sel];
                    if (m_pLearning && !victim.empty())
                    {
                        m_pLearning->Blacklist(m_lastReading, victim);
                        wchar_t logbuf[200];
                        swprintf_s(logbuf,
                                   L"[GenerativeIME] Blacklist: reading=%s word=%s\n",
                                   m_lastReading.c_str(), victim.c_str());
                        OutputDebugStringW(logbuf);
                    }
                    // Also record the boundary so even if a future analyzer
                    // run produces a different joined string with the same
                    // shape, Phase B still gets bypassed.
                    if (m_pLearning)
                    {
                        if (auto* mc = MecabAnalyzer::GetGlobal();
                            mc && mc->IsReady())
                        {
                            auto* sk = SkkDictionary::GetGlobal();
                            auto parts = bunsetsu::SplitMecab(m_lastReading, *mc, sk);
                            if (parts.size() >= 2)
                            {
                                std::vector<size_t> endOffsets;
                                size_t cum = 0;
                                for (const auto& p : parts)
                                {
                                    cum += p.reading.size();
                                    endOffsets.push_back(cum);
                                }
                                if (!endOffsets.empty()) endOffsets.pop_back();
                                m_pLearning->BlacklistBoundary(m_lastReading, endOffsets);
                                wchar_t logbuf[260];
                                std::wstring joined;
                                for (size_t i = 0; i < endOffsets.size(); ++i)
                                {
                                    if (i) joined.push_back(L',');
                                    joined += std::to_wstring(endOffsets[i]);
                                }
                                swprintf_s(logbuf,
                                           L"[GenerativeIME] BlacklistBoundary (single): reading=%s ends=[%s]\n",
                                           m_lastReading.c_str(), joined.c_str());
                                OutputDebugStringW(logbuf);
                            }
                        }
                    }
                    cur.erase(cur.begin() + sel);
                    // Keep the prediction reading list parallel to the shown
                    // candidates, or later navigation would map the wrong
                    // full reading to a pick.
                    if (m_predictionActive && sel < static_cast<int>(m_predictionReadings.size()))
                        m_predictionReadings.erase(m_predictionReadings.begin() + sel);
                    if (cur.empty())
                    {
                        m_pCandWnd->Hide();
                        m_predictionActive = false;
                        m_predictionReadings.clear();
                    }
                    else
                    {
                        m_pCandWnd->SetCandidates(cur);
                        if (m_predictionActive) NavigatePrediction(0, pic);
                        else ApplyCandidateSelection(pic);
                    }
                }
            }
            *pfEaten = TRUE;
        }
    }
    else if (wParam == VK_F5)
    {
        // Unicode-codepoint input. When the current buffer is exactly
        // 4 hex digits (half-width digits, half/full-width A-F, or full-
        // width digits — any casing), replace the composition with the
        // corresponding BMP scalar value's character. Anything else is
        // left untouched (key is still eaten so it doesn't reach the
        // host and open a menu / trigger a browser refresh).
        //
        // The main use case is inserting a combining voiced-sound mark
        // right after a character:
        //   ふ [commit] → ３０９Ａ F5 [commit] → ぷ
        //   任 [commit] → ３０９９ F5 [commit] → 任゙
        // because U+3099 / U+309A are combining marks that bind to the
        // preceding base character on render. The IME just makes the
        // codepoint typeable — the combining semantics are up to the
        // font/shaper on the host side.
        auto hexVal = [](wchar_t c) -> int
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
            if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
            if (c >= 0xFF10 && c <= 0xFF19) return c - 0xFF10; // '０'-'９'
            if (c >= 0xFF41 && c <= 0xFF46) return 10 + (c - 0xFF41); // 'ａ'-'ｆ'
            if (c >= 0xFF21 && c <= 0xFF26) return 10 + (c - 0xFF21); // 'Ａ'-'Ｆ'
            return -1;
        };
        if (m_pComposition)
        {
            // Forward: 4-6 hex digits → Unicode scalar value.
            // 4 digits addresses the BMP directly; 5-6 digits address the
            // supplementary planes and get UTF-16 surrogate-pair encoded.
            //
            // Source is normally the romaji buffer (the user typed the
            // hex directly). For the roundtrip case — reverse F5 wrote
            // its hex string into m_fkeyConvertedText and cleared the
            // buffer to keep DisplayForMode from mangling it — we fall
            // back to that field so a second F5 tap flips「4EFB」back
            // to「任」.
            const std::wstring& src = !m_romajiBuffer.empty()
                                          ? m_romajiBuffer
                                          : m_fkeyConvertedText;
            const size_t n = src.size();
            if (n >= 4 && n <= 6)
            {
                unsigned cp = 0;
                bool ok = true;
                for (size_t i = 0; i < n; ++i)
                {
                    int v = hexVal(src[i]);
                    if (v < 0)
                    {
                        ok = false;
                        break;
                    }
                    cp = (cp << 4) | static_cast<unsigned>(v);
                }
                // Valid Unicode scalar: 0..10FFFF minus the surrogate
                // range D800..DFFF (those are code units, not scalar
                // values on their own).
                if (ok && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF))
                {
                    std::wstring text;
                    if (cp <= 0xFFFF)
                    {
                        text.push_back(static_cast<wchar_t>(cp));
                    }
                    else
                    {
                        // Supplementary plane → UTF-16 surrogate pair.
                        // U+1F600 → 0xD83D 0xDE00 = a 😀 emoji.
                        unsigned s = cp - 0x10000;
                        text.push_back(static_cast<wchar_t>(0xD800 | (s >> 10)));
                        text.push_back(static_cast<wchar_t>(0xDC00 | (s & 0x3FF)));
                    }
                    if (m_pCandWnd) m_pCandWnd->Hide();
                    if (pic) RequestEditSession(pic, EditAction::Update, text);
                    m_compositionConverted = TRUE;
                    m_fkeyConvertedText = text;
                    m_lastReading = src;
                    m_predictionActive = false;
                    m_predictionReadings.clear();
                }
            }
        }
        else if (!m_lastCommittedText.empty() && pic)
        {
            // Idle F5 → in-place toggle. If the last commit was a normal
            // character (「任」), the trailing 1-2 wchars of the document
            // are swapped for the U+xxxx hex form ("4EFB"). If the last
            // commit was 4-6 hex chars we ourselves emitted, they're
            // swapped back for the encoded character. No composition
            // opens, no Enter needed — the doc mutates in place and F5
            // becomes a clean toggle key.
            //
            // We used to open a composition holding the hex string, but
            // that stranded「任」in the document (F5 → doc:「任4EFB[composition]」
            // → Enter →「任4EFB」 which is not what "show me the codepoint of
            // 任" was meant to produce). In-place matches the user's
            // mental model of "F5 replaces the last thing I typed with its
            // Unicode counterpart".
            ToggleCodepointInPlace(pic);
        }
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
        case VK_F6: text = hira;
            break;
        case VK_F7: text = ToFullKatakana(hira);
            break;
        case VK_F8: text = ToHalfKatakana(ToFullKatakana(hira));
            break;
        case VK_F9: text = ToFullWidthAscii(m_romajiBuffer);
            break;
        case VK_F10: text = m_romajiBuffer;
            break;
        }
        if (m_pCandWnd) m_pCandWnd->Hide();
        if (pic && !text.empty()) RequestEditSession(pic, EditAction::Update, text);
        m_compositionConverted = TRUE;
        m_fkeyConvertedText = text;
        m_lastReading = hira; // learning key for the F-key form
        m_predictionActive = false; // F-key form supersedes predictions
        m_predictionReadings.clear();
        *pfEaten = TRUE;
    }
    else if (wParam == VK_F4 && !m_pComposition && !m_lastCommittedText.empty()
        && GetKeyState(VK_MENU) >= 0)
    {
        // Repeat-paste (記号連打): re-insert the previous commit verbatim,
        // no composition round-trip. AppendCommittedText keeps the LLM
        // context buffer in sync (m_lastCommittedText is overwritten with
        // the same string, so repeats keep repeating).
        if (pic) RequestEditSession(pic, EditAction::InsertDirect, m_lastCommittedText);
        AppendCommittedText(m_lastCommittedText);
        *pfEaten = TRUE;
    }
    else if (wParam >= '1' && wParam <= '9' && m_pCandWnd && m_pCandWnd->IsVisible())
    {
        // Number key: jump to that row of the current page (the rendered
        // index is 1-based per page, not per full candidate list).
        int idx = m_pCandWnd->GetPageStart() + static_cast<int>(wParam - '1');
        if (idx < static_cast<int>(m_pCandWnd->Count()))
        {
            m_pCandWnd->SelectIndex(idx);
            ApplyCandidateSelection(pic);
            if (InBunsetsuMode())
            {
                // Phase B: mirror the VK_RETURN bunsetsu-commit path. The
                // number key picked ONE clause; ApplyCandidateSelection has
                // already synced that pick into m_bunsetsuList and rendered
                // JoinSelected into the composition range. Commit the full
                // join (not just the single clause's Selected()) and record
                // per-clause learning like Enter does. Without this branch
                // the previous code committed only the focused clause's
                // text to AppendCommittedText / learning, and left
                // m_bunsetsuList behind — the next composition then hit
                // ApplyCandidateSelection's InBunsetsuMode branch and
                // painted a stale JoinSelected over its display.
                SyncFocusedBunsetsuSelection();
                std::wstring text = bunsetsu::JoinSelected(m_bunsetsuList);
                std::wstring joinedReading;
                std::vector<std::wstring> clauseReadings;
                if (m_pLearning)
                {
                    AppContext ctx = AppContext::Capture();
                    for (const auto& b : m_bunsetsuList)
                    {
                        if (b.reading.empty() || b.candidates.empty()) continue;
                        if (b.selected >= b.candidates.size()) continue;
                        m_pLearning->Record(b.reading, b.candidates[b.selected], ctx);
                        joinedReading += b.reading;
                        clauseReadings.push_back(b.reading);
                    }
                    if (!joinedReading.empty() && !text.empty() && m_bunsetsuList.size() >= 2)
                    {
                        m_pLearning->Record(joinedReading, text, ctx);
                    }
                }
                AppendCommittedText(text);
                if (!joinedReading.empty()) m_lastCommittedReading = joinedReading;
                m_lastCommittedClauseReadings = std::move(clauseReadings);
                if (pic) RequestEditSession(pic, EditAction::EndCommit, text);
                LeaveBunsetsuMode();
            }
            else
            {
                std::wstring picked = m_pCandWnd->GetSelected();
                if (m_pLearning && !m_lastReading.empty())
                {
                    m_pLearning->Record(m_lastReading, picked, AppContext::Capture());
                }
                AppendCommittedText(picked);
                if (!m_lastReading.empty()) m_lastCommittedReading = m_lastReading;
                size_t caretShift = BracketPairCaretBackShift(picked);
                if (pic) RequestEditSession(pic, EditAction::EndCommit, picked, caretShift);
            }
            m_romajiBuffer.clear();
            m_compositionConverted = FALSE;
            m_lastReading.clear();
            m_fkeyConvertedText.clear();
            m_predictionActive = false;
            m_predictionReadings.clear();
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

    if (IsEqualGUID(rguid, c_guidKeyKanji) ||
        IsEqualGUID(rguid, c_guidKeyImeOn) ||
        IsEqualGUID(rguid, c_guidKeyImeOff))
    {
        // All three preserved keys map to the same physical 半角/全角 key
        // on JIS keyboards — the choice between VK_KANJI, VK_OEM_AUTO
        // ("activate IME"), and VK_OEM_ENLW ("deactivate IME") is what
        // Windows emits based on ITS view of the OPENCLOSE compartment.
        // The old handler trusted that view: OEM_AUTO → SetImeOpenClose(TRUE),
        // OEM_ENLW → SetImeOpenClose(FALSE). That works while Windows and
        // our m_isImeOn agree, but breaks when they drift.
        //
        // Observed drift (cmd launched from Explorer): m_isImeOn defaults
        // to TRUE in the constructor and Activate assumes the follow-on
        // SetIMEStateCompartments(TRUE) reaches the global OPENCLOSE.
        // In conhost that write can silently no-op (foreign-writer
        // gating), leaving OPENCLOSE=0 while our cache still says TRUE.
        // Windows then emits VK_OEM_AUTO on the first 半角/全角 tap ("I
        // think IME is off, turn it on"), and our old handler called
        // SetImeOpenClose(TRUE) — which merely synced the compartment
        // up to what m_isImeOn already claimed. Nothing visibly changed.
        // The user tapped again, Windows now saw OPENCLOSE=1, emitted
        // VK_OEM_ENLW, and only THEN did the IME turn off. Two taps to
        // reach half-width instead of one.
        //
        // Fix: sync m_isImeOn from the compartment first (so a stale
        // cache doesn't trip us), then toggle. All three keys go through
        // this same path because the physical intent is identical —
        // "give me the opposite of whatever state I'm in now."
        SyncImeStateFromCompartments();
        SetImeOpenClose(!m_isImeOn);
        *pfEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, c_guidKeyDebugLog))
    {
        // Ctrl+F5: append the current attempt state to the misconversion
        // log. Runs regardless of composition/commit state — an empty
        // buffer just produces an entry with blank fields, which is still
        // useful ("I was in App X and nothing was queued").
        LogMisconversionAttempt();
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
    // Full state reset — mirror the VK_RETURN commit path. Previously only
    // the romaji buffer and the converted flag were cleared, so a forced
    // termination mid-conversion left m_bunsetsuList / m_lastReading /
    // m_fkeyConvertedText behind. InBunsetsuMode() then stayed true into the
    // NEXT fresh composition, and a bare Enter re-committed the PREVIOUS
    // conversion's JoinSelected — the "直前の変換結果がコンポジションに出る"
    // bug. Clearing all of it here makes the next input start clean.
    LeaveBunsetsuMode();
    m_lastReading.clear();
    m_fkeyConvertedText.clear();
    m_nonconvertCycle = 0;
    m_predictionActive = false;
    m_predictionReadings.clear();
    // 吸収した「」はもうコンポジション範囲の一部として doc に定着済み
    // (host が範囲テキストごと持って行った)。state だけリセット。
    m_absorbedCloseBracket = 0;
    if (m_pCandWnd) m_pCandWnd->Hide();
    return S_OK;
}

void CTextService::SetComposition(ITfComposition* pComposition)
{
    if (m_pComposition)
    {
        m_pComposition->Release();
        m_pComposition = nullptr;
    }
    if (pComposition)
    {
        m_pComposition = pComposition;
        m_pComposition->AddRef();
    }
}

HRESULT CTextService::RequestEditSession(ITfContext* pContext, EditAction action, const std::wstring& text,
                                         size_t caretOffsetFromEnd)
{
    // 吸収した閉じ括弧はコンポジション表示に自動付加する。
    // EndCommit は m_text 空で「範囲そのまま確定」する運用があり、その場合の
    // 括弧は既にコンポジション末尾に含まれているので追加しない。
    // EndCancel は下で m_cancelReplacement 経由で「」を復元させるため素通し。
    std::wstring effectiveText = text;
    size_t effectiveCaret = caretOffsetFromEnd;
    if (m_absorbedCloseBracket != 0)
    {
        bool appendHere = (action == EditAction::StartAndUpdate)
            || (action == EditAction::Update)
            || (action == EditAction::EndCommit && !text.empty());
        if (appendHere)
        {
            effectiveText.push_back(m_absorbedCloseBracket);
            if (effectiveCaret == 0) effectiveCaret = 1;
        }
        // EndCommit(空 text) の場合はコンポジション既存末尾に「」が居るので
        // caret offset だけ 1 引き上げてペア内側に留める。
        if (action == EditAction::EndCommit && text.empty() && effectiveCaret == 0)
        {
            effectiveCaret = 1;
        }
    }

    auto pSession = new CEditSession(this, pContext, action, effectiveText);

    // When we're in Phase B AND the action is an Update of the live
    // composition, attach the current focused-bunsetsu offset so the edit
    // session can split the display attribute and highlight just that
    // clause. Other actions (Start, EndCommit, EndCancel) don't care.
    if (InBunsetsuMode() && action == EditAction::Update)
    {
        size_t start = 0;
        for (size_t i = 0; i < m_focusedBunsetsu && i < m_bunsetsuList.size(); ++i)
            start += m_bunsetsuList[i].Selected().size();
        size_t len = (m_focusedBunsetsu < m_bunsetsuList.size())
                         ? m_bunsetsuList[m_focusedBunsetsu].Selected().size()
                         : 0;
        if (len > 0) pSession->SetBunsetsuFocus(start, len);
    }

    if (effectiveCaret > 0)
        pSession->SetCaretOffsetFromEnd(effectiveCaret);

    // EndCancel: 吸収した「」を元の位置 (コンポジション範囲) に置き戻す。
    if (action == EditAction::EndCancel && m_absorbedCloseBracket != 0)
    {
        std::wstring rep(1, m_absorbedCloseBracket);
        pSession->SetCancelReplacement(rep);
    }

    // 状態クリア: 確定/キャンセルが走ったら吸収を忘れる。Update 系ではまだ
    // コンポジション継続中なので保持。
    if (action == EditAction::EndCommit || action == EditAction::EndCancel)
        m_absorbedCloseBracket = 0;

    // First try synchronous read-write.
    HRESULT hrSession = S_OK;
    HRESULT hr = pContext->RequestEditSession(m_tfClientId, pSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    if (FAILED(hr) || FAILED(hrSession))
    {
        hrSession = S_OK;
        hr = pContext->RequestEditSession(m_tfClientId, pSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hrSession);
        wchar_t buf[200];
        swprintf_s(buf, L"[GenerativeIME] RequestEditSession fell back to async hr=0x%08X session=0x%08X\n",
                   static_cast<unsigned>(hr), static_cast<unsigned>(hrSession));
        OutputDebugStringW(buf);
    }
    pSession->Release();
    return hr;
}
