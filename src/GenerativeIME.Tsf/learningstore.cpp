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

std::wstring LearningStore::BlacklistPath() const
{
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        return L"";
    std::wstring dir = appData;
    CoTaskMemFree(appData);
    dir += L"\\GenerativeIME";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\blacklist.txt";
}

std::wstring LearningStore::BoundaryBlacklistPath() const
{
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        return L"";
    std::wstring dir = appData;
    CoTaskMemFree(appData);
    dir += L"\\GenerativeIME";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\boundary_blacklist.txt";
}

namespace
{
    std::wstring JoinBoundary(const std::vector<size_t>& endOffsets)
    {
        std::wstring s;
        for (size_t i = 0; i < endOffsets.size(); ++i)
        {
            if (i) s.push_back(L',');
            s += std::to_wstring(endOffsets[i]);
        }
        return s;
    }
}

HRESULT LearningStore::Load()
{
    m_lastPicked.clear();
    m_blacklist.clear();

    std::wstring path = StorePath();
    if (!path.empty())
    {
        std::ifstream f(path, std::ios::binary);
        if (f.is_open())
        {
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
                    m_lastPicked[reading] = picked;
                }
            }
        }
    }

    // Blacklist is loaded independently of the main store; missing /
    // unreadable blacklist file is fine (first run / never opted out).
    std::wstring blPath = BlacklistPath();
    if (!blPath.empty())
    {
        std::ifstream f(blPath, std::ios::binary);
        if (f.is_open())
        {
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                std::wstring reading = FromUtf8(line.substr(0, tab));
                std::wstring word    = FromUtf8(line.substr(tab + 1));
                if (!reading.empty() && !word.empty())
                {
                    m_blacklist[reading].insert(word);
                }
            }
        }
    }

    // Boundary blacklist: same format (reading\tjoined), but the second
    // column is a comma-joined list of end-offset integers.
    m_boundaryBlacklist.clear();
    std::wstring bbPath = BoundaryBlacklistPath();
    if (!bbPath.empty())
    {
        std::ifstream f(bbPath, std::ios::binary);
        if (f.is_open())
        {
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                std::wstring reading = FromUtf8(line.substr(0, tab));
                std::wstring joined  = FromUtf8(line.substr(tab + 1));
                if (!reading.empty() && !joined.empty())
                {
                    m_boundaryBlacklist[reading].insert(joined);
                }
            }
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

HRESULT LearningStore::Blacklist(const std::wstring& reading, const std::wstring& word)
{
    if (reading.empty() || word.empty()) return E_INVALIDARG;

    auto& set = m_blacklist[reading];
    if (!set.insert(word).second) return S_OK;  // already blacklisted

    std::wstring path = BlacklistPath();
    if (path.empty()) return E_FAIL;
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f.is_open()) return E_FAIL;
    f << ToUtf8(reading) << '\t' << ToUtf8(word) << '\n';
    return S_OK;
}

HRESULT LearningStore::BlacklistBoundary(const std::wstring& reading,
                                         const std::vector<size_t>& endOffsets)
{
    if (reading.empty()) return E_INVALIDARG;

    std::wstring joined = JoinBoundary(endOffsets);
    auto& set = m_boundaryBlacklist[reading];
    if (!set.insert(joined).second) return S_OK;  // already

    std::wstring path = BoundaryBlacklistPath();
    if (path.empty()) return E_FAIL;
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f.is_open()) return E_FAIL;
    f << ToUtf8(reading) << '\t' << ToUtf8(joined) << '\n';
    return S_OK;
}

bool LearningStore::IsBoundaryBlacklisted(const std::wstring& reading,
                                          const std::vector<size_t>& endOffsets) const
{
    auto it = m_boundaryBlacklist.find(reading);
    if (it == m_boundaryBlacklist.end()) return false;
    return it->second.count(JoinBoundary(endOffsets)) > 0;
}

std::wstring LearningStore::GetFav(const std::wstring& reading) const
{
    auto it = m_lastPicked.find(reading);
    if (it == m_lastPicked.end()) return {};

    // Honor the blacklist: a fav that was later opted out shouldn't
    // resurface via the fav-fast-path.
    auto blIt = m_blacklist.find(reading);
    if (blIt != m_blacklist.end() && blIt->second.count(it->second) > 0)
        return {};

    return it->second;
}

std::vector<std::wstring> LearningStore::Reorder(
    const std::wstring& reading,
    const std::vector<std::wstring>& candidates) const
{
    if (candidates.empty()) return candidates;

    // Blacklist filter first. If the reading has no blacklist entry,
    // the original list passes through unchanged. If filtering would
    // drop EVERY candidate, fall back to the original — better to show
    // a blacklisted choice than nothing.
    std::vector<std::wstring> filtered;
    auto blIt = m_blacklist.find(reading);
    if (blIt == m_blacklist.end())
    {
        filtered = candidates;
    }
    else
    {
        filtered.reserve(candidates.size());
        for (const auto& c : candidates)
        {
            if (blIt->second.count(c) == 0) filtered.push_back(c);
        }
        if (filtered.empty()) filtered = candidates;
    }

    auto it = m_lastPicked.find(reading);
    if (it == m_lastPicked.end()) return filtered;

    const std::wstring& fav = it->second;
    std::vector<std::wstring> out;
    out.reserve(filtered.size() + 1);

    bool found = false;
    for (const auto& c : filtered)
    {
        if (!found && c == fav) { found = true; continue; }
        out.push_back(c);
    }
    if (!found)
    {
        // Favorite isn't in the (post-blacklist) candidate set — either a
        // newer analyzer produced a different top candidate, or the user
        // blacklisted the previous favorite. Returning a "ghost" fav
        // would force a stale pick back to the top forever; better to
        // let the live candidates speak for themselves.
        return filtered;
    }
    out.insert(out.begin(), fav);
    return out;
}
