#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// App context captured at commit time: which process + top-level window the
// user was typing into. Learnings get scoped by this so「かんじ→感じ」
// picked in a chat window doesn't override「かんじ→漢字」 in a code editor.
// An empty AppContext (all fields blank) means "no context" — legacy 2-field
// learning.txt entries load as this, and it's also what commits without a
// foreground window (rare, e.g. login shell) fall back to.
struct AppContext
{
    std::wstring procName;    // basename of executable, e.g. "Code.exe"
    std::wstring windowClass; // GetClassName of the top window
    std::wstring titleNorm;   // GetWindowText, trimmed / de-tabbed / truncated

    bool Empty() const {
        return procName.empty() && windowClass.empty() && titleNorm.empty();
    }

    // Captures the current foreground window. If hwnd is nullptr uses
    // GetForegroundWindow(). Best-effort — returns a partially-populated
    // AppContext (or all-empty) if a Win32 call fails, never throws.
    static AppContext Capture(HWND hwnd = nullptr);
};

// Tiny in-process learning store: remembers the user's last-picked candidate
// for each reading, persisted as a plain UTF-8 tab-separated file under
// %APPDATA%\GenerativeIME\learning.txt. No frequency counts, no LRU eviction
// yet — newest wins. Since 2026-07-02 each entry is scoped by AppContext,
// with a cascade fallback so the user still sees learnings across contexts
// when no closer match exists.
class LearningStore
{
public:
    LearningStore() = default;

    // Reads the store from disk. Safe to call multiple times; later calls
    // refresh from disk (useful if another process / instance edited it).
    HRESULT Load();

    // Records (reading -> picked) scoped to `ctx` and appends to disk
    // immediately so a crash before the next entry doesn't lose the
    // choice. Passing an empty ctx records as global (equivalent to the
    // pre-2026-07-02 behavior).
    HRESULT Record(const std::wstring& reading, const std::wstring& picked,
                   const AppContext& ctx);
    // Legacy overload — same as Record(..., AppContext{}).
    HRESULT Record(const std::wstring& reading, const std::wstring& picked) {
        return Record(reading, picked, AppContext{});
    }

    // Reorder `candidates` so the most-recently-picked one for `reading`
    // (if present) is at index 0; blacklisted entries get dropped
    // entirely. If the blacklist would empty the list the original is
    // returned unchanged (better to surface something than nothing).
    std::vector<std::wstring> Reorder(const std::wstring& reading,
                                      const std::vector<std::wstring>& candidates) const;

    // Direct fav lookup — returns the most-recently-picked text for
    // `reading` under `ctx`, or empty if none. Cascade: full ctx match
    // > (proc, class) > proc > global. Lets callers short-circuit the
    // whole SKK/MeCab/Ollama pipeline when the user has already taught
    // us what they want for this exact reading in this exact context.
    std::wstring GetFav(const std::wstring& reading, const AppContext& ctx) const;
    // Legacy overload — global (empty ctx) lookup.
    std::wstring GetFav(const std::wstring& reading) const {
        return GetFav(reading, AppContext{});
    }

    // Opt-out a candidate so future Reorder() drops it from the result.
    // Persisted to a blacklist file alongside learning.txt; survives
    // restart. Idempotent — re-blacklisting the same pair is a no-op.
    HRESULT Blacklist(const std::wstring& reading, const std::wstring& word);

    // Wipe every previously-recorded pick for `reading` from BOTH the
    // in-memory maps and the on-disk learning.txt. Used by the Ctrl+Shift+
    // F5 misconvert log: when the user flags a bad commit we want the
    // wrong pick to stop overriding the dictionary head. Empty `reading`
    // is a no-op. Rewrites learning.txt to drop matching lines (streaming
    // read + atomic replace) — the file is normally small so the compact
    // is cheap; on I/O failure the in-memory clear still stands and the
    // next process restart will pick up the leftover disk lines, which
    // callers can either accept or retry the compact for.
    HRESULT ForgetReading(const std::wstring& reading);

    // Negative bunsetsu-boundary learning. When the user opts out of a
    // bunsetsu partition (Shift+Delete in Phase B), the offending
    // boundary array — END character indices of every bunsetsu except
    // the last — is recorded as forbidden for that composite reading.
    // Next time TryOllamaConvertAsync sees SplitMecab produce the same
    // boundaries, it skips Phase B entirely and lets Ollama propose a
    // saner split.
    HRESULT BlacklistBoundary(const std::wstring& reading,
                              const std::vector<size_t>& endOffsets);
    bool IsBoundaryBlacklisted(const std::wstring& reading,
                               const std::vector<size_t>& endOffsets) const;

private:
    std::wstring StorePath() const;
    std::wstring BlacklistPath() const;
    std::wstring BoundaryBlacklistPath() const;

    // Global (context-less) fav map. Populated by legacy 2-field
    // learning.txt entries AND by empty-ctx Record() calls. Serves as
    // the outermost cascade fallback in GetFav.
    std::unordered_map<std::wstring, std::wstring> m_lastPicked;

    // Context-scoped fav map. Key encodes (procName | windowClass |
    // titleNorm | reading) as a `\x1E`-separated wstring (RS control
    // char, never appears in real titles). Value is the picked text.
    // Newest write wins on the same key.
    std::unordered_map<std::wstring, std::wstring> m_ctxPicked;

    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_blacklist;
    // Key: composite reading. Value: set of forbidden end-offset arrays,
    // each serialized as a comma-joined wstring ("4,10") for fast hash
    // lookup without writing a custom hasher for vector<size_t>.
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_boundaryBlacklist;
};
