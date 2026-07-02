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
            // === 男性器 ===
            L"ちんぽ", L"ちんこ", L"ちんちん", L"おちんちん",
            L"ちんぽこ", L"ちんぽう", L"ちんちく", L"ちんとり",
            L"ぺにす", L"ぽこちん", L"まら", L"ふぐり",
            L"きんたま", L"たまたま", L"いんけい", L"だんこん",
            L"ずるむけ", L"ほうけい", L"ぱっく",
            // === 女性器 ===
            L"まんこ", L"おまんこ", L"おめこ", L"おそそ",
            L"ぼぼ", L"ちゃむ", L"わぎな", L"びらびら",
            L"くりとりす", L"くりちゃん", L"にくひだ", L"いんもう",
            L"あそこ",  // context-dependent but often 下ネタ
            // === 胸部 ===
            L"おっぱい", L"おっぱいちゃん", L"ちくび", L"ぱいぱい",
            L"ぱいずり", L"にゅうりん", L"にゅうとう", L"ばすと",
            L"おちち",
            // === 尻 / 肛門 ===
            L"けつ", L"けつあな", L"おしり", L"あなる",
            L"こうもん", L"あなるふぁっく",
            // === 性行為 ===
            L"せっくす", L"はめる", L"ふぇらちお", L"くんに",
            L"くんにり", L"くんにりんぐす", L"あなる", L"ぶっかけ",
            L"ぱいずり", L"ちんぽしゃぶり", L"すまた", L"ちくびあまえ",
            L"にゅうりんふぇらちお", L"あくめ", L"えっち", L"ばっく",
            L"きしゅたい", L"ぐらいんど", L"ろーたー",
            L"ちんぽさわり", L"はなあくめ", L"ちんぽおしゃぶり",
            L"みずぜめ",
            // === 分泌物 ===
            L"ざーめん", L"せいえき", L"ちんぽじる", L"あいえき",
            L"ちつえき", L"おしっこ", L"しょんべん",
            L"かんちょう",
            // === 自慰 ===
            L"せんずり", L"おなにー", L"しこしこ", L"しこる",
            L"じくり", L"ちんぽしごき", L"ふぇら",
            // === 四十八手 (traditional 48 sexual positions - well-known subset) ===
            L"ほんて", L"ちゃうす", L"さかさちゃうす", L"すわりちゃうす",
            L"ほかけちゃうす", L"まつばくずし", L"たちまつば", L"ちどり",
            L"てまくら", L"だきじぞう", L"だきあげ", L"だきしめ",
            L"しがらみ", L"みやま", L"わけいり", L"たちがなえ",
            L"すわりがなえ", L"うぐいすのたにわたり", L"しゅもくぞり",
            L"いわしみず", L"つばめがえし", L"おしぐるま", L"あじろ",
            L"でふね", L"いりふね", L"そりばし", L"はなびし",
            L"たちはなびし", L"ふかなさけ", L"しのび", L"うらばしご",
            L"くびひきれんぼ", L"にしきえのよう", L"つばくらお",
            L"いすかのはし", L"うのくちばし", L"にだんがえし",
            // === いく / 絶頂 ===
            L"いくいく", L"いっちゃう", L"あくめ",
            // === 罵倒 / 卑語 ===
            L"くそ", L"くそったれ", L"くそやろう",
            L"やりまん", L"やりちん", L"びっち",
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
