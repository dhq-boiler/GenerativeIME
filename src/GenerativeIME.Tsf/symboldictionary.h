#pragma once

#include <string>
#include <string_view>
#include <vector>

// Reading -> symbol/unit lookup. Bypasses the LLM for things MeCab can't
// give a reading for (Unicode-compat symbols like ℃, ㎢, →) and for things
// users say by visual name ("やじるし" -> "→"). Hits from here go straight
// into the candidate list without going through Ollama, so simple symbol
// input stays instant.
namespace symbols
{
    // Direct exact-match lookup. Empty vector if no hit.
    std::vector<std::wstring> Lookup(std::wstring_view reading);

    // Longest-prefix segmentation: combines multiple keys ("ろーま5うえつき2" -> "Ⅴ²").
    // Returns empty vector if no segmentation works.
    std::vector<std::wstring> SegmentedLookup(std::wstring_view reading);

    // Top-level entry the IME calls: tries Lookup, then SegmentedLookup,
    // then choonpu-expanded variants. Returns first that produces results.
    std::vector<std::wstring> LookupAll(std::wstring_view reading);
}
