#pragma once

#include <string>
#include <vector>

// Sensitive-reading mask variants. For readings on a small curated list
// (adult vocabulary the user asked to be softened in chat contexts), the
// candidate window offers not just「ちんぽ」/「チンポ」 but also masked
// variants「ち〇ぽ」/「〇んぽ」/「ちん〇」so a user can pick a softer form
// without leaving the composition. Non-sensitive readings return an
// empty vector so the mechanism is opt-in per-reading and doesn't
// pollute every candidate list.

namespace masks
{
    // Returns hiragana + katakana masked variants for the given reading,
    // or an empty vector if the reading isn't on the sensitive list. Mask
    // character is 〇 (U+3007 IDEOGRAPHIC NUMBER ZERO) which reads
    // visually cleaner than ○ (U+25CB) in Japanese fonts. Each position
    // in the reading produces one variant; the two half-width letters
    // ゃぇぁ etc. count as positions too so「おっぱい」→ お○ぱい etc.
    std::vector<std::wstring> Variants(const std::wstring& reading);
}
