#include "romajitokana.h"
#include <unordered_map>
#include <cwctype>

namespace
{
    using Table = std::unordered_map<std::wstring, std::wstring>;

    const Table& GetTable()
    {
        static const Table t = {
            {L"a", L"あ"}, {L"i", L"い"}, {L"u", L"う"}, {L"e", L"え"}, {L"o", L"お"},
            {L"ka", L"か"}, {L"ki", L"き"}, {L"ku", L"く"}, {L"ke", L"け"}, {L"ko", L"こ"},
            {L"ga", L"が"}, {L"gi", L"ぎ"}, {L"gu", L"ぐ"}, {L"ge", L"げ"}, {L"go", L"ご"},
            {L"sa", L"さ"}, {L"si", L"し"}, {L"shi", L"し"}, {L"su", L"す"}, {L"se", L"せ"}, {L"so", L"そ"},
            {L"za", L"ざ"}, {L"zi", L"じ"}, {L"ji", L"じ"}, {L"zu", L"ず"}, {L"ze", L"ぜ"}, {L"zo", L"ぞ"},
            {L"ta", L"た"}, {L"ti", L"ち"}, {L"chi", L"ち"}, {L"tu", L"つ"}, {L"tsu", L"つ"}, {L"te", L"て"}, {L"to", L"と"},
            {L"da", L"だ"}, {L"di", L"ぢ"}, {L"du", L"づ"}, {L"de", L"で"}, {L"do", L"ど"},
            {L"na", L"な"}, {L"ni", L"に"}, {L"nu", L"ぬ"}, {L"ne", L"ね"}, {L"no", L"の"},
            {L"ha", L"は"}, {L"hi", L"ひ"}, {L"hu", L"ふ"}, {L"fu", L"ふ"}, {L"he", L"へ"}, {L"ho", L"ほ"},
            {L"ba", L"ば"}, {L"bi", L"び"}, {L"bu", L"ぶ"}, {L"be", L"べ"}, {L"bo", L"ぼ"},
            {L"pa", L"ぱ"}, {L"pi", L"ぴ"}, {L"pu", L"ぷ"}, {L"pe", L"ぺ"}, {L"po", L"ぽ"},
            {L"ma", L"ま"}, {L"mi", L"み"}, {L"mu", L"む"}, {L"me", L"め"}, {L"mo", L"も"},
            {L"ya", L"や"}, {L"yu", L"ゆ"}, {L"yo", L"よ"},
            {L"ra", L"ら"}, {L"ri", L"り"}, {L"ru", L"る"}, {L"re", L"れ"}, {L"ro", L"ろ"},
            {L"wa", L"わ"}, {L"wo", L"を"},
            // "wi" / "we" / "wu" / "wyi" / "wye": IME convention for typing
            // ウィ / ウェ / ウ / ヰ / ヱ. Without these, ローマ字 "windou"
            // (= ウィンドウ) stalls partway through and the ASCII tail
            // ("windou") falls out to MeCab as garbage ("ぃんー同").
            {L"wi", L"うぃ"}, {L"we", L"うぇ"}, {L"wu", L"う"},
            {L"wyi", L"ゐ"}, {L"wye", L"ゑ"},
            {L"nn", L"ん"},

            // Y-series palatalized rows. The i / e columns produce
            // small-vowel forms (きぃ / きぇ / ちぃ / ちぇ …) which every
            // mainstream IME supports; the earlier tables missed them so
            // typing "tye" for チェ or "sye" for シェ fell into the
            // unmatched branch and leaked raw ASCII. Cha/Cya/Tya all map
            // to ちゃ so the ti/tya/cha aliases stay consistent.
            {L"kya", L"きゃ"}, {L"kyi", L"きぃ"}, {L"kyu", L"きゅ"}, {L"kye", L"きぇ"}, {L"kyo", L"きょ"},
            {L"gya", L"ぎゃ"}, {L"gyi", L"ぎぃ"}, {L"gyu", L"ぎゅ"}, {L"gye", L"ぎぇ"}, {L"gyo", L"ぎょ"},
            {L"sha", L"しゃ"}, {L"shu", L"しゅ"}, {L"sho", L"しょ"},
            {L"sya", L"しゃ"}, {L"syi", L"しぃ"}, {L"syu", L"しゅ"}, {L"sye", L"しぇ"}, {L"syo", L"しょ"},
            {L"ja", L"じゃ"}, {L"ju", L"じゅ"}, {L"jo", L"じょ"},
            {L"jya", L"じゃ"}, {L"jyi", L"じぃ"}, {L"jyu", L"じゅ"}, {L"jye", L"じぇ"}, {L"jyo", L"じょ"},
            {L"zya", L"じゃ"}, {L"zyi", L"じぃ"}, {L"zyu", L"じゅ"}, {L"zye", L"じぇ"}, {L"zyo", L"じょ"},
            {L"cha", L"ちゃ"}, {L"chu", L"ちゅ"}, {L"cho", L"ちょ"},
            {L"cya", L"ちゃ"}, {L"cyi", L"ちぃ"}, {L"cyu", L"ちゅ"}, {L"cye", L"ちぇ"}, {L"cyo", L"ちょ"},
            {L"tya", L"ちゃ"}, {L"tyi", L"ちぃ"}, {L"tyu", L"ちゅ"}, {L"tye", L"ちぇ"}, {L"tyo", L"ちょ"},
            {L"dya", L"ぢゃ"}, {L"dyi", L"ぢぃ"}, {L"dyu", L"ぢゅ"}, {L"dye", L"ぢぇ"}, {L"dyo", L"ぢょ"},
            {L"nya", L"にゃ"}, {L"nyi", L"にぃ"}, {L"nyu", L"にゅ"}, {L"nye", L"にぇ"}, {L"nyo", L"にょ"},
            {L"hya", L"ひゃ"}, {L"hyi", L"ひぃ"}, {L"hyu", L"ひゅ"}, {L"hye", L"ひぇ"}, {L"hyo", L"ひょ"},
            {L"bya", L"びゃ"}, {L"byi", L"びぃ"}, {L"byu", L"びゅ"}, {L"bye", L"びぇ"}, {L"byo", L"びょ"},
            {L"pya", L"ぴゃ"}, {L"pyi", L"ぴぃ"}, {L"pyu", L"ぴゅ"}, {L"pye", L"ぴぇ"}, {L"pyo", L"ぴょ"},
            {L"mya", L"みゃ"}, {L"myi", L"みぃ"}, {L"myu", L"みゅ"}, {L"mye", L"みぇ"}, {L"myo", L"みょ"},
            {L"rya", L"りゃ"}, {L"ryi", L"りぃ"}, {L"ryu", L"りゅ"}, {L"rye", L"りぇ"}, {L"ryo", L"りょ"},

            // Foreign-sound rows. Most modern IMEs offer these so the user
            // can type ファイル / ヴィヴィッド / ティーチャー without
            // dropping into katakana mode.
            {L"fa", L"ふぁ"}, {L"fi", L"ふぃ"}, {L"fe", L"ふぇ"}, {L"fo", L"ふぉ"},
            {L"fya", L"ふゃ"}, {L"fyu", L"ふゅ"}, {L"fyo", L"ふょ"},
            {L"va", L"ヴぁ"}, {L"vi", L"ヴぃ"}, {L"vu", L"ヴ"}, {L"ve", L"ヴぇ"}, {L"vo", L"ヴぉ"},
            {L"vya", L"ヴゃ"}, {L"vyu", L"ヴゅ"}, {L"vyo", L"ヴょ"},
            {L"ye", L"いぇ"},                       // イェ
            {L"she", L"しぇ"},                      // シェ
            {L"je",  L"じぇ"},                      // ジェ
            {L"che", L"ちぇ"},                      // チェ
            {L"tsa", L"つぁ"}, {L"tsi", L"つぃ"}, {L"tse", L"つぇ"}, {L"tso", L"つぉ"},
            {L"tha", L"てゃ"}, {L"thi", L"てぃ"}, {L"thu", L"てゅ"}, {L"the", L"てぇ"}, {L"tho", L"てょ"},
            {L"dha", L"でゃ"}, {L"dhi", L"でぃ"}, {L"dhu", L"でゅ"}, {L"dhe", L"でぇ"}, {L"dho", L"でょ"},
            {L"twa", L"とぁ"}, {L"twi", L"とぃ"}, {L"twu", L"とぅ"}, {L"twe", L"とぇ"}, {L"two", L"とぉ"},
            {L"dwa", L"どぁ"}, {L"dwi", L"どぃ"}, {L"dwu", L"どぅ"}, {L"dwe", L"どぇ"}, {L"dwo", L"どぉ"},
            {L"kwa", L"くぁ"}, {L"kwi", L"くぃ"}, {L"kwe", L"くぇ"}, {L"kwo", L"くぉ"},
            {L"gwa", L"ぐぁ"}, {L"gwi", L"ぐぃ"}, {L"gwe", L"ぐぇ"}, {L"gwo", L"ぐぉ"},
            {L"swa", L"すぁ"}, {L"swi", L"すぃ"}, {L"swe", L"すぇ"}, {L"swo", L"すぉ"},
            {L"hwa", L"ふぁ"}, {L"hwi", L"ふぃ"}, {L"hwe", L"ふぇ"}, {L"hwo", L"ふぉ"},
            // Small-kana shorthands. l*/x* prefix → produces the small
            // version directly (useful for things the table above can't
            // express, e.g. ぃ on its own, or ょ at the start of a row).
            {L"la", L"ぁ"}, {L"li", L"ぃ"}, {L"lu", L"ぅ"}, {L"le", L"ぇ"}, {L"lo", L"ぉ"},
            {L"xa", L"ぁ"}, {L"xi", L"ぃ"}, {L"xu", L"ぅ"}, {L"xe", L"ぇ"}, {L"xo", L"ぉ"},
            {L"lya", L"ゃ"}, {L"lyu", L"ゅ"}, {L"lyo", L"ょ"},
            {L"xya", L"ゃ"}, {L"xyu", L"ゅ"}, {L"xyo", L"ょ"},
            {L"lwa", L"ゎ"}, {L"xwa", L"ゎ"},
            {L"lke", L"ヶ"}, {L"xke", L"ヶ"},
            {L"ltu", L"っ"}, {L"xtu", L"っ"},
            {L"ltsu", L"っ"}, {L"xtsu", L"っ"},
            {L"-", L"ー"},

            // Punctuation / common symbols: when the IME is on we map ASCII
            // input to full-width Japanese equivalents so the user gets
            // natural text without having to toggle IME off for every comma.
            {L",",  L"、"}, {L".",  L"。"},
            {L"!",  L"！"}, {L"?",  L"？"},
            {L"/",  L"・"},
            {L"[",  L"「"}, {L"]",  L"」"},
            {L"(",  L"（"}, {L")",  L"）"},
            {L"{",  L"｛"}, {L"}",  L"｝"},
            {L"<",  L"＜"}, {L">",  L"＞"},
            {L":",  L"："}, {L";",  L"；"},
            {L"+",  L"＋"}, {L"=",  L"＝"}, {L"_",  L"＿"},
            {L"'",  L"’"}, {L"\"", L"”"},
            {L"@",  L"＠"}, {L"#",  L"＃"}, {L"$",  L"＄"}, {L"%",  L"％"},
            {L"^",  L"＾"}, {L"&",  L"＆"}, {L"*",  L"＊"}, {L"~",  L"〜"},
            {L"|",  L"｜"}, {L"\\", L"￥"}, {L"`",  L"｀"},
        };
        return t;
    }

    // Up to 4 chars: covers "ltsu" / "xtsu" / "wyi" / "wye". The longest-
    // first lookup inside Convert prefers the longer match so a literal
    // "ts" inside the buffer still resolves to つ when followed by a vowel.
    constexpr int kMaxKey = 4;

    wchar_t ToLower(wchar_t c) { return (wchar_t)std::towlower(c); }

    bool IsSokuonConsonant(wchar_t c)
    {
        // Sokuon triggers on a doubled ROMAJI consonant only ("kk" → っk,
        // "tt" → っt …). Restricting the check to a-z prevents doubled
        // digits ("99") and symbols ("..") from spuriously producing っ —
        // e.g. typing 「３０９９」 was rendered as 「３０っ９」 because '9'
        // is not a vowel and not 'n', so the previous "anything not a
        // vowel/n" predicate accepted it. Vowels and 'n' are still excluded
        // (they never form sokuon in Hepburn romaji).
        if (c < L'a' || c > L'z') return false;
        return c != L'a' && c != L'i' && c != L'u' && c != L'e' && c != L'o' && c != L'n';
    }
}

namespace romaji
{
    Result Convert(std::wstring_view romaji)
    {
        const auto& table = GetTable();
        std::wstring hira;
        hira.reserve(romaji.size());

        size_t i = 0;
        while (i < romaji.size())
        {
            bool matched = false;
            const size_t maxLen = (std::min)((size_t)kMaxKey, romaji.size() - i);
            for (size_t len = maxLen; len >= 1; --len)
            {
                std::wstring key(romaji.substr(i, len));
                for (auto& c : key) c = ToLower(c);
                auto it = table.find(key);
                if (it != table.end())
                {
                    hira.append(it->second);
                    i += len;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;

            // Sokuon: doubled consonant (kk, tt, ss, ...) → っ + remainder
            if (i + 1 < romaji.size())
            {
                wchar_t c = ToLower(romaji[i]);
                wchar_t next = ToLower(romaji[i + 1]);
                if (c == next && IsSokuonConsonant(c))
                {
                    hira.push_back(L'っ');
                    i += 1;
                    continue;
                }
            }

            // 'n' followed by a non-vowel / non-y / non-n consonant → ん
            if (ToLower(romaji[i]) == L'n' && i + 1 < romaji.size())
            {
                wchar_t next = ToLower(romaji[i + 1]);
                if (next != L'a' && next != L'i' && next != L'u' && next != L'e' && next != L'o'
                    && next != L'y' && next != L'n')
                {
                    hira.push_back(L'ん');
                    i += 1;
                    continue;
                }
            }

            // Digits (0-9) pass through so mixed input like "dai1kai"
            // becomes "だい1かい" in the composition, kept as one live
            // composition until Space converts the whole thing to
            // 第1回. Without this, the digit hit the "unmatched" branch
            // below and the caller's composition auto-committed on the
            // key that never got into the buffer.
            if (romaji[i] >= L'0' && romaji[i] <= L'9')
            {
                hira.push_back(romaji[i]);
                i += 1;
                continue;
            }

            // Uppercase Roman letters pass through unchanged. Every entry
            // in the table above is keyed on lowercase, so an uppercase
            // char is a deliberate "keep this letter as-is" signal from
            // the caller ("Gsupotto" → "Gすぽっと" which SKK converts to
            // 「Gスポット」via its Gすぽっと direct entry). Without this,
            // Shift+G in 全角かな mode was impossible to type because
            // the letter fell to the "unmatched" branch and stalled.
            if (romaji[i] >= L'A' && romaji[i] <= L'Z')
            {
                hira.push_back(romaji[i]);
                i += 1;
                continue;
            }

            // Full-width Roman letters (Ａ-Ｚ / ａ-ｚ) also pass through: in
            // 全角ひらがな mode Shift+alpha injects a full-width uppercase
            // letter straight into the buffer ("ＩＭＥ") so it can be
            // committed as-is or swapped to half-width from the candidate
            // window before commit. Keep scanning so trailing kana in a
            // mixed buffer ("ＩＭＥです") still convert.
            if ((romaji[i] >= 0xFF21 && romaji[i] <= 0xFF3A) ||
                (romaji[i] >= 0xFF41 && romaji[i] <= 0xFF5A))
            {
                hira.push_back(romaji[i]);
                i += 1;
                continue;
            }

            // Unmatched: leave the rest as romaji for the caller to keep typing.
            return { hira, std::wstring(romaji.substr(i)) };
        }
        return { hira, L"" };
    }

    std::wstring FinalizeTrailingN(std::wstring_view romaji)
    {
        if (romaji == L"n") return L"ん";
        return std::wstring(romaji);
    }
}
