#include "candidatewindow.h"
#include "globals.h"
#include <algorithm>

namespace
{
    constexpr wchar_t kWndClass[] = L"GenerativeIME_CandWnd_v2"; // bump on class style change
    constexpr int kPaddingX  = 12;
    constexpr int kPaddingY  = 6;
    constexpr int kMinWidth  = 160;
    constexpr int kMaxRows   = 9;
    constexpr int kCornerRadius = 6;

    // MS-IME-ish palette tuned for Win11 light theme.
    constexpr COLORREF kBgColor       = RGB(255, 255, 255);
    constexpr COLORREF kBorderColor   = RGB(200, 200, 200);
    constexpr COLORREF kSelBgColor    = RGB(0, 120, 215);
    constexpr COLORREF kSelTextColor  = RGB(255, 255, 255);
    constexpr COLORREF kTextColor     = RGB(30, 30, 30);
    constexpr COLORREF kIndexColor    = RGB(140, 140, 140);
    constexpr COLORREF kSelIndexColor = RGB(200, 220, 240);
    constexpr COLORREF kSepColor      = RGB(238, 238, 238);
}

CCandidateWindow::CCandidateWindow()
    : m_hwnd(nullptr)
    , m_font(nullptr)
    , m_rowHeight(20)
    , m_width(kMinWidth)
    , m_selected(0)
    , m_pageStart(0)
{}

CCandidateWindow::~CCandidateWindow()
{
    Destroy();
}

HRESULT CCandidateWindow::Create()
{
    if (m_hwnd) return S_OK;

    WNDCLASSEXW wc = { sizeof(wc) };
    // CS_DROPSHADOW: free OS-rendered soft shadow under the popup, matches
    // tooltip / context-menu styling so the candidate list doesn't feel like
    // a raw Win32 surface floating on top of the editor.
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = kWndClass;
    wc.hbrBackground = nullptr; // we paint everything
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
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

    // Yu Gothic UI 14pt — readable Japanese, available on Win10+ out of the box.
    m_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Yu Gothic UI");
    return S_OK;
}

void CCandidateWindow::ApplyRoundedRegion()
{
    if (!m_hwnd) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, kCornerRadius * 2, kCornerRadius * 2);
    SetWindowRgn(m_hwnd, rgn, TRUE); // window takes ownership; do not DeleteObject(rgn).
}

void CCandidateWindow::Destroy()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
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

    HDC hdc = GetDC(m_hwnd);
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    m_rowHeight = tm.tmHeight + 4;

    int widest = kMinWidth;
    for (size_t i = 0; i < m_candidates.size(); ++i)
    {
        // Prefix " N. " (or "    " when index > 9) so we can render index hints later.
        std::wstring line = std::to_wstring(i + 1) + L". " + m_candidates[i];
        SIZE sz;
        GetTextExtentPoint32W(hdc, line.c_str(), (int)line.size(), &sz);
        if (sz.cx > widest) widest = sz.cx;
    }
    m_width = widest + kPaddingX * 2;

    SelectObject(hdc, oldFont);
    ReleaseDC(m_hwnd, hdc);

    int rows = (int)(std::min)((size_t)kMaxRows, m_candidates.size());
    if (rows < 1) rows = 1;
    int height = rows * m_rowHeight + kPaddingY * 2;
    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyRoundedRegion();
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
    m_selected = (m_selected + 1) % (int)m_candidates.size();
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
    m_selected = (m_selected - 1 + (int)m_candidates.size()) % (int)m_candidates.size();
    if (m_selected < m_pageStart || m_selected >= m_pageStart + kMaxRows)
    {
        m_pageStart = (m_selected / kMaxRows) * kMaxRows;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CCandidateWindow::SelectIndex(int index)
{
    if (index < 0 || (size_t)index >= m_candidates.size()) return;
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
    if (nextStart >= (int)m_candidates.size()) nextStart = 0; // wrap
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
        int total = (int)m_candidates.size();
        prevStart = ((total - 1) / kMaxRows) * kMaxRows;
    }
    m_pageStart = prevStart;
    m_selected = m_pageStart;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
}

std::wstring CCandidateWindow::GetSelected() const
{
    if (m_selected < 0 || (size_t)m_selected >= m_candidates.size()) return L"";
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
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // handled in WM_PAINT
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // never take focus
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CCandidateWindow::Paint(HDC hdc)
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Background fill + thin border. Rounded corners come from the window
    // region set in ApplyRoundedRegion; here we just paint inside that mask.
    HBRUSH bg = CreateSolidBrush(kBgColor);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, kBorderColor);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, kCornerRadius * 2, kCornerRadius * 2);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetBkMode(hdc, TRANSPARENT);

    int y = kPaddingY;
    int total = (int)m_candidates.size();
    int rowsThisPage = (std::min)(kMaxRows, total - m_pageStart);
    for (int row = 0; row < rowsThisPage; ++row)
    {
        int globalIdx = m_pageStart + row;
        RECT full = { 1, y, rc.right - 1, y + m_rowHeight };
        bool selected = (globalIdx == m_selected);

        if (selected)
        {
            HBRUSH hi = CreateSolidBrush(kSelBgColor);
            FillRect(hdc, &full, hi);
            DeleteObject(hi);
        }

        // Index keeps its on-screen position (1-9), not the absolute index,
        // so number keys map naturally to the visible rows regardless of page.
        std::wstring index = std::to_wstring(row + 1) + L".";
        RECT idxRect = { kPaddingX, y, kPaddingX + 22, y + m_rowHeight };
        SetTextColor(hdc, selected ? kSelIndexColor : kIndexColor);
        DrawTextW(hdc, index.c_str(), (int)index.size(), &idxRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        RECT textRect = { kPaddingX + 26, y, rc.right - kPaddingX, y + m_rowHeight };
        SetTextColor(hdc, selected ? kSelTextColor : kTextColor);
        DrawTextW(hdc, m_candidates[globalIdx].c_str(), (int)m_candidates[globalIdx].size(), &textRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        if (row + 1 < rowsThisPage && !selected && (globalIdx + 1) != m_selected)
        {
            HPEN sep = CreatePen(PS_SOLID, 1, kSepColor);
            HPEN op = (HPEN)SelectObject(hdc, sep);
            MoveToEx(hdc, kPaddingX, y + m_rowHeight - 1, nullptr);
            LineTo(hdc, rc.right - kPaddingX, y + m_rowHeight - 1);
            SelectObject(hdc, op);
            DeleteObject(sep);
        }

        y += m_rowHeight;
    }

    SelectObject(hdc, oldFont);
}
