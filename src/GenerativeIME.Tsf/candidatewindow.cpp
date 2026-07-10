#include "candidatewindow.h"
#include "emojitext.h"
#include "globals.h"
#include <algorithm>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace
{
    constexpr wchar_t kWndClass[] = L"GenerativeIME_CandWnd_v2"; // bump on class style change
    constexpr int kPaddingX = 12;
    constexpr int kPaddingY = 6;
    constexpr int kMinWidth = 160;
    constexpr int kMaxRows = 9;
    constexpr int kCornerRadius = 6;

    constexpr float kFontSize = 18.0f; // DIP == px (RT pinned to 96 DPI)
    constexpr wchar_t kFontName[] = L"Yu Gothic UI"; // readable Japanese, Win10+ stock

    // Right-edge type annotation for emoji rows. Small + gray so it reads
    // as metadata, not as part of the candidate.
    constexpr float kAnnotFontSize = 11.0f;
    constexpr wchar_t kEmojiAnnot[] = L"(emoji)";
    constexpr int kAnnotGap = 10; // min space between candidate and annotation

    // MS-IME-ish palette tuned for Win11 light theme.
    constexpr COLORREF kBgColor = RGB(255, 255, 255);
    constexpr COLORREF kBorderColor = RGB(200, 200, 200);
    constexpr COLORREF kSelBgColor = RGB(0, 120, 215);
    constexpr COLORREF kSelTextColor = RGB(255, 255, 255);
    constexpr COLORREF kTextColor = RGB(30, 30, 30);
    constexpr COLORREF kIndexColor = RGB(140, 140, 140);
    constexpr COLORREF kSelIndexColor = RGB(200, 220, 240);
    constexpr COLORREF kSepColor = RGB(238, 238, 238);

    D2D1_COLOR_F ToColorF(COLORREF c)
    {
        return D2D1::ColorF(GetRValue(c) / 255.0f,
                            GetGValue(c) / 255.0f,
                            GetBValue(c) / 255.0f);
    }
}

CCandidateWindow::CCandidateWindow()
    : m_hwnd(nullptr)
      , m_rowHeight(20)
      , m_width(kMinWidth)
      , m_selected(0)
      , m_pageStart(0)
{
}

CCandidateWindow::~CCandidateWindow()
{
    Destroy();
}

HRESULT CCandidateWindow::Create()
{
    if (m_hwnd) return S_OK;

    WNDCLASSEXW wc = {sizeof(wc)};
    // CS_DROPSHADOW: free OS-rendered soft shadow under the popup, matches
    // tooltip / context-menu styling so the candidate list doesn't feel like
    // a raw Win32 surface floating on top of the editor.
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kWndClass;
    wc.hbrBackground = nullptr; // we paint everything
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc); // ignore re-register error

    // WS_EX_NOACTIVATE prevents stealing focus from the host text field; without
    // it the composition would terminate the moment we showed the candidate list.
    // WS_EX_TOOLWINDOW keeps it out of the taskbar.
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                             kWndClass, nullptr,
                             WS_POPUP,
                             0, 0, m_width, m_rowHeight,
                             nullptr, nullptr, g_hInst, this);
    if (!m_hwnd) return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    if (SUCCEEDED(hr))
    {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
    }
    if (SUCCEEDED(hr))
    {
        hr = m_pDWriteFactory->CreateTextFormat(kFontName, nullptr,
                                                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"ja-jp", &m_pTextFormat);
    }
    if (SUCCEEDED(hr))
    {
        hr = m_pDWriteFactory->CreateTextFormat(kFontName, nullptr,
                                                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, kAnnotFontSize, L"ja-jp", &m_pAnnotFormat);
    }
    if (SUCCEEDED(hr))
    {
        m_pAnnotFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_pAnnotFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // One candidate per row; DWrite vertically centers within the row
        // rect, and NO_WRAP + our clip rect handles overlong candidates.
        m_pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Row height from a sample that exercises both the Japanese font
        // and the emoji fallback, so emoji rows don't clip. (GDI版 used
        // TEXTMETRIC.tmHeight + 4; this is the DWrite equivalent.)
        IDWriteTextLayout* probe = nullptr;
        if (SUCCEEDED(m_pDWriteFactory->CreateTextLayout(L"あAg😀", 5, m_pTextFormat,
            10000.0f, 100.0f, &probe)))
        {
            DWRITE_TEXT_METRICS tm{};
            probe->GetMetrics(&tm);
            m_rowHeight = static_cast<int>(tm.height + 0.5f) + 4;
            probe->Release();
        }
    }
    return hr;
}

void CCandidateWindow::ApplyRoundedRegion()
{
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, kCornerRadius * 2, kCornerRadius * 2);
    SetWindowRgn(m_hwnd, rgn, TRUE); // window takes ownership; do not DeleteObject(rgn).
}

void CCandidateWindow::Destroy()
{
    DiscardRenderTarget();
    if (m_pAnnotFormat)
    {
        m_pAnnotFormat->Release();
        m_pAnnotFormat = nullptr;
    }
    if (m_pTextFormat)
    {
        m_pTextFormat->Release();
        m_pTextFormat = nullptr;
    }
    if (m_pDWriteFactory)
    {
        m_pDWriteFactory->Release();
        m_pDWriteFactory = nullptr;
    }
    if (m_pD2DFactory)
    {
        m_pD2DFactory->Release();
        m_pD2DFactory = nullptr;
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

HRESULT CCandidateWindow::EnsureRenderTarget()
{
    if (m_pRT) return S_OK;
    if (!m_pD2DFactory || !m_hwnd) return E_FAIL;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    // Pin the render target to 96 DPI so 1 DIP == 1 pixel and all the
    // pixel-based layout math (row height, paddings, SetWindowPos sizes)
    // keeps meaning what it says. Same fixed-pixel behavior as the old
    // GDI -18 font height.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(
            m_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
        &m_pRT);
    if (FAILED(hr)) return hr;

    hr = m_pRT->CreateSolidColorBrush(ToColorF(kTextColor), &m_pBrush);
    if (FAILED(hr))
    {
        DiscardRenderTarget();
        return hr;
    }
    return S_OK;
}

void CCandidateWindow::DiscardRenderTarget()
{
    if (m_pBrush)
    {
        m_pBrush->Release();
        m_pBrush = nullptr;
    }
    if (m_pRT)
    {
        m_pRT->Release();
        m_pRT = nullptr;
    }
}

float CCandidateWindow::MeasureLineWidth(const std::wstring& text)
{
    if (!m_pDWriteFactory || !m_pTextFormat || text.empty()) return 0.0f;
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = m_pDWriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                    m_pTextFormat, 100000.0f, 100.0f, &layout);
    if (FAILED(hr)) return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    layout->Release();
    return tm.widthIncludingTrailingWhitespace;
}

float CCandidateWindow::MeasureAnnotationWidth(const std::wstring& text)
{
    if (!m_pDWriteFactory || !m_pAnnotFormat || text.empty()) return 0.0f;
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = m_pDWriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                    m_pAnnotFormat, 100000.0f, 100.0f, &layout);
    if (FAILED(hr)) return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    layout->Release();
    return tm.widthIncludingTrailingWhitespace;
}

void CCandidateWindow::SetCandidates(const std::vector<std::wstring>& candidates)
{
    m_candidates = candidates;
    m_selected = 0;
    m_pageStart = 0;
    Resize();
    if (m_hwnd && IsWindowVisible(m_hwnd)) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void CCandidateWindow::Resize()
{
    if (!m_hwnd) return;

    int widest = kMinWidth;
    // Emoji rows carry a right-edge "(emoji)" tag; reserve its width so
    // the annotation never overlaps a long candidate.
    const int annotSpan = static_cast<int>(MeasureAnnotationWidth(kEmojiAnnot) + 0.5f) + kAnnotGap;
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        // Prefix " N. " (or "    " when index > 9) so we can render index hints later.
        std::wstring line = std::to_wstring(i + 1) + L". " + m_candidates[i];
        int w = static_cast<int>(MeasureLineWidth(line) + 0.5f);
        if (emojitext::IsEmoji(m_candidates[i])) w += annotSpan;
        if (w > widest) widest = w;
    }
    m_width = widest + kPaddingX * 2;

    int rows = static_cast<int>((std::min)((size_t)kMaxRows, m_candidates.size()));
    if (rows < 1) rows = 1;
    int height = rows * m_rowHeight + kPaddingY * 2;
    // Reserve a slim strip below the candidate list for the Ollama
    // spinner. The strip stays in the layout whether or not a request
    // is pending so the window doesn't jitter when SetOllamaPending
    // flips — keeping the height stable matters because Win11's drop
    // shadow re-snaps every resize.
    if (m_ollamaPending) height += m_rowHeight / 2 + 4;
    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyRoundedRegion();
    if (m_pRT) m_pRT->Resize(D2D1::SizeU(m_width, height));
}

void CCandidateWindow::ShowAt(POINT screenPos)
{
    if (!m_hwnd || m_candidates.empty()) return;
    SetWindowPos(m_hwnd, HWND_TOPMOST, screenPos.x, screenPos.y,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void CCandidateWindow::Hide()
{
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

bool CCandidateWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void CCandidateWindow::SelectNext()
{
    if (m_candidates.empty()) return;
    m_selected = (m_selected + 1) % static_cast<int>(m_candidates.size());
    // Auto-scroll: if we walked off the bottom of the visible window, move
    // the page start so the new selection lands on the first row of a fresh
    // page (or wraps to page 0 when we cycled back to candidate 0).
    if (m_selected < m_pageStart || m_selected >= m_pageStart + kMaxRows)
    {
        m_pageStart = (m_selected / kMaxRows) * kMaxRows;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CCandidateWindow::SelectPrev()
{
    if (m_candidates.empty()) return;
    m_selected = (m_selected - 1 + static_cast<int>(m_candidates.size())) % static_cast<int>(m_candidates.size());
    if (m_selected < m_pageStart || m_selected >= m_pageStart + kMaxRows)
    {
        m_pageStart = (m_selected / kMaxRows) * kMaxRows;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CCandidateWindow::SelectIndex(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_candidates.size()) return;
    m_selected = index;
    if (m_selected < m_pageStart || m_selected >= m_pageStart + kMaxRows)
    {
        m_pageStart = (m_selected / kMaxRows) * kMaxRows;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CCandidateWindow::PageNext()
{
    if (m_candidates.empty()) return;
    int nextStart = m_pageStart + kMaxRows;
    if (nextStart >= static_cast<int>(m_candidates.size())) nextStart = 0; // wrap
    m_pageStart = nextStart;
    m_selected = m_pageStart;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void CCandidateWindow::PagePrev()
{
    if (m_candidates.empty()) return;
    int prevStart = m_pageStart - kMaxRows;
    if (prevStart < 0)
    {
        // wrap to the last full or partial page
        int total = static_cast<int>(m_candidates.size());
        prevStart = ((total - 1) / kMaxRows) * kMaxRows;
    }
    m_pageStart = prevStart;
    m_selected = m_pageStart;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

std::wstring CCandidateWindow::GetSelected() const
{
    if (m_selected < 0 || static_cast<size_t>(m_selected) >= m_candidates.size()) return L"";
    return m_candidates[m_selected];
}

LRESULT CALLBACK CCandidateWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<CCandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CCandidateWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
        {
            // BeginPaint/EndPaint still bracket the D2D work — they validate
            // the dirty region so Windows stops sending WM_PAINT.
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            Paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_SIZE:
        if (m_pRT) m_pRT->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        return 0;
    case WM_ERASEBKGND:
        return 1; // handled in WM_PAINT
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // never take focus
    case WM_TIMER:
        if (wParam == 1 && m_ollamaPending)
        {
            ++m_spinnerFrame;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CCandidateWindow::SetOllamaPending(bool pending)
{
    if (m_ollamaPending == pending) return;
    m_ollamaPending = pending;
    if (!m_hwnd) return;
    if (pending)
    {
        // ~110 ms tick = smooth Braille-spinner cycle without burning
        // power. Timer ID 1 is reserved for this.
        SetTimer(m_hwnd, 1, 110, nullptr);
    }
    else
    {
        KillTimer(m_hwnd, 1);
    }
    Resize();
    if (IsWindowVisible(m_hwnd)) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CCandidateWindow::DrawLine(const std::wstring& text, const D2D1_RECT_F& rect, COLORREF color)
{
    if (text.empty()) return;
    m_pBrush->SetColor(ToColorF(color));
    // ENABLE_COLOR_FONT is the whole point of the D2D rewrite: emoji glyph
    // runs (Segoe UI Emoji fallback) come out in color instead of the
    // monochrome outlines GDI would draw.
    m_pRT->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_pTextFormat, rect, m_pBrush,
                     D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void CCandidateWindow::DrawAnnotation(const std::wstring& text, const D2D1_RECT_F& rect, COLORREF color)
{
    if (text.empty() || !m_pAnnotFormat) return;
    m_pBrush->SetColor(ToColorF(color));
    m_pRT->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), m_pAnnotFormat, rect, m_pBrush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void CCandidateWindow::Paint()
{
    if (FAILED(EnsureRenderTarget())) return;

    RECT rcPx;
    GetClientRect(m_hwnd, &rcPx);
    D2D1_RECT_F rc = D2D1::RectF(0.0f, 0.0f, static_cast<float>(rcPx.right), static_cast<float>(rcPx.bottom));

    m_pRT->BeginDraw();

    // Background fill + thin border. Rounded corners come from the window
    // region set in ApplyRoundedRegion; here we just paint inside that mask.
    m_pRT->Clear(ToColorF(kBgColor));
    D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(
        D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f),
        static_cast<float>(kCornerRadius), static_cast<float>(kCornerRadius));
    m_pBrush->SetColor(ToColorF(kBorderColor));
    m_pRT->DrawRoundedRectangle(frame, m_pBrush, 1.0f);

    int y = kPaddingY;
    int total = static_cast<int>(m_candidates.size());
    int rowsThisPage = (std::min)(kMaxRows, total - m_pageStart);
    for (int row = 0; row < rowsThisPage; ++row)
    {
        int globalIdx = m_pageStart + row;
        bool selected = (globalIdx == m_selected);

        if (selected)
        {
            m_pBrush->SetColor(ToColorF(kSelBgColor));
            m_pRT->FillRectangle(
                D2D1::RectF(1.0f, static_cast<float>(y), rc.right - 1.0f, static_cast<float>(y + m_rowHeight)),
                m_pBrush);
        }

        // Index keeps its on-screen position (1-9), not the absolute index,
        // so number keys map naturally to the visible rows regardless of page.
        std::wstring index = std::to_wstring(row + 1) + L".";
        DrawLine(index,
                 D2D1::RectF(static_cast<float>(kPaddingX), static_cast<float>(y),
                             static_cast<float>(kPaddingX + 22), static_cast<float>(y + m_rowHeight)),
                 selected ? kSelIndexColor : kIndexColor);

        DrawLine(m_candidates[globalIdx],
                 D2D1::RectF(static_cast<float>(kPaddingX + 26), static_cast<float>(y),
                             rc.right - kPaddingX, static_cast<float>(y + m_rowHeight)),
                 selected ? kSelTextColor : kTextColor);

        // Right-edge type tag so 16px glyphs are identifiable at a glance
        // (❕ vs ❗) and it's obvious the row commits an emoji character.
        if (emojitext::IsEmoji(m_candidates[globalIdx]))
        {
            float annotW = MeasureAnnotationWidth(kEmojiAnnot);
            DrawAnnotation(kEmojiAnnot,
                           D2D1::RectF(rc.right - kPaddingX - annotW, static_cast<float>(y),
                                       rc.right - kPaddingX, static_cast<float>(y + m_rowHeight)),
                           selected ? kSelIndexColor : kIndexColor);
        }

        if (row + 1 < rowsThisPage && !selected && (globalIdx + 1) != m_selected)
        {
            m_pBrush->SetColor(ToColorF(kSepColor));
            float sepY = static_cast<float>(y + m_rowHeight) - 0.5f;
            m_pRT->DrawLine(D2D1::Point2F(static_cast<float>(kPaddingX), sepY),
                            D2D1::Point2F(rc.right - kPaddingX, sepY),
                            m_pBrush, 1.0f);
        }

        y += m_rowHeight;
    }

    // Ollama-pending spinner along the bottom edge. Cycles through a set
    // of unicode wave-style glyphs so the user can see the LLM hasn't
    // forgotten about them while a fallback request is in flight.
    if (m_ollamaPending)
    {
        static const wchar_t* kFrames[] = {
            L"⠋ Ollama 思考中…", L"⠙ Ollama 思考中…", L"⠹ Ollama 思考中…",
            L"⠸ Ollama 思考中…", L"⠼ Ollama 思考中…", L"⠴ Ollama 思考中…",
            L"⠦ Ollama 思考中…", L"⠧ Ollama 思考中…", L"⠇ Ollama 思考中…",
            L"⠏ Ollama 思考中…",
        };
        constexpr int kFrameCount = sizeof(kFrames) / sizeof(kFrames[0]);
        const wchar_t* frame = kFrames[static_cast<unsigned>(m_spinnerFrame) % kFrameCount];

        DrawLine(frame,
                 D2D1::RectF(static_cast<float>(kPaddingX), static_cast<float>(y + 2),
                             rc.right - kPaddingX, rc.bottom - 2.0f),
                 kIndexColor);
    }

    HRESULT hr = m_pRT->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        // Device loss (driver reset, RDP session change, …): drop the
        // device resources and let the next WM_PAINT rebuild them.
        DiscardRenderTarget();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}
