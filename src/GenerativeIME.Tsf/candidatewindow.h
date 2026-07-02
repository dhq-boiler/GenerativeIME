#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

// Top-level popup that lists Ollama-returned conversion candidates and tracks
// a selection cursor. Owned by CTextService; created once per Activate and
// destroyed on Deactivate. The window never takes focus (WS_EX_NOACTIVATE),
// so the host app and IME composition behave as if it isn't there.
//
// Rendering is Direct2D + DirectWrite (not GDI) so emoji candidates from
// SKK-JISYO.emoji draw in color — GDI's DrawText can only produce the
// monochrome fallback glyphs.
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
    void Paint();
    void Resize();
    void ApplyRoundedRegion();

    // Lazily (re)creates the HwndRenderTarget + brush. Device resources die
    // with the render target (D2DERR_RECREATE_TARGET), so Paint calls this
    // every time and DiscardRenderTarget on failure.
    HRESULT EnsureRenderTarget();
    void    DiscardRenderTarget();
    // One line of `text` into `rect`, vertically centered, left-aligned,
    // clipped, with color-font (emoji) glyph runs enabled.
    void    DrawLine(const std::wstring& text, const D2D1_RECT_F& rect, COLORREF color);
    // Pixel width of one rendered line (measured via IDWriteTextLayout so
    // emoji fallback runs measure correctly).
    float   MeasureLineWidth(const std::wstring& text);

    HWND        m_hwnd;
    int         m_rowHeight;
    int         m_width;
    std::vector<std::wstring> m_candidates;
    int         m_selected;
    int         m_pageStart; // index of first candidate currently rendered
    bool        m_ollamaPending = false;
    int         m_spinnerFrame = 0;  // bumped on every WM_TIMER tick

    // Device-independent (live for the window's whole life).
    ID2D1Factory*          m_pD2DFactory = nullptr;
    IDWriteFactory*        m_pDWriteFactory = nullptr;
    IDWriteTextFormat*     m_pTextFormat = nullptr;
    // Device-dependent (recreated after D2DERR_RECREATE_TARGET).
    ID2D1HwndRenderTarget* m_pRT = nullptr;
    ID2D1SolidColorBrush*  m_pBrush = nullptr;
};
