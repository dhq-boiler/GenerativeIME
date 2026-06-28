#include "skkdictionary.h"
#include "globals.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <fstream>
#include <mutex>

#pragma comment(lib, "Shlwapi.lib")

namespace
{
    std::wstring FromUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    // Path to the dictionary file. Resolved next to our DLL so the IME
    // works regardless of which user / app loaded us — AppData would force
    // a per-user copy and break system-wide deployment.
    std::wstring ResolveDictPath()
    {
        wchar_t dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInst, dllPath, MAX_PATH) == 0) return L"";
        PathRemoveFileSpecW(dllPath);
        std::wstring p = dllPath;
        p += L"\\SKK-JISYO.L.utf8";
        return p;
    }

    std::once_flag g_skkOnce;
    SkkDictionary* g_pSkk = nullptr;
}

SkkDictionary* SkkDictionary::GetGlobal()
{
    std::call_once(g_skkOnce, []()
    {
        g_pSkk = new SkkDictionary();
        std::wstring path = ResolveDictPath();
        HRESULT hr = g_pSkk->Load(path);
        wchar_t buf[512];
        swprintf_s(buf,
                   L"[GenerativeIME] SkkDictionary.Load hr=0x%08X count=%zu path=%s\n",
                   (unsigned)hr, g_pSkk->EntryCount(), path.c_str());
        OutputDebugStringW(buf);
    });
    return g_pSkk;
}

HRESULT SkkDictionary::Load(const std::wstring& path)
{
    m_entries.clear();
    m_loaded = false;
    if (path.empty()) return E_INVALIDARG;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    std::string raw;
    while (std::getline(f, raw))
    {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.empty() || raw[0] == ';') continue;       // comments / headers

        std::wstring line = FromUtf8(raw);

        // Layout: "<reading> /<cand1>/<cand2>/.../"
        size_t sp = line.find(L' ');
        if (sp == std::wstring::npos) continue;

        std::wstring reading = line.substr(0, sp);
        if (reading.empty()) continue;
        // Okuri-ari readings end in an ASCII letter (e.g. "おくr" for okuru/送る).
        // We can't render those without okuri information from the composition
        // layer, so drop them here rather than mishandle them downstream.
        wchar_t back = reading.back();
        if (back < 128 && ((back >= L'a' && back <= L'z') || (back >= L'A' && back <= L'Z')))
            continue;

        std::wstring rest = line.substr(sp + 1);
        if (rest.size() < 2 || rest.front() != L'/') continue;

        std::vector<std::wstring> cands;
        cands.reserve(4);
        size_t pos = 1;                                   // skip leading '/'
        while (pos < rest.size())
        {
            size_t next = rest.find(L'/', pos);
            if (next == std::wstring::npos) break;
            std::wstring cand = rest.substr(pos, next - pos);
            // Annotation: "<word>;<gloss>" — keep only the word.
            size_t semi = cand.find(L';');
            if (semi != std::wstring::npos) cand.resize(semi);
            if (!cand.empty()) cands.push_back(std::move(cand));
            pos = next + 1;
        }

        if (!cands.empty())
            m_entries.emplace(std::move(reading), std::move(cands));
    }

    m_loaded = true;
    return S_OK;
}

std::vector<std::wstring> SkkDictionary::Lookup(const std::wstring& reading) const
{
    auto it = m_entries.find(reading);
    if (it == m_entries.end()) return {};
    return it->second;
}
