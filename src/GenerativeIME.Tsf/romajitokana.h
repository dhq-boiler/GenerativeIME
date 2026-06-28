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
}
