#pragma once

#include <windows.h>
#include <unknwn.h>
#include <msctf.h>

extern HINSTANCE g_hInst;
extern volatile LONG g_cRefDll;

// TextService CLSID: {D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}
extern const CLSID c_clsidGenerativeImeTextService;

// Profile GUID: {F267F064-7917-4631-BB73-567C314F43BE}
extern const GUID  c_guidGenerativeImeProfile;

// Display attribute GUID for composition input text: {B4CD8585-EB3A-4971-A770-6EB7E071A0D3}
extern const GUID  c_guidDisplayAttributeInput;

// LangBar item GUID (the "あ" mode button): {098029FD-8E37-47EE-9252-0CF677A18C44}
extern const GUID  c_guidLangBarItemButton;

// Preserved-key GUIDs for the Japanese keyboard's mode-switch keys.
// Each preserved key needs a stable identifier so TSF can route the
// OnPreservedKey callback back to the right action.
extern const GUID  c_guidKeyKanji;       // VK_KANJI    (0x19) — toggle
extern const GUID  c_guidKeyImeOn;       // VK_OEM_AUTO (0xF3) — explicit ON
extern const GUID  c_guidKeyImeOff;      // VK_OEM_ENLW (0xF4) — explicit OFF

// Resolved TfGuidAtom for c_guidDisplayAttributeInput. Populated by CTextService::Activate
// via ITfCategoryMgr::RegisterGUID, consumed by edit sessions to stamp GUID_PROP_ATTRIBUTE.
extern TfGuidAtom  g_gaDisplayAttributeInput;

// Japanese (ja-JP)
constexpr LANGID c_langIdJapanese = 0x0411;

// Human-readable strings used for registration / language bar.
extern const wchar_t c_szTextServiceDesc[];
extern const wchar_t c_szInfoKeyPrefix[];
