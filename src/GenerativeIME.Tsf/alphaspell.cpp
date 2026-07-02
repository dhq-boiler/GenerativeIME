#include "alphaspell.h"

#include <algorithm>
#include <cwchar>

namespace
{
    struct LetterName
    {
        const wchar_t* reading;
        wchar_t        letter;
    };

    // Hiragana names of the English letters, with common spelling variants
    // (both えー/えい for A, じぇー/じぇい for J, …). The romaji layer has
    // already normalized input to hiragana, so katakana forms are not needed.
    constexpr LetterName kNames[] = {
        { L"えー",       L'A' }, { L"えい",   L'A' },
        { L"びー",       L'B' },
        { L"しー",       L'C' },
        { L"でぃー",     L'D' },
        { L"いー",       L'E' },
        { L"えふ",       L'F' },
        { L"じー",       L'G' },
        { L"えいち",     L'H' }, { L"えっち", L'H' },
        { L"あい",       L'I' },
        { L"じぇー",     L'J' }, { L"じぇい", L'J' },
        { L"けー",       L'K' }, { L"けい",   L'K' },
        { L"える",       L'L' },
        { L"えむ",       L'M' },
        { L"えぬ",       L'N' },
        { L"おー",       L'O' },
        { L"ぴー",       L'P' },
        { L"きゅー",     L'Q' },
        { L"あーる",     L'R' },
        { L"えす",       L'S' },
        { L"てぃー",     L'T' },
        { L"ゆー",       L'U' },
        { L"ぶい",       L'V' },
        { L"だぶりゅー", L'W' }, { L"だぶるー", L'W' },
        { L"えっくす",   L'X' },
        { L"わい",       L'Y' },
        { L"ぜっと",     L'Z' },
    };

    // Names sorted longest-first so the parser prefers えいち(H) over
    // えい(A)+ち-dead-end before backtracking has to kick in.
    const std::vector<LetterName>& SortedNames()
    {
        static const std::vector<LetterName> sorted = []
        {
            std::vector<LetterName> v(std::begin(kNames), std::end(kNames));
            std::stable_sort(v.begin(), v.end(),
                             [](const LetterName& a, const LetterName& b)
                             { return wcslen(a.reading) > wcslen(b.reading); });
            return v;
        }();
        return sorted;
    }

    // Backtracking parse with a dead-position memo: readings are short, and
    // `failed` caps the work at O(positions × names) even on adversarial
    // input like えいえいえいえい….
    bool ParseFrom(const std::wstring& r, size_t pos,
                   std::wstring& out, std::vector<char>& failed)
    {
        if (pos == r.size()) return true;
        if (failed[pos]) return false;
        for (const auto& n : SortedNames())
        {
            size_t len = wcslen(n.reading);
            if (r.compare(pos, len, n.reading) == 0)
            {
                out.push_back(n.letter);
                if (ParseFrom(r, pos + len, out, failed)) return true;
                out.pop_back();
            }
        }
        failed[pos] = 1;
        return false;
    }
}

namespace alphaspell
{
    std::vector<std::wstring> Spell(const std::wstring& reading)
    {
        if (reading.size() < 4) return {};  // 2 letters × min 2 chars each

        std::wstring upper;
        std::vector<char> failed(reading.size(), 0);
        if (!ParseFrom(reading, 0, upper, failed)) return {};
        if (upper.size() < 2) return {};

        std::wstring lower = upper;
        for (auto& c : lower) c = (wchar_t)towlower(c);
        return { upper, lower };
    }
}
