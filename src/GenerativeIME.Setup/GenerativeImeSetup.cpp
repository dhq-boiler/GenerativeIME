// GenerativeImeSetup.exe — per-user TSF seed launcher invoked by the MSI as a
// deferred/Impersonate=yes CustomAction (case A in WDAC feedback round 4).
//
// Windows Vista+ exposes InstallLayoutOrTip / SetDefaultLayoutOrTip in input.dll.
// Calling them here (impersonating the installing user) makes Windows itself
// write HKCU\Software\Microsoft\CTF\Assemblies / Preload / Substitutes and keep
// the TSF broker cache (ctfmon) in sync. Plain reg-writes do not do that.
//
// Reference: mozc's ImeUtil::SetDefault(), Microsoft Learn "InstallLayoutOrTip".

#include <windows.h>

typedef BOOL (WINAPI *PFN_InstallLayoutOrTip)(LPCWSTR psz, DWORD dwFlags);
typedef BOOL (WINAPI *PFN_SetDefaultLayoutOrTip)(LPCWSTR psz, DWORD dwFlags);

// Format: 0x<LANGID>:{CLSID}{ProfileGUID}
static const wchar_t kProfile[] =
    L"0x0411:{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}{F267F064-7917-4631-BB73-567C314F43BE}";

int wmain()
{
    HMODULE hInput = LoadLibraryW(L"input.dll");
    if (!hInput) return 0; // Non-fatal: MSI CustomAction has Return="ignore".

    auto pInstall = reinterpret_cast<PFN_InstallLayoutOrTip>(
        GetProcAddress(hInput, "InstallLayoutOrTip"));
    auto pDefault = reinterpret_cast<PFN_SetDefaultLayoutOrTip>(
        GetProcAddress(hInput, "SetDefaultLayoutOrTip"));

    if (pInstall) pInstall(kProfile, 0);
    if (pDefault) pDefault(kProfile, 0);

    FreeLibrary(hInput);
    return 0;
}
