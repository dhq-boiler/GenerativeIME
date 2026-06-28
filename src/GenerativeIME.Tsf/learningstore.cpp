#include "learningstore.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")

namespace
{
    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring FromUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }
}

std::wstring LearningStore::StorePath() const
{
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        return L"";
    std::wstring dir = appData;
    CoTaskMemFree(appData);
    dir += L"\\GenerativeIME";
    // CreateDirectoryW on an existing path returns FALSE w/ ERROR_ALREADY_EXISTS;
    // we ignore both that and ERROR_PATH_NOT_FOUND for the parent (rare on AppData).
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\learning.txt";
}

HRESULT LearningStore::Load()
{
    m_lastPicked.clear();
    std::wstring path = StorePath();
    if (path.empty()) return E_FAIL;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return S_FALSE; // first run is fine

    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::wstring reading = FromUtf8(line.substr(0, tab));
        std::wstring picked  = FromUtf8(line.substr(tab + 1));
        if (!reading.empty() && !picked.empty())
        {
            m_lastPicked[reading] = picked; // later entries win — natural since we append
        }
    }
    return S_OK;
}

HRESULT LearningStore::Record(const std::wstring& reading, const std::wstring& picked)
{
    if (reading.empty() || picked.empty()) return E_INVALIDARG;
    m_lastPicked[reading] = picked;

    std::wstring path = StorePath();
    if (path.empty()) return E_FAIL;

    // Append-only: fast, crash-tolerant. Compaction (dedupe by reading) is
    // a future enhancement when the file gets unwieldy.
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f.is_open()) return E_FAIL;
    f << ToUtf8(reading) << '\t' << ToUtf8(picked) << '\n';
    return S_OK;
}

std::vector<std::wstring> LearningStore::Reorder(
    const std::wstring& reading,
    const std::vector<std::wstring>& candidates) const
{
    auto it = m_lastPicked.find(reading);
    if (it == m_lastPicked.end() || candidates.empty()) return candidates;

    const std::wstring& fav = it->second;
    std::vector<std::wstring> out;
    out.reserve(candidates.size() + 1);

    bool found = false;
    for (const auto& c : candidates)
    {
        if (!found && c == fav) { found = true; continue; }
        out.push_back(c);
    }
    if (found)
    {
        out.insert(out.begin(), fav);
    }
    else
    {
        // Favorite wasn't in this candidate set; prepend it anyway so the
        // user still gets their previous choice without re-typing.
        out.insert(out.begin(), fav);
    }
    return out;
}
