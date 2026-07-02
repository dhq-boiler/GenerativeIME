#include "masks.h"
#include <unordered_set>
#include <algorithm>

namespace masks
{
    // Curated sensitive-reading set. Kept small on purpose: every entry
    // here fires masked-variant expansion on every candidate window that
    // hits this reading, so a false positive is annoying (imagine
    // 「せいこう」→ ○いこう being offered when the user typed for 成功).
    // Readings on this list must be almost-always adult/vulgar in
    // context; ambiguous ones (せいこう / いく / etc.) stay off.
    static const std::unordered_set<std::wstring>& Sensitive()
    {
        static const auto* s = new std::unordered_set<std::wstring>{
            // 男性器
            L"ちんぽ", L"ちんこ", L"ちんちん", L"おちんちん",
            L"ぺにす", L"ぽこちん",
            // 女性器
            L"まんこ", L"おまんこ", L"おめこ",
            // 胸部
            L"おっぱい", L"ちくび",
            // 性行為 / 分泌物
            L"せっくす", L"ふぇらちお", L"くんに", L"あなる",
            L"ぶっかけ", L"ざーめん",
            // その他強い罵倒表現
            L"くそ",   // often chat-context censored
        };
        return *s;
    }

    // Hiragana → Katakana of a whole reading. Table walk instead of the
    // usual +0x60 offset so we skip 30FC (long vowel mark) and the
    // small-form range boundaries cleanly.
    static std::wstring HiraToKata(const std::wstring& hira)
    {
        std::wstring k;
        k.reserve(hira.size());
        for (wchar_t c : hira) {
            int u = (int)c;
            if (u >= 0x3041 && u <= 0x3096) k.push_back((wchar_t)(u + 0x60));
            else k.push_back(c);
        }
        return k;
    }

    std::vector<std::wstring> Variants(const std::wstring& reading)
    {
        const auto& s = Sensitive();
        if (s.find(reading) == s.end()) return {};

        constexpr wchar_t kMask = L'〇';  // U+3007 IDEOGRAPHIC NUMBER ZERO
        std::vector<std::wstring> out;
        // One variant per character position, first the hiragana forms.
        for (size_t i = 0; i < reading.size(); ++i) {
            std::wstring m = reading;
            m[i] = kMask;
            out.push_back(std::move(m));
        }
        // Then the katakana equivalents. Some users prefer カタカナ for
        // this class of vocabulary regardless of typing habit, so we
        // offer both and let the user pick.
        std::wstring kata = HiraToKata(reading);
        if (kata != reading) {
            for (size_t i = 0; i < kata.size(); ++i) {
                std::wstring m = kata;
                m[i] = kMask;
                out.push_back(std::move(m));
            }
        }
        return out;
    }
}
