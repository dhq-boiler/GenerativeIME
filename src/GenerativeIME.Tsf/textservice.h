#pragma once

#include "globals.h"
#include "bunsetsu.h"
#include <msctf.h>
#include <string>
#include <vector>

enum class EditAction;
class CLangBarItemButton;
class CCandidateWindow;
class LearningStore;
struct PendingOllamaRequest;
struct PendingOllamaReorderRequest;
struct PendingOllamaFallbackRequest;

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
    void    CommitConvertedIfAny(ITfContext* pContext);
    POINT   QueryCandidateAnchorPos(ITfContext* pContext);
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
    ImeMode             m_imeMode;      // shaping mode applied to composition display

    // Rolling buffer of recently committed text, for feeding context to the
    // LLM. ~kRecentContextMax chars; older chars drop off the front so we
    // never grow unbounded across a long editing session.
    static constexpr size_t kRecentContextMax = 60;
    std::wstring        m_recentContext;
    void                AppendCommittedText(const std::wstring& text);

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
};
