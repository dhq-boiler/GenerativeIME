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
            {L"nn", L"ん"},

            {L"kya", L"きゃ"}, {L"kyu", L"きゅ"}, {L"kyo", L"きょ"},
            {L"gya", L"ぎゃ"}, {L"gyu", L"ぎゅ"}, {L"gyo", L"ぎょ"},
            {L"sha", L"しゃ"}, {L"shu", L"しゅ"}, {L"sho", L"しょ"},
            {L"sya", L"しゃ"}, {L"syu", L"しゅ"}, {L"syo", L"しょ"},
            {L"ja", L"じゃ"}, {L"ju", L"じゅ"}, {L"jo", L"じょ"},
            {L"jya", L"じゃ"}, {L"jyu", L"じゅ"}, {L"jyo", L"じょ"},
            {L"cha", L"ちゃ"}, {L"chu", L"ちゅ"}, {L"cho", L"ちょ"},
            {L"cya", L"ちゃ"}, {L"cyu", L"ちゅ"}, {L"cyo", L"ちょ"},
            {L"tya", L"ちゃ"}, {L"tyu", L"ちゅ"}, {L"tyo", L"ちょ"},
            {L"nya", L"にゃ"}, {L"nyu", L"にゅ"}, {L"nyo", L"にょ"},
            {L"hya", L"ひゃ"}, {L"hyu", L"ひゅ"}, {L"hyo", L"ひょ"},
            {L"bya", L"びゃ"}, {L"byu", L"びゅ"}, {L"byo", L"びょ"},
            {L"pya", L"ぴゃ"}, {L"pyu", L"ぴゅ"}, {L"pyo", L"ぴょ"},
            {L"mya", L"みゃ"}, {L"myu", L"みゅ"}, {L"myo", L"みょ"},
            {L"rya", L"りゃ"}, {L"ryu", L"りゅ"}, {L"ryo", L"りょ"},

            {L"fa", L"ふぁ"}, {L"fi", L"ふぃ"}, {L"fe", L"ふぇ"}, {L"fo", L"ふぉ"},
            {L"va", L"ヴぁ"}, {L"vi", L"ヴぃ"}, {L"vu", L"ヴ"}, {L"ve", L"ヴぇ"}, {L"vo", L"ヴぉ"},
            {L"la", L"ぁ"}, {L"li", L"ぃ"}, {L"lu", L"ぅ"}, {L"le", L"ぇ"}, {L"lo", L"ぉ"},
            {L"xa", L"ぁ"}, {L"xi", L"ぃ"}, {L"xu", L"ぅ"}, {L"xe", L"ぇ"}, {L"xo", L"ぉ"},
            {L"ltu", L"っ"}, {L"xtu", L"っ"},
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

    constexpr int kMaxKey = 3;

    wchar_t ToLower(wchar_t c) { return (wchar_t)std::towlower(c); }

    bool IsSokuonConsonant(wchar_t c)
    {
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
