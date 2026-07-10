#pragma once

#include <string>
#include <string_view>

// Port of GenerativeIME.Core.RomajiToHiragana (C#). Pure function: same input
// always returns the same (hira, remaining) split. `remaining` is the trailing
// romaji that hasn't yet matched a kana (e.g. "ky" while user is mid-typing
// "kyou"). Display in composition should be hira + remaining so the user sees
// their literal typing tail until it resolves.
namespace romaji
{
    struct Result
    {
        std::wstring hira;
        std::wstring remaining;
    };

    Result Convert(std::wstring_view romaji);

    // For commit: a lone trailing "n" should resolve to "ん" rather than stay
    // as romaji. Only handles the simplest case (exactly "n"); intermediate
    // states like "kan" already convert via Convert's normal n→ん rule.
    std::wstring FinalizeTrailingN(std::wstring_view romaji);

    // Backspace helper: returns the number of romaji chars that produced the
    // LAST kana in Convert(romaji). Meant for callers that have already
    // verified Convert(romaji).remaining is empty — i.e. the buffer is fully
    // resolved — so the returned length maps to exactly one visible kana.
    // For "aka" → 2 (the "ka" tail), for "n" → 0 (no chunk consumed; "n"
    // alone is still unresolved). Callers should fall back to a plain
    // pop_back() when this returns 0.
    size_t LastKanaLen(std::wstring_view romaji);
}
