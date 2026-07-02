#include "learningstore.h"
#include <windows.h>
#include <shlobj.h>
#include <psapi.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")

// -- AppContext -----------------------------------------------------------

AppContext AppContext::Capture(HWND hwnd)
{
    AppContext c;
    if (!hwnd) hwnd = GetForegroundWindow();
    if (!hwnd) return c;

    wchar_t cls[256] = { 0 };
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0) c.windowClass = cls;

    wchar_t title[512] = { 0 };
    if (GetWindowTextW(hwnd, title, ARRAYSIZE(title)) > 0)
    {
        std::wstring t = title;
        // De-tab / de-newline / de-RS so storage stays parseable.
        for (auto& ch : t) {
            if (ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'\x1E')
                ch = L' ';
        }
        // Trim trailing whitespace.
        while (!t.empty() && iswspace(t.back())) t.pop_back();
        // Trim leading whitespace.
        size_t start = 0;
        while (start < t.size() && iswspace(t[start])) ++start;
        if (start) t.erase(0, start);
        // Truncate: 60 wchar_t is enough to distinguish e.g. Chrome tabs
        // ("ChatGPT — foo bar" vs "GitLab · project · main") without
        // storing per-file title variations (line numbers, dirty flags).
        if (t.size() > 60) t.resize(60);
        c.titleNorm = std::move(t);
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid)
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc)
        {
            wchar_t path[MAX_PATH] = { 0 };
            DWORD sz = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, path, &sz))
            {
                std::wstring p(path, sz);
                auto slash = p.find_last_of(L"\\/");
                c.procName = (slash != std::wstring::npos) ? p.substr(slash + 1) : p;
            }
            CloseHandle(hProc);
        }
    }
    return c;
}

namespace
{
    // Serialize an AppContext + reading into a lookup key. Uses RS (0x1E)
    // as the separator since real titles / class names / process names
    // never contain it (we sanitize titles at capture time).
    std::wstring MakeCtxKey(const std::wstring& proc,
                            const std::wstring& cls,
                            const std::wstring& title,
                            const std::wstring& reading)
    {
        std::wstring k;
        k.reserve(proc.size() + cls.size() + title.size() + reading.size() + 4);
        k += proc;    k.push_back(L'\x1E');
        k += cls;     k.push_back(L'\x1E');
        k += title;   k.push_back(L'\x1E');
        k += reading;
        return k;
    }
}

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
    m_ctxPicked.clear();
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
                // Split by tab. Format is either:
                //   reading\tpicked                            (legacy)
                //   reading\tpicked\tproc\tclass\ttitleNorm    (2026-07-02+)
                // Anything else (0 or 3-4 tabs) is a corrupt row; skip.
                std::vector<std::string> parts;
                {
                    size_t start = 0;
                    while (true) {
                        auto pos = line.find('\t', start);
                        if (pos == std::string::npos) {
                            parts.push_back(line.substr(start));
                            break;
                        }
                        parts.push_back(line.substr(start, pos - start));
                        start = pos + 1;
                    }
                }
                if (parts.size() != 2 && parts.size() != 5) continue;

                std::wstring reading = FromUtf8(parts[0]);
                std::wstring picked  = FromUtf8(parts[1]);
                if (reading.empty() || picked.empty()) continue;

                if (parts.size() == 2)
                {
                    // Legacy row → global map.
                    m_lastPicked[reading] = picked;
                }
                else // parts.size() == 5
                {
                    std::wstring proc  = FromUtf8(parts[2]);
                    std::wstring cls   = FromUtf8(parts[3]);
                    std::wstring title = FromUtf8(parts[4]);
                    if (proc.empty() && cls.empty() && title.empty())
                    {
                        // 5-field with empty ctx: an explicit global record.
                        m_lastPicked[reading] = picked;
                    }
                    else
                    {
                        // Scoped record — does NOT touch the global map.
                        // Cross-context leakage was the whole reason for
                        // this rework; keeping the two maps disjoint is
                        // what makes GetFav's cascade correct.
                        m_ctxPicked[MakeCtxKey(proc, cls, title, reading)] = picked;
                    }
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

HRESULT LearningStore::Record(const std::wstring& reading, const std::wstring& picked,
                               const AppContext& ctx)
{
    if (reading.empty() || picked.empty()) return E_INVALIDARG;
    // Populate every scope-level entry so GetFav's narrower→broader cascade
    // has data to hit at each stop. Storing only at the exact (proc, cls,
    // title) key meant the (proc, cls) and (proc) lookups always missed
    // and cascade fell straight through to a never-populated m_lastPicked.
    // Concrete effect: reopening the same app under a different window
    // title lost every learning. Now the newest commit "seeds" all
    // broader scopes too, but the narrower entry always wins the cascade
    // so specific contexts still override.
    if (ctx.Empty())
    {
        m_lastPicked[reading] = picked;
    }
    else
    {
        m_ctxPicked[MakeCtxKey(ctx.procName, ctx.windowClass, ctx.titleNorm, reading)] = picked;
        m_ctxPicked[MakeCtxKey(ctx.procName, ctx.windowClass, L"",              reading)] = picked;
        m_ctxPicked[MakeCtxKey(ctx.procName, L"",              L"",              reading)] = picked;
        m_lastPicked[reading] = picked;
    }

    std::wstring path = StorePath();
    if (path.empty()) return E_FAIL;

    // Append-only: fast, crash-tolerant. Compaction (dedupe by reading) is
    // a future enhancement when the file gets unwieldy.
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f.is_open()) return E_FAIL;
    // 5-field format even for empty ctx — always the same column count
    // simplifies grepping / awking, and a Load() picks up either.
    f << ToUtf8(reading)  << '\t'
      << ToUtf8(picked)   << '\t'
      << ToUtf8(ctx.procName)    << '\t'
      << ToUtf8(ctx.windowClass) << '\t'
      << ToUtf8(ctx.titleNorm)   << '\n';
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

std::wstring LearningStore::GetFav(const std::wstring& reading, const AppContext& ctx) const
{
    // Cascade: full (proc, class, title) → (proc, class) → proc → global.
    // Each narrower scope wins if it has a match; falls back to the
    // wider scope otherwise. Blacklist is applied at every stage —
    // opting out a fav in one context shouldn't resurface it because
    // a broader-scope entry happened to survive.
    auto blHit = [&](const std::wstring& picked) -> bool {
        auto blIt = m_blacklist.find(reading);
        return blIt != m_blacklist.end() && blIt->second.count(picked) > 0;
    };
    auto tryLookup = [&](const std::wstring& proc,
                         const std::wstring& cls,
                         const std::wstring& title) -> std::wstring {
        if (proc.empty() && cls.empty() && title.empty()) return {};
        auto it = m_ctxPicked.find(MakeCtxKey(proc, cls, title, reading));
        if (it == m_ctxPicked.end()) return {};
        if (blHit(it->second)) return {};
        return it->second;
    };

    if (!ctx.Empty())
    {
        if (auto r = tryLookup(ctx.procName, ctx.windowClass, ctx.titleNorm); !r.empty())
            return r;
        if (auto r = tryLookup(ctx.procName, ctx.windowClass, L""); !r.empty())
            return r;
        if (auto r = tryLookup(ctx.procName, L"", L""); !r.empty())
            return r;
    }

    auto it = m_lastPicked.find(reading);
    if (it == m_lastPicked.end()) return {};
    if (blHit(it->second)) return {};
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
