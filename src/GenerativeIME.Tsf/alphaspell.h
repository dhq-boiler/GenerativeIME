#pragma once

#include <string>
#include <vector>

// Acronym synthesis: if the whole reading segments into English letter
// names (あい+えむ+いー → I M E), offer the spelled-out forms as
// candidates ("IME", "ime"). Purely algorithmic — covers every acronym
// (URL, API, CPU, …) without dictionary entries, which is why あいえむいー
// works even though no SKK maintainer ever thought to add it.
namespace alphaspell
{
    // Returns { UPPERCASE, lowercase } when `reading` parses entirely as
    // 2+ letter names; empty vector otherwise. Two letters minimum because
    // single names collide with ordinary words (えー is a filler, あい is
    // 愛) and a 1-letter acronym isn't worth the candidate-list noise.
    std::vector<std::wstring> Spell(const std::wstring& reading);
}
