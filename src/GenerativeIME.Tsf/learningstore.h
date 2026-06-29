#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Tiny in-process learning store: remembers the user's last-picked candidate
// for each reading, persisted as a plain UTF-8 tab-separated file under
// %APPDATA%\GenerativeIME\learning.txt. No frequency counts, no LRU eviction
// yet — newest wins.
class LearningStore
{
public:
    LearningStore() = default;

    // Reads the store from disk. Safe to call multiple times; later calls
    // refresh from disk (useful if another process / instance edited it).
    HRESULT Load();

    // Records (reading -> picked) and appends to disk immediately so a
    // crash before the next entry doesn't lose the choice.
    HRESULT Record(const std::wstring& reading, const std::wstring& picked);

    // Reorder `candidates` so the most-recently-picked one for `reading`
    // (if present) is at index 0; blacklisted entries get dropped
    // entirely. If the blacklist would empty the list the original is
    // returned unchanged (better to surface something than nothing).
    std::vector<std::wstring> Reorder(const std::wstring& reading,
                                      const std::vector<std::wstring>& candidates) const;

    // Direct fav lookup — returns the most-recently-picked text for
    // `reading`, or empty if none. Lets callers short-circuit the whole
    // SKK/MeCab/Ollama pipeline when the user has already taught us
    // what they want for this exact reading.
    std::wstring GetFav(const std::wstring& reading) const;

    // Opt-out a candidate so future Reorder() drops it from the result.
    // Persisted to a blacklist file alongside learning.txt; survives
    // restart. Idempotent — re-blacklisting the same pair is a no-op.
    HRESULT Blacklist(const std::wstring& reading, const std::wstring& word);

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

    std::unordered_map<std::wstring, std::wstring> m_lastPicked;
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_blacklist;
    // Key: composite reading. Value: set of forbidden end-offset arrays,
    // each serialized as a comma-joined wstring ("4,10") for fast hash
    // lookup without writing a custom hasher for vector<size_t>.
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_boundaryBlacklist;
};
