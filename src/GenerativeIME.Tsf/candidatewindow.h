#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Top-level popup that lists Ollama-returned conversion candidates and tracks
// a selection cursor. Owned by CTextService; created once per Activate and
// destroyed on Deactivate. The window never takes focus (WS_EX_NOACTIVATE),
// so the host app and IME composition behave as if it isn't there.
class CCandidateWindow
{
public:
    CCandidateWindow();
    ~CCandidateWindow();

    HRESULT Create();
    void    Destroy();
    HWND    GetHwnd() const { return m_hwnd; }

    void SetCandidates(const std::vector<std::wstring>& candidates);
    void ShowAt(POINT screenPos);
    void Hide();
    bool IsVisible() const;

    void   SelectNext();
    void   SelectPrev();
    void   SelectIndex(int index);
    void   PageNext();
    void   PagePrev();
    // Index of the selected candidate among the items currently rendered on
    // screen (0-based). Used to map number keys to candidate offsets.
    int    GetSelectedDisplayOffset() const { return m_selected - m_pageStart; }
    int    GetPageStart() const { return m_pageStart; }
    size_t Count() const { return m_candidates.size(); }
    int    GetSelectedIndex() const { return m_selected; }
    std::wstring GetSelected() const;
    const std::vector<std::wstring>& GetCandidates() const { return m_candidates; }

    // Render a small spinner ("⠋⠙⠹…" / dot wave) along the bottom edge
    // while we're waiting for an Ollama fallback response. Caller flips
    // this to true when an async LLM request starts and back to false
    // when the response lands (or times out). A WM_TIMER drives the
    // animation; we leave it stopped when nothing is pending.
    void SetOllamaPending(bool pending);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint(HDC hdc);
    void Resize();
    void ApplyRoundedRegion();

    HWND        m_hwnd;
    HFONT       m_font;
    int         m_rowHeight;
    int         m_width;
    std::vector<std::wstring> m_candidates;
    int         m_selected;
    int         m_pageStart; // index of first candidate currently rendered
    bool        m_ollamaPending = false;
    int         m_spinnerFrame = 0;  // bumped on every WM_TIMER tick
};
