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

    // A speculative-completion hit: `reading` is the full dictionary key
    // that starts with the user's typed prefix, `word` the candidate shown
    // in the prediction popup.
    struct Prediction
    {
        std::wstring reading;
        std::wstring word;
    };

    // Predictive completion (投機的変換): returns up to `maxResults` words
    // whose reading starts with — and is strictly longer than — `prefix`
    // (the exact reading is what Space conversion already covers). Nearer
    // completions (shorter readings) rank first. Only okuri-nashi direct
    // entries participate: okuri-ari flatten-through keys would surface
    // truncated stems like 送り出 (from 送り出s).
    std::vector<Prediction> PredictCompletions(const std::wstring& prefix,
                                               size_t maxResults) const;

    // Looks up an okuri-ari verb / adjective stem by the stripped reading
    // (the reading WITHOUT the trailing ASCII letter that SKK uses to mark
    // the inflection class — "ふr /振/触/降/" is keyed by "ふ"). Returns
    // the kanji-stem candidates. bunsetsu uses this to recover inflection-
    // form alternates that SKK only stores at the stem level, so an input
    // like "ふる" can offer 振る / 触る / 降る even though SKK's "ふる"
    // okuri-nashi entry only carries "古".
    std::vector<std::wstring> LookupOkuri(const std::wstring& stemReading) const;

    // True iff `reading` was seen as an okuri-NASHI entry in SKK-JISYO.
    // Not merely present in m_entries (which also includes okuri-ari
    // flatten-through) — the reading must have appeared as an explicit
    // entry the dict maintainer wrote. Callers use this to decide
    // whether the ReadsAs filter should apply: for direct entries the
    // filter must be bypassed (SKK-explicit greetings like
    // 「こんにちわ /今日は/」 don't pass MeCab's pronunciation check),
    // for okuri-ari-only readings it must stay in force to drop
    // synthesized garbage like 「ですg /出過/」 -> 「出過」.
    bool HasDirectEntry(const std::wstring& reading) const;

private:
    SkkDictionary() = default;

    // Parses a SKK-JISYO formatted file at `path`. UTF-8 encoded, LF or CRLF
    // line endings. Returns S_OK on success even if the file is malformed in
    // places — we skip unparseable lines rather than fail the whole load.
    HRESULT Load(const std::wstring& path);

    std::unordered_map<std::wstring, std::vector<std::wstring>> m_entries;
    // Separate map for okuri-ari stems (e.g. "ふ" → {振, 触, 降, ...} from
    // the "ふr" line). Kept apart from m_entries because the same stem
    // reading is also a valid okuri-nashi reading on its own ("ふ" → 不/府/
    // 普 from the okuri-nashi side, plus the okuri-ari kanji stems used
    // for verb inflection reconstruction).
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_okuri;
    // Set of readings that appeared as okuri-NASHI entries in the dict
    // (as opposed to okuri-ari flatten-through). See HasDirectEntry.
    // Uses a `set`-shaped unordered_map to reuse the same allocator hint;
    // membership is the only thing we care about.
    std::unordered_map<std::wstring, char> m_directReadings;
    // Readings of m_entries sorted lexicographically, as pointers into the
    // map's keys (unordered_map nodes are stable so the pointers stay valid
    // for the dictionary's lifetime). Built once at the end of Load; gives
    // PredictCompletions the lower_bound prefix-range scan the hash map
    // alone can't do.
    std::vector<const std::wstring*> m_sortedReadings;
    bool m_loaded = false;
};
