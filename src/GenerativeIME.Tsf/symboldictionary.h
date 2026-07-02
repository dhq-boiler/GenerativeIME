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

    // Full-width <-> half-width punctuation pair lookup for a single-char
    // composition. Given "！" returns {"！", "!"}; given "!" returns
    // {"!", "！"} — the typed form stays at index 0 so a bare Enter keeps
    // what the user got, while ↓/Space picks the other form. Empty for
    // anything that isn't a known punctuation pair.
    std::vector<std::wstring> PunctPairs(std::wstring_view typed);

    // Single ASCII letter -> glyph variants (typed form first, then the
    // full-width / opposite-case / regional-indicator 🇼 / circled Ⓦ
    // forms). Same idea as the digit entries ("5" → Ⅴ/⑤/五) but generated
    // arithmetically for all 26 letters instead of hand-listed. Empty for
    // anything that isn't exactly one ASCII letter.
    std::vector<std::wstring> LetterVariants(std::wstring_view typed);
}
