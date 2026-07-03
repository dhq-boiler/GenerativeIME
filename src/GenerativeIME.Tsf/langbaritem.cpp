#include "langbaritem.h"
#include "textservice.h"
#include "resource.h"
#include <oleauto.h>
#include <olectl.h>
#include <strsafe.h>
#include <shellapi.h>
#include <string>
#include <thread>

#pragma comment(lib, "shell32.lib")

// GUID_LBI_INPUTMODE is declared (without an `extern` body) only in some
// private MS headers; the public Windows SDK ships no header that exposes it.
// However the symbol IS exported by uuid.lib (dumpbin /symbols shows it under
// SECT53). Forward-declare it here so we can pass it to TF_LANGBARITEMINFO;
// the linker resolves it against uuid.lib (already in our deps).
extern "C" const GUID GUID_LBI_INPUTMODE;

namespace
{
    constexpr DWORD kSinkCookie = 0x0FAB0FAB;
    constexpr wchar_t kButtonDesc[]  = L"GenerativeIME Mode";

    const wchar_t* LabelForMode(ImeMode mode)
    {
        switch (mode)
        {
        case ImeMode::Hiragana:     return L"あ";
        case ImeMode::FullKatakana: return L"ア";
        case ImeMode::HalfKatakana: return L"ｱ";
        case ImeMode::FullAlnum:    return L"Ａ";
        case ImeMode::Off:
        default:                    return L"A";
        }
    }

    // Launch the WPF user-dictionary manager. It ships next to our DLL
    // (build post-build / MSI payload), so resolve the path from our own
    // module rather than assuming an install location.
    void LaunchDictManager()
    {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(g_hInst, path, MAX_PATH) == 0) return;
        std::wstring dir(path);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);
        std::wstring exe = dir + L"\\GenerativeIME.DictManager.exe";
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    }
}

CLangBarItemButton::CLangBarItemButton(CTextService* pService)
    : m_cRef(1)
    , m_pSink(nullptr)
    , m_dwSinkCookie(TF_INVALID_COOKIE)
    , m_pService(pService)
{
    InterlockedIncrement(&g_cRefDll);

    m_info.clsidService = c_clsidGenerativeImeTextService;
    // GUID_LBI_INPUTMODE is the magic identifier the Win11 Input Indicator
    // looks for to render the "あ/A" mode pill on the left side of the IME
    // branding icon. Pair with TF_LBI_STYLE_SHOWNINTRAY so the shell knows
    // we belong in the tray cluster.
    m_info.guidItem     = GUID_LBI_INPUTMODE;
    // BTN_BUTTON: we draw our own popup menu from OnClick via TrackPopupMenu;
    // Win11's Input Indicator ignores BTN_MENU's auto-menu route anyway.
    m_info.dwStyle      = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    m_info.ulSort       = 0;
    StringCchCopyW(m_info.szDescription, ARRAYSIZE(m_info.szDescription), kButtonDesc);
}

CLangBarItemButton::~CLangBarItemButton()
{
    if (m_pSink) { m_pSink->Release(); m_pSink = nullptr; }
    InterlockedDecrement(&g_cRefDll);
}

STDMETHODIMP CLangBarItemButton::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = static_cast<ITfSource*>(this);
    }
    if (*ppvObj) { AddRef(); return S_OK; }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CLangBarItemButton::AddRef()  { return InterlockedIncrement(&m_cRef); }
STDMETHODIMP_(ULONG) CLangBarItemButton::Release()
{
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarItemButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    *pInfo = m_info;
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::Show(BOOL /*fShow*/)
{
    return E_NOTIMPL;
}

STDMETHODIMP CLangBarItemButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(kButtonDesc);
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
}

// Menu IDs used by InitMenu/OnMenuSelect. Order chosen to match MS-IME's
// own input-mode menu top-to-bottom.
enum : UINT
{
    kMenuHiragana = 100,
    kMenuKatakana = 101,
    kMenuHalfKatakana = 102,
    kMenuFullAlnum = 103,
    kMenuHalfAlnum = 104,
    kMenuUserDict = 110,   // "ユーザー辞書…" — launches the WPF manager
};

STDMETHODIMP CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, const RECT* prcArea)
{
    // Right click toggles immediately. Left click opens the mode menu.
    if (click == TF_LBI_CLK_RIGHT)
    {
        if (m_pService) m_pService->ToggleImeFromUI();
        return S_OK;
    }

    int x = pt.x;
    int y = pt.y;
    if (prcArea) { x = prcArea->left; y = prcArea->bottom; }

    // Snapshot what the worker thread needs so it can render without touching
    // shared mutable state. Read imeOn from the service (single source of
    // truth) — caching it on the LangBar would drift from CTextService.
    CTextService* service = m_pService;
    BOOL imeOn = service ? service->IsImeOn() : FALSE;

    // Run the menu on its own dedicated thread that creates its OWN top-level
    // WS_POPUP. This dodges the UWP-host case (Notepad on Win11) where
    // SetForegroundWindow on an existing same-thread window is silently
    // refused — a brand-new top-level window owned by a fresh thread can
    // take foreground. TrackPopupMenu blocks on its modal loop in that
    // worker thread; IME thread returns immediately.
    std::thread([service, imeOn, x, y]()
    {
        HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC", L"", WS_POPUP,
            -100, -100, 1, 1,
            nullptr, nullptr, g_hInst, nullptr);
        if (!owner) return;
        ShowWindow(owner, SW_SHOWNA);
        SetForegroundWindow(owner);

        HMENU menu = CreatePopupMenu();
        if (menu)
        {
            UINT hi = imeOn ? MF_CHECKED : 0;
            UINT lo = imeOn ? 0 : MF_CHECKED;
            AppendMenuW(menu, MF_STRING | hi, kMenuHiragana,     L"ひらがな");
            AppendMenuW(menu, MF_STRING,      kMenuKatakana,     L"全角カタカナ");
            AppendMenuW(menu, MF_STRING,      kMenuHalfKatakana, L"半角カタカナ");
            AppendMenuW(menu, MF_STRING,      kMenuFullAlnum,    L"全角英数");
            AppendMenuW(menu, MF_STRING | lo, kMenuHalfAlnum,    L"半角英数");
            AppendMenuW(menu, MF_SEPARATOR,   0,                 nullptr);
            AppendMenuW(menu, MF_STRING,      kMenuUserDict,     L"ユーザー辞書…");

            UINT cmd = (UINT)TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                x, y, 0, owner, nullptr);
            DestroyMenu(menu);

            {
                wchar_t buf[160];
                swprintf_s(buf, L"[GenerativeIME] menu cmd=%u imeOn=%d service=%p\n",
                           cmd, (int)imeOn, service);
                OutputDebugStringW(buf);
            }

            if (cmd == kMenuUserDict)
            {
                // Not a mode change: open the user-dictionary manager. No IME
                // state to touch, so this is independent of `service`.
                LaunchDictManager();
            }
            else if (service && cmd != 0)
            {
                // Translate menu id -> ImeMode wParam for the IME thread.
                // 0=Off, 1=Hiragana, 2=FullKatakana, 3=HalfKatakana, 4=FullAlnum.
                int modeId = 1;
                switch (cmd)
                {
                case kMenuHiragana:     modeId = 1; break;
                case kMenuKatakana:     modeId = 2; break;
                case kMenuHalfKatakana: modeId = 3; break;
                case kMenuFullAlnum:    modeId = 4; break;
                case kMenuHalfAlnum:    modeId = 0; break;
                }
                {
                    wchar_t buf[128];
                    swprintf_s(buf, L"[GenerativeIME] menu->modeId=%d post msgWnd=%p\n",
                               modeId, service->GetMsgWindow());
                    OutputDebugStringW(buf);
                }
                HWND msgWnd = service->GetMsgWindow();
                if (msgWnd) PostMessageW(msgWnd, WM_USER + 3, (WPARAM)modeId, 0);
            }
        }
        DestroyWindow(owner);
    }).detach();

    return S_OK;
}

STDMETHODIMP CLangBarItemButton::InitMenu(ITfMenu* pMenu)
{
    if (!pMenu) return E_INVALIDARG;

    // Mirror MS-IME's tray menu. CHECKED flag marks the current mode so the
    // user sees what's active. For Phase 1 only Hiragana <-> Half-alnum
    // actually wire into IME state; the katakana / full-alnum items are
    // stubs that will gain effect when a per-composition mode is added.
    auto add = [&](UINT id, const wchar_t* label, bool checked) {
        DWORD flags = 0;
        if (checked) flags |= TF_LBMENUF_CHECKED;
        pMenu->AddMenuItem(id, flags, nullptr, nullptr,
                           const_cast<wchar_t*>(label),
                           (ULONG)wcslen(label), nullptr);
    };

    const BOOL imeOn = m_pService ? m_pService->IsImeOn() : FALSE;
    add(kMenuHiragana,     L"ひらがな",         imeOn);
    add(kMenuKatakana,     L"全角カタカナ",     false);
    add(kMenuHalfKatakana, L"半角カタカナ",     false);
    add(kMenuFullAlnum,    L"全角英数",         false);
    add(kMenuHalfAlnum,    L"半角英数",         !imeOn);
    add(kMenuUserDict,     L"ユーザー辞書…",    false);
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::OnMenuSelect(UINT wID)
{
    if (!m_pService) return S_OK;
    const BOOL imeOn = m_pService->IsImeOn();
    switch (wID)
    {
    case kMenuHiragana:
    case kMenuKatakana:
    case kMenuHalfKatakana:
    case kMenuFullAlnum:
        // All "Japanese-input" rows: ensure IME is ON. Per-mode behavior
        // (katakana etc.) requires a conversion-mode compartment write
        // we haven't wired yet.
        if (!imeOn) m_pService->ToggleImeFromUI();
        break;
    case kMenuHalfAlnum:
        if (imeOn) m_pService->ToggleImeFromUI();
        break;
    case kMenuUserDict:
        LaunchDictManager();
        break;
    }
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    // Choose icon by current mode. Off uses the gray "A". Other modes have
    // their own glyph icons (あ / ア / ｱ / Ａ). The Input Indicator caches
    // GetIcon per OnUpdate notification, so SetImeMode -> sink->OnUpdate
    // is what makes the tray pill actually repaint when the mode flips.
    UINT res = IDI_GENIME_A;
    if (m_pService && m_pService->IsImeOn())
    {
        switch (m_pService->GetImeMode())
        {
        case ImeMode::Hiragana:     res = IDI_GENIME;          break;
        case ImeMode::FullKatakana: res = IDI_GENIME_KATA;     break;
        case ImeMode::HalfKatakana: res = IDI_GENIME_HALFKATA; break;
        case ImeMode::FullAlnum:    res = IDI_GENIME_FALNUM;   break;
        case ImeMode::Off:
        default:                    res = IDI_GENIME_A;        break;
        }
    }
    *phIcon = static_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(res),
                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    return *phIcon ? S_OK : E_FAIL;
}

STDMETHODIMP CLangBarItemButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    ImeMode mode = ImeMode::Off;
    if (m_pService && m_pService->IsImeOn()) mode = m_pService->GetImeMode();
    *pbstrText = SysAllocString(LabelForMode(mode));
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
}

void CLangBarItemButton::UpdateMode()
{
    if (m_pSink) m_pSink->OnUpdate(TF_LBI_TEXT | TF_LBI_ICON);
}

STDMETHODIMP CLangBarItemButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(IID_ITfLangBarItemSink, riid)) return CONNECT_E_CANNOTCONNECT;
    if (m_pSink) return CONNECT_E_ADVISELIMIT;
    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&m_pSink)))
    {
        m_pSink = nullptr;
        return E_NOINTERFACE;
    }
    m_dwSinkCookie = kSinkCookie;
    if (pdwCookie) *pdwCookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarItemButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != kSinkCookie || !m_pSink) return CONNECT_E_NOCONNECTION;
    m_pSink->Release();
    m_pSink = nullptr;
    m_dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}
