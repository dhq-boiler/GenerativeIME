// INITGUID must be defined exactly once in the project before <ole2.h>/<msctf.h>
// to materialize the GUID_* extern constants (GUID_LBI_INPUTMODE,
// GUID_TFCAT_*, GUID_PROP_ATTRIBUTE, ...) into this translation unit's data.
// All other TUs see only the externs from the headers and link to these.
#define INITGUID
#include "globals.h"
#include <initguid.h>
#include <msctf.h>

HINSTANCE g_hInst = nullptr;
volatile LONG g_cRefDll = 0;

// {D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}
extern const CLSID c_clsidGenerativeImeTextService =
    {0xD256C881, 0x4B4F, 0x4B8E, {0xBB, 0xD6, 0xE4, 0x90, 0xBE, 0xDC, 0x85, 0xD9}};

// {F267F064-7917-4631-BB73-567C314F43BE}
extern const GUID c_guidGenerativeImeProfile =
    {0xF267F064, 0x7917, 0x4631, {0xBB, 0x73, 0x56, 0x7C, 0x31, 0x4F, 0x43, 0xBE}};

// {B4CD8585-EB3A-4971-A770-6EB7E071A0D3}
extern const GUID c_guidDisplayAttributeInput =
    {0xB4CD8585, 0xEB3A, 0x4971, {0xA7, 0x70, 0x6E, 0xB7, 0xE0, 0x71, 0xA0, 0xD3}};

TfGuidAtom g_gaDisplayAttributeInput = TF_INVALID_GUIDATOM;

// {5C0F8F4A-2D7E-4A1F-8C9B-3F2A6B4E1D5C}
extern const GUID c_guidDisplayAttributeBunsetsuFocus =
    {0x5C0F8F4A, 0x2D7E, 0x4A1F, {0x8C, 0x9B, 0x3F, 0x2A, 0x6B, 0x4E, 0x1D, 0x5C}};

TfGuidAtom g_gaDisplayAttributeBunsetsuFocus = TF_INVALID_GUIDATOM;

// {098029FD-8E37-47EE-9252-0CF677A18C44}
extern const GUID c_guidLangBarItemButton =
    {0x098029FD, 0x8E37, 0x47EE, {0x92, 0x52, 0x0C, 0xF6, 0x77, 0xA1, 0x8C, 0x44}};

// {5CDD5B94-71BD-444B-82C3-AE1EFF87E116}
extern const GUID c_guidKeyKanji =
    {0x5CDD5B94, 0x71BD, 0x444B, {0x82, 0xC3, 0xAE, 0x1E, 0xFF, 0x87, 0xE1, 0x16}};
// {3976F935-C7CB-406C-841A-B97B122E3CC6}
extern const GUID c_guidKeyImeOn =
    {0x3976F935, 0xC7CB, 0x406C, {0x84, 0x1A, 0xB9, 0x7B, 0x12, 0x2E, 0x3C, 0xC6}};
// {79050DAF-16D2-4214-9CE7-16A9C2E17B49}
extern const GUID c_guidKeyImeOff =
    {0x79050DAF, 0x16D2, 0x4214, {0x9C, 0xE7, 0x16, 0xA9, 0xC2, 0xE1, 0x7B, 0x49}};
// {A1B29E7C-3F4D-4E7A-9C21-4E5F62A81D9B}  Ctrl+F5 misconversion log
extern const GUID c_guidKeyDebugLog =
    {0xA1B29E7C, 0x3F4D, 0x4E7A, {0x9C, 0x21, 0x4E, 0x5F, 0x62, 0xA8, 0x1D, 0x9B}};

extern const wchar_t c_szTextServiceDesc[] = L"GenerativeIME";
extern const wchar_t c_szInfoKeyPrefix[] = L"CLSID\\";
