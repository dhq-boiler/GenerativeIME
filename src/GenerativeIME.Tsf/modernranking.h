#pragma once

#include <string>
#include <vector>

// Modern-usage top-candidate override for readings where the raw SKK-JISYO.L
// ranking is out of sync with actual modern-Japanese usage frequency.
//
// The rationale is data-driven: mining 1000 random ja.wikipedia articles
// (see scripts/mine/mine_wikipedia.ps1 + corpus/goldens/wikipedia-top.tsv)
// yielded 387 (reading, expected_kanji) pairs occurring ≥100 times. Of
// those, 91 had SKK top-candidate wrong per modern usage (e.g. SKK gives
// し→死, corpus wants し→市; SKK gives だい→大, corpus wants だい→第),
// and 7 had no direct SKK entry at all (mostly verb inflection forms
// MeCab morphology already handles at bunsetsu level, plus a few 助詞
// 複合 like「に対して」/「に関する」/「と共に」).
//
// Where a single reading has multiple valid expected kanji (かん appears
// as 館/間/巻/官/艦; か as 家/化/科/下), the entry here reflects the
// HIGHEST-frequency choice in the corpus. Alternatives remain reachable
// through the candidate list under SKK's own ordering.
//
// The map is applied by PromoteToTop() at the point where either the
// whole-reading SKK direct-hit path (textservice.cpp) or the per-morpheme
// bunsetsu noun path (bunsetsu.cpp) has just retrieved SKK candidates:
// if `reading` matches an entry AND the preferred kanji is already in the
// candidate list, it is moved to position 0. If the kanji is NOT in the
// list, it is FRONT-INSERTED — this handles the "no SKK entry" tail
// (verb inflection forms, 助詞 compounds) with the same code path.

namespace modernranking
{
    struct Entry
    {
        const wchar_t* reading;
        const wchar_t* preferred;
    };

    // Returns the preferred kanji surface for `reading`, or empty if the
    // reading is not in the override map.
    std::wstring GetPreferred(const std::wstring& reading);

    // Reorder `candidates` so the modern-usage preferred surface (if any)
    // sits at index 0. Existing candidates are preserved and de-duped.
    // If the preferred surface isn't in `candidates`, it's front-inserted.
    // Returns the possibly-reordered vector (input passed by value on purpose).
    std::vector<std::wstring> PromoteToTop(const std::wstring& reading,
                                           std::vector<std::wstring> candidates);
}
