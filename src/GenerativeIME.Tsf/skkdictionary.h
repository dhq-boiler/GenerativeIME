#pragma once

#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

// Reading -> candidate-words dictionary loaded from SKK-JISYO.L (UTF-8).
//
// The SKK dictionary already organizes entries as "ひらがな読み /漢字1/漢字2/..."
// — exactly the lookup direction an IME needs. We bypass any morphological
// analyzer and just read it as a hash map. Okuri-ari entries (readings ending
// in a single ASCII letter that marks the okurigana stem) are skipped because
// our composition layer doesn't pass okuri information through yet.
//
// Single global instance: the dictionary is ~6 MB of strings; allocating it
// per CTextService instance would balloon working set in shells that spin up
// multiple TSF instances (Win11 routinely does, see textservice.cpp comments).
class SkkDictionary
{
public:
    // Returns the process-wide instance, lazily loading it the first time.
    // Subsequent calls reuse the loaded dictionary. Safe to call from any
    // thread (load is mutex-protected).
    static SkkDictionary* GetGlobal();

    bool   IsLoaded() const { return m_loaded; }
    size_t EntryCount() const { return m_entries.size(); }

    // Returns the candidates for `reading`, or an empty vector if the reading
    // is not in the dictionary. Candidates are in dictionary order (SKK-JISYO
    // orders them roughly by frequency / preference).
    std::vector<std::wstring> Lookup(const std::wstring& reading) const;

    struct PrefixMatch
    {
        size_t                    length = 0;  // chars of `reading` consumed (0 = no match)
        std::vector<std::wstring> candidates;
    };

    // Finds the longest prefix of `reading.substr(start)` that exists in the
    // dictionary. Used by the bunsetsu (clause) splitter to break a long
    // hiragana run like "あしたはいやらしい" into "あした" + "は" + "いやらしい".
    PrefixMatch FindLongestPrefix(const std::wstring& reading, size_t start) const;

private:
    SkkDictionary() = default;

    // Parses a SKK-JISYO formatted file at `path`. UTF-8 encoded, LF or CRLF
    // line endings. Returns S_OK on success even if the file is malformed in
    // places — we skip unparseable lines rather than fail the whole load.
    HRESULT Load(const std::wstring& path);

    std::unordered_map<std::wstring, std::vector<std::wstring>> m_entries;
    bool m_loaded = false;
};
