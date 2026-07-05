#pragma once

#include "globals.h"
#include "bunsetsu.h"
#include <msctf.h>
#include <string>
#include <unordered_map>
#include <vector>

enum class EditAction;
class CLangBarItemButton;
class CCandidateWindow;
class LearningStore;
struct PendingOllamaRequest;
struct PendingOllamaReorderRequest;
struct PendingOllamaFallbackRequest;
struct PendingAcronymRequest;

// Active "input mode" inside the IME. Off means IME is bypassing input entirely;
// the other four shape how m_romajiBuffer renders in the composition.
enum class ImeMode
{
    Off,           // IME is off; keys pass through
    Hiragana,      // あ
    FullKatakana,  // ア
    HalfKatakana,  // ｱ
    FullAlnum,     // ａ
};

class CTextService
    : public ITfTextInputProcessor
    , public ITfKeyEventSink
    , public ITfCompositionSink
    , public ITfDisplayAttributeProvider
    , public ITfCompartmentEventSink
{
public:
    CTextService();

    // IUnknown
    STDMETHODIMP            QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG)    AddRef() override;
    STDMETHODIMP_(ULONG)    Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guidInfo, ITfDisplayAttributeInfo** ppInfo) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // Used by CEditSession to read / write composition state.
    void            SetComposition(ITfComposition* pComposition);
    ITfComposition* GetComposition() const { return m_pComposition; }

    // Exposed for CLangBarItemButton::OnClick to flip IME mode.
    BOOL IsImeOn() const { return m_isImeOn; }
    void ToggleImeFromUI() { SetImeOpenClose(!m_isImeOn); }

    ImeMode GetImeMode() const { return m_imeMode; }
    void    SetImeMode(ImeMode mode);

    // Lent to CLangBarItemButton as a TrackPopupMenu owner — using the foreground
    // window would force a costly taskbar focus shuffle before the menu appears.
    HWND GetMsgWindow() const { return m_hwndMsg; }

    // Visible (but off-screen) HWND we own, suitable for SetForegroundWindow
    // + TrackPopupMenu. The candidate window's hwnd works because it's a real
    // WS_POPUP that the same thread created — unlike HWND_MESSAGE children
    // which can't take foreground focus.
    HWND GetPopupOwnerHwnd() const;

private:
    ~CTextService();

    HRESULT InitKeyEventSink();
    void    UninitKeyEventSink();
    HRESULT InitPreservedKeys();
    void    UninitPreservedKeys();
    HRESULT SetIMEStateCompartments(BOOL enable);
    HRESULT InitDisplayAttributeGuidAtom();
    HRESULT InitLangBarItem();
    void    UninitLangBarItem();
    HRESULT InitCompartmentSinks();
    void    UninitCompartmentSinks();
    void    SyncImeStateFromCompartments();
    void    SetImeOpenClose(BOOL on);
    bool    ShouldEat(WPARAM wParam) const;
    void    TryOllamaConvertAsync(ITfContext* pContext);
    void    HandleOllamaDone(PendingOllamaRequest* pending);
    HRESULT InitMessageWindow();
    void    UninitMessageWindow();
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void    ShowLangBarMenu(int x, int y);
    void    ApplyCandidateSelection(ITfContext* pContext);
    // Returns true iff the helper actually auto-committed a converted composition.
    // Callers use the return value to decide whether the FOLLOWING keystroke's
    // edit session must start a fresh composition (StartAndUpdate) rather than
    // trust `m_pComposition == nullptr` alone — some hosts defer our sync
    // EndCommit to the async fallback path, so the pointer can still be non-null
    // when we return here.
    bool    CommitConvertedIfAny(ITfContext* pContext);
    // F5 idle-state "codepoint ⇔ character" in-place toggle. Reads
    // m_lastCommittedText and either replaces its trailing 4-6 hex chars
    // with the character they encode (hex → char direction) or replaces
    // its trailing single-char / surrogate-pair with its U+xxxx hex form
    // (char → hex direction). Both directions edit the document in place
    // via a sync edit session, so a bare F5 on「任」just gets you「4EFB」
    // (no composition, no Enter needed), and a second F5 flips back.
    // Returns true iff the replacement actually landed.
    bool    ToggleCodepointInPlace(ITfContext* pContext);
    // Ctrl+F5 handler. Appends the current attempt state to
    // %APPDATA%\GenerativeIME\misconversions.log as a UTF-8 block —
    // timestamp, buffer, reading, last displayed candidates, last
    // committed text, recent LLM context. Silently no-ops on I/O
    // failure (a full-disk or permission problem should never break
    // typing). Intended for the developer / power user grepping the
    // file later to spot patterns that need dictionary or ranking
    // improvements.
    void    LogMisconversionAttempt();
    POINT   QueryCandidateAnchorPos(ITfContext* pContext);
    POINT   QueryBunsetsuAnchorPos(ITfContext* pContext, size_t offset, size_t length);
    HRESULT RequestEditSession(ITfContext* pContext, EditAction action, const std::wstring& text);

    LONG                m_cRef;
    ITfThreadMgr*       m_pThreadMgr;
    TfClientId          m_tfClientId;
    std::wstring        m_romajiBuffer;
    ITfComposition*     m_pComposition;
    CLangBarItemButton* m_pLangBarItem;
    BOOL                m_isImeOn;
    BOOL                m_compositionConverted; // true once Ollama replaced the romaji-derived text
    ITfCompartment*     m_pCompOpenClose;
    ITfCompartment*     m_pCompConvMode;
    DWORD               m_dwCookieOpenClose;
    DWORD               m_dwCookieConvMode;
    HWND                m_hwndMsg;      // hidden HWND_MESSAGE window for worker-to-IME PostMessage
    CCandidateWindow*   m_pCandWnd;     // popup listing Ollama candidates; null until Create succeeds
    LearningStore*      m_pLearning;    // persisted reading-to-favorite map
    std::wstring        m_lastReading;  // reading that produced the current candidate list, for Record() on commit
    // F6-F10 conversions overwrite the composition range with a fixed form
    // (hiragana / katakana / ascii) instead of going through the candidate
    // window. We stash the resulting text here so VK_RETURN can commit it
    // and feed it to LearningStore the same way a candidate pick would.
    // Cleared on commit / cancel / fresh composition.
    std::wstring        m_fkeyConvertedText;
    ImeMode             m_imeMode;      // shaping mode applied to composition display

    // Rolling buffer of recently committed text, for feeding context to the
    // LLM. ~kRecentContextMax chars; older chars drop off the front so we
    // never grow unbounded across a long editing session.
    static constexpr size_t kRecentContextMax = 60;
    std::wstring        m_recentContext;
    void                AppendCommittedText(const std::wstring& text);

    // The most recent single commit, verbatim. F4 (with the IME on and no
    // composition open) re-inserts it, so symbol/emoji spam like ‼️‼️‼️
    // doesn't need a full convert round-trip per repeat.
    std::wstring        m_lastCommittedText;

    // Reading that produced m_lastCommittedText. m_lastReading gets cleared
    // aggressively during commit + prediction refresh, so it's empty by
    // the time Ctrl+Shift+F5 fires for a misconvert log. This mirror
    // survives across those clears specifically for the "record + forget
    // the learning entry" path — LogMisconversionAttempt hands it to
    // LearningStore::ForgetReading so the wrong pick that just went to
    // disk stops overriding the dictionary head next time.
    std::wstring        m_lastCommittedReading;

    // Per-clause readings from the last committed bunsetsu conversion.
    // Empty for single-candidate commits (m_lastCommittedReading covers
    // those). Populated in Phase B: each entry is one bunsetsu's reading
    // in commit order. Ctrl+Shift+F5 iterates over these on top of the
    // joined m_lastCommittedReading so per-clause learnings — the
    // ones bunsetsu commits Record individually — are also purged. Without
    // this, forgetting「ついかしたじっそう」leaves the per-clause
    //「ついかした→付いかした」 and 「じっそう→下実装」 entries behind
    // and the wrong split resurfaces the next time.
    std::vector<std::wstring> m_lastCommittedClauseReadings;

    // Monotonically increasing counter bumped whenever we start a SKK
    // lookup. The reorder worker stamps the active counter into its request;
    // by the time it returns, if the counter has moved (the user typed more,
    // committed, switched apps, etc.) we drop the stale result.
    unsigned            m_reorderSeq = 0;
    // Fire-and-forget reorder of the just-shown candidate list against
    // m_recentContext. Caller has already updated the candidate window with
    // `candidates` in their default (SKK) order.
    void                StartReorderAsync(ITfContext* pContext,
                                          const std::wstring& reading,
                                          const std::vector<std::wstring>& candidates);
    void                HandleOllamaReorderDone(PendingOllamaReorderRequest* pending);

    // Fire-and-forget supplementary Ollama lookup when MeCab's split looks
    // dubious (see bunsetsu::LooksSuspect). Caller has already shown the
    // MeCab result; when Ollama returns, its candidates get prepended to the
    // candidate window so the user sees the saner LLM suggestion above the
    // literal UniDic answer. Same staleness check as the reorder path —
    // shares m_reorderSeq.
    void                StartMecabSupplementAsync(ITfContext* pContext,
                                                  const std::wstring& reading,
                                                  const std::wstring& mecabTop);
    void                HandleOllamaFallbackDone(PendingOllamaFallbackRequest* pending);

    // Fire-and-forget LLM acronym expansion. When an all-uppercase alnum
    // composition ("ＩＭＦ") has no built-in AcronymExpansions entry, we ask
    // Ollama for the meaning and append its answers to the width/case
    // candidate list already on screen. Shares m_reorderSeq for staleness.
    // `base` is the candidate list currently shown, so the done handler can
    // append behind it without racing to re-derive it.
    void                StartAcronymExpandAsync(ITfContext* pContext,
                                                const std::wstring& acronym,
                                                const std::wstring& display,
                                                const std::vector<std::wstring>& base);
    void                HandleAcronymDone(PendingAcronymRequest* pending);

    // Pre-load the Ollama model on Activate so the first real user query
    // doesn't pay a 90-second cold-load. Fire-and-forget — we discard the
    // result; only the side effect of leaving the model resident matters.
    void                StartOllamaWarmupAsync();

    // Phase B state: when MeCab splits the input into 2+ bunsetsu, we let
    // the user step between them with Tab/Shift+Tab and cycle each one's
    // candidates independently with Space/↓/↑. Empty vector means we're
    // in single-bunsetsu mode (SKK whole hit, single-morpheme MeCab, or
    // pre-conversion romaji buffer) and the candidate window behaves as
    // before — its selection is what gets committed.
    std::vector<Bunsetsu> m_bunsetsuList;
    size_t                m_focusedBunsetsu = 0;
    // True iff we're in Phase B mode (m_bunsetsuList non-empty AND the
    // candidate window is showing the focused bunsetsu's candidates).
    bool                  InBunsetsuMode() const { return !m_bunsetsuList.empty(); }
    // Hand off the MeCab split to Phase B state and start showing the
    // first bunsetsu's candidates. Called from TryOllamaConvertAsync.
    void                  EnterBunsetsuMode(std::vector<Bunsetsu> parts,
                                            ITfContext* pContext);
    // Clear Phase B state and the candidate window. Composition is left
    // alone — callers (EndCommit, EndCancel) handle that.
    void                  LeaveBunsetsuMode();
    // Re-render composition based on JoinSelected and point the candidate
    // window at the focused bunsetsu's candidate set. Called whenever a
    // Tab moves focus or a candidate is picked in Phase B mode.
    void                  RepaintBunsetsu(ITfContext* pContext);
    // Mirror the candidate window's selection index into the focused
    // bunsetsu, clamped to that bunsetsu's own candidate count. The window
    // can be showing a longer list than the bunsetsu owns (async Ollama
    // results landing mid-Phase-B), and an unclamped index would send
    // JoinSelected reading past the candidates vector into raw heap.
    void                  SyncFocusedBunsetsuSelection();
    // Grow (delta=+1) or shrink (delta=-1) the focused bunsetsu by one
    // character, redistributing across the boundary with the next
    // bunsetsu. Regenerates candidates for the affected bunsetsu via
    // bunsetsu::MakeBunsetsuFromReading and repaints.
    void                  ResizeFocusedBunsetsu(int delta, ITfContext* pContext);

    // 投機的変換 (speculative conversion): while the user is still typing,
    // SkkDictionary::PredictCompletions runs against the kana typed so far
    // and the candidate window shows completed words (こんに → 今日は).
    // m_predictionActive marks that the visible candidate list holds these
    // predictions (NOT Space-conversion results) so key routing can treat
    // Space as "convert what I typed" instead of "cycle candidates".
    // m_predictionReadings runs parallel to the shown candidates and holds
    // each prediction's full dictionary reading, so picking one records
    // (full reading → word) in LearningStore rather than the typed prefix.
    bool                      m_predictionActive = false;
    std::vector<std::wstring> m_predictionReadings;
    // Refresh (or dismiss) the prediction popup from the current romaji
    // buffer. Called after every buffer-mutating keystroke; also clears
    // m_lastReading because whatever candidate list that reading produced
    // is stale once the buffer changes.
    void                      UpdatePrediction(ITfContext* pContext);
    // Move the prediction selection by `delta` and mirror the pick into
    // the composition. The first navigation adopts the highlighted top
    // entry as-is (delta ignored) so ↓/Tab enters the list before
    // advancing past it; delta=0 re-applies the current highlight (used
    // after PageNext/PagePrev moved it).
    void                      NavigatePrediction(int delta, ITfContext* pContext);

    // ドキュメント文脈バイアス (MeCab版): on composition start we read the
    // text surrounding the caret (TF_ES_READ), run MeCab over it, and keep
    // a volatile reading→surface map of the kanji / katakana words already
    // present in the document. Conversion prefers these right below the
    // learning fav; prediction surfaces them above generic SKK completions.
    // NEVER persisted: document text is not the user's own conversion
    // history — writing it into LearningStore would pollute real learning
    // with vocabulary from documents the user merely opened.
    std::unordered_map<std::wstring, std::wstring> m_docVocab;
    ULONGLONG                                      m_docVocabTick = 0;
    void ScanDocumentVocab(ITfContext* pContext);
    // Caret-window document slice captured by ScanDocumentVocab, with an
    // 〔入力位置〕 marker where the conversion result will land. Feeds the
    // Ollama reorder / fallback prompts so the model sees the actual
    // discourse instead of only the last 60 committed chars.
    std::wstring m_docContext;
    // Context string for Ollama prompts: the document slice when available,
    // else the rolling m_recentContext commit buffer (some hosts return an
    // empty TF_ES_READ slice).
    std::wstring BuildLlmContext() const;

    // 変換 key when no composition is live and the host has a selection:
    // grab the selected text, recover its hiragana reading via MeCab's
    // pronunciation field, and start a fresh composition + conversion
    // against that reading. Final commit replaces the original selection.
    void                  TryReconvertFromSelection(ITfContext* pContext);
    // 無変換 key while composing: cycle the romaji buffer's rendering
    // through hiragana → 全角カタカナ → 半角カタカナ → ローマ字 →
    // (back to hiragana). Same end states as F6-F10 but with one key.
    void                  CycleNonconvertForm(ITfContext* pContext);

    // Index into the cycle above. Reset on commit / cancel / new composition.
    int                   m_nonconvertCycle = 0;
};
