#include "emojitext.h"

namespace
{
    // Joiners / modifiers that ride along inside emoji sequences without
    // deciding emoji-ness by themselves.
    bool IsEmojiJoiner(char32_t cp)
    {
        return cp == 0x200D // ZWJ (🐕‍🦺)
            || cp == 0xFE0F // VS16
            || cp == 0x20E3 // keycap
            || (cp >= 0x1F3FB && cp <= 0x1F3FF); // skin tones
    }

    // Blocks whose members are emoji (or render as emoji together with
    // VS16, which our dictionaries always attach to text-default bases).
    // Deliberately NOT included: 25A0-25B9 geometric shapes (■ △ are
    // ordinary text symbols), 2190-21FF arrows (→ from the symbol dict),
    // 2605/2606 (★☆) — those must stay unlabeled.
    bool IsEmojiCp(char32_t cp)
    {
        return (cp >= 0x1F000 && cp <= 0x1FAFF) // pictographs, flags, …
            || (cp >= 0x2600 && cp <= 0x27BF // misc symbols + dingbats
                && cp != 0x2605 && cp != 0x2606) //   …minus text ★☆
            || (cp >= 0x2B00 && cp <= 0x2BFF) // ⭐ ⬛ ⭕
            || cp == 0x203C || cp == 0x2049 // ‼ ⁉
            || cp == 0x2122 || cp == 0x2139 // ™ ℹ
            || cp == 0x2934 || cp == 0x2935
            || cp == 0x231A || cp == 0x231B || cp == 0x2328
            || (cp >= 0x23CF && cp <= 0x23FA) // ⏏ ⏩ … ⏺
            || cp == 0x24C2
            || cp == 0x3030 || cp == 0x303D
            || cp == 0x3297 || cp == 0x3299;
    }
}

namespace emojitext
{
    bool IsEmoji(std::wstring_view s)
    {
        if (s.empty()) return false;
        bool sawEmoji = false;
        size_t i = 0;
        while (i < s.size())
        {
            char32_t cp = s[i];
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()
                && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                i += 2;
            }
            else
            {
                i += 1;
            }

            if (IsEmojiJoiner(cp)) continue;
            // Every non-joiner codepoint must be emoji — a single stray
            // kana/kanji/latin char means this is text that happens to
            // contain a symbol, not an emoji candidate.
            if (!IsEmojiCp(cp)) return false;
            sawEmoji = true;
        }
        return sawEmoji;
    }
}
