#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

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
    // (if present) is at index 0; rest preserve their original order.
    std::vector<std::wstring> Reorder(const std::wstring& reading,
                                      const std::vector<std::wstring>& candidates) const;

private:
    std::wstring StorePath() const;

    std::unordered_map<std::wstring, std::wstring> m_lastPicked;
};
