using System.Text;

namespace GenerativeIME.Core;

public static class RomajiToHiragana
{
    private const int MaxKey = 3;

    private static readonly Dictionary<string, string> Table = new(StringComparer.Ordinal)
    {
        ["a"] = "あ", ["i"] = "い", ["u"] = "う", ["e"] = "え", ["o"] = "お",
        ["ka"] = "か", ["ki"] = "き", ["ku"] = "く", ["ke"] = "け", ["ko"] = "こ",
        ["ga"] = "が", ["gi"] = "ぎ", ["gu"] = "ぐ", ["ge"] = "げ", ["go"] = "ご",
        ["sa"] = "さ", ["si"] = "し", ["shi"] = "し", ["su"] = "す", ["se"] = "せ", ["so"] = "そ",
        ["za"] = "ざ", ["zi"] = "じ", ["ji"] = "じ", ["zu"] = "ず", ["ze"] = "ぜ", ["zo"] = "ぞ",
        ["ta"] = "た", ["ti"] = "ち", ["chi"] = "ち", ["tu"] = "つ", ["tsu"] = "つ", ["te"] = "て", ["to"] = "と",
        ["da"] = "だ", ["di"] = "ぢ", ["du"] = "づ", ["de"] = "で", ["do"] = "ど",
        ["na"] = "な", ["ni"] = "に", ["nu"] = "ぬ", ["ne"] = "ね", ["no"] = "の",
        ["ha"] = "は", ["hi"] = "ひ", ["hu"] = "ふ", ["fu"] = "ふ", ["he"] = "へ", ["ho"] = "ほ",
        ["ba"] = "ば", ["bi"] = "び", ["bu"] = "ぶ", ["be"] = "べ", ["bo"] = "ぼ",
        ["pa"] = "ぱ", ["pi"] = "ぴ", ["pu"] = "ぷ", ["pe"] = "ぺ", ["po"] = "ぽ",
        ["ma"] = "ま", ["mi"] = "み", ["mu"] = "む", ["me"] = "め", ["mo"] = "も",
        ["ya"] = "や", ["yu"] = "ゆ", ["yo"] = "よ",
        ["ra"] = "ら", ["ri"] = "り", ["ru"] = "る", ["re"] = "れ", ["ro"] = "ろ",
        ["wa"] = "わ", ["wo"] = "を",
        ["nn"] = "ん",

        ["kya"] = "きゃ", ["kyu"] = "きゅ", ["kyo"] = "きょ",
        ["gya"] = "ぎゃ", ["gyu"] = "ぎゅ", ["gyo"] = "ぎょ",
        ["sha"] = "しゃ", ["shu"] = "しゅ", ["sho"] = "しょ",
        ["sya"] = "しゃ", ["syu"] = "しゅ", ["syo"] = "しょ",
        ["ja"] = "じゃ", ["ju"] = "じゅ", ["jo"] = "じょ",
        ["jya"] = "じゃ", ["jyu"] = "じゅ", ["jyo"] = "じょ",
        ["cha"] = "ちゃ", ["chu"] = "ちゅ", ["cho"] = "ちょ",
        ["cya"] = "ちゃ", ["cyu"] = "ちゅ", ["cyo"] = "ちょ",
        ["tya"] = "ちゃ", ["tyu"] = "ちゅ", ["tyo"] = "ちょ",
        ["nya"] = "にゃ", ["nyu"] = "にゅ", ["nyo"] = "にょ",
        ["hya"] = "ひゃ", ["hyu"] = "ひゅ", ["hyo"] = "ひょ",
        ["bya"] = "びゃ", ["byu"] = "びゅ", ["byo"] = "びょ",
        ["pya"] = "ぴゃ", ["pyu"] = "ぴゅ", ["pyo"] = "ぴょ",
        ["mya"] = "みゃ", ["myu"] = "みゅ", ["myo"] = "みょ",
        ["rya"] = "りゃ", ["ryu"] = "りゅ", ["ryo"] = "りょ",

        ["fa"] = "ふぁ", ["fi"] = "ふぃ", ["fe"] = "ふぇ", ["fo"] = "ふぉ",
        ["va"] = "ヴぁ", ["vi"] = "ヴぃ", ["vu"] = "ヴ", ["ve"] = "ヴぇ", ["vo"] = "ヴぉ",
        ["la"] = "ぁ", ["li"] = "ぃ", ["lu"] = "ぅ", ["le"] = "ぇ", ["lo"] = "ぉ",
        ["xa"] = "ぁ", ["xi"] = "ぃ", ["xu"] = "ぅ", ["xe"] = "ぇ", ["xo"] = "ぉ",
        ["ltu"] = "っ", ["xtu"] = "っ",
        ["-"] = "ー"
    };

    public static (string Hiragana, string Remaining) Convert(string romaji)
    {
        var hira = new StringBuilder();
        var i = 0;
        while (i < romaji.Length)
        {
            var matched = false;
            var max = Math.Min(MaxKey, romaji.Length - i);

            for (var len = max; len >= 1; len--)
            {
                var key = romaji.Substring(i, len).ToLowerInvariant();
                if (Table.TryGetValue(key, out var kana))
                {
                    hira.Append(kana);
                    i += len;
                    matched = true;
                    break;
                }
            }

            if (matched)
            {
                continue;
            }

            // sokuon: same consonant twice (kk, tt, ss, …) → っ + remainder
            if (i + 1 < romaji.Length)
            {
                var c = char.ToLowerInvariant(romaji[i]);
                var next = char.ToLowerInvariant(romaji[i + 1]);
                if (c == next && IsSokuonConsonant(c))
                {
                    hira.Append('っ');
                    i += 1;
                    continue;
                }
            }

            // 'n' followed by non-vowel/y/n consonant → ん
            if (char.ToLowerInvariant(romaji[i]) == 'n' && i + 1 < romaji.Length)
            {
                var next = char.ToLowerInvariant(romaji[i + 1]);
                if (next != 'a' && next != 'i' && next != 'u' && next != 'e' && next != 'o'
                    && next != 'y' && next != 'n')
                {
                    hira.Append('ん');
                    i += 1;
                    continue;
                }
            }

            // unmatched → leftover for caller
            return (hira.ToString(), romaji.Substring(i));
        }

        return (hira.ToString(), string.Empty);
    }

    public static string FinalizeTrailingN(string romaji)
    {
        if (romaji == "n")
        {
            return "ん";
        }

        return romaji;
    }

    private static bool IsSokuonConsonant(char c)
    {
        return c is not ('a' or 'i' or 'u' or 'e' or 'o' or 'n');
    }
}