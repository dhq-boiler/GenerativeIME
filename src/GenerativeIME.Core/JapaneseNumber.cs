using System.Text;

namespace GenerativeIME.Core;

/// 「ごじゅう」「にひゃくさんじゅうよん」のような日本語数詞読みを整数値に解釈し、
/// 半角/全角/ローマ数字/漢数字 の表記候補に変換する。
public static class JapaneseNumber
{
    private static readonly (string Reading, int Value)[] Digits =
    {
        // 長い順に並べる（最長一致のため）
        ("きゅう", 9),
        ("よん", 4), ("なな", 7),
        ("いち", 1), ("しち", 7), ("はち", 8),
        ("に", 2), ("さん", 3), ("し", 4), ("ご", 5), ("ろく", 6), ("く", 9)
    };

    private static readonly (string Reading, int Value)[] Multipliers =
    {
        ("おく", 100_000_000),
        ("まん", 10_000),
        ("せん", 1_000), ("ぜん", 1_000),
        ("ひゃく", 100), ("びゃく", 100), ("ぴゃく", 100),
        ("じゅう", 10), ("じっ", 10)
    };

    // 特殊連声: 数字＋接頭辞でひらがな表記が変化するもの
    // 例: ろっぴゃく(600), はっぴゃく(800), さんびゃく(300), さんぜん(3000), はっせん(8000), いっせん(1000)
    private static readonly Dictionary<string, int> SpecialCompounds = new(StringComparer.Ordinal)
    {
        ["さんびゃく"] = 300,
        ["ろっぴゃく"] = 600,
        ["はっぴゃく"] = 800,
        ["さんぜん"] = 3000,
        ["はっせん"] = 8000,
        ["いっせん"] = 1000
    };

    public static int? Parse(string reading)
    {
        if (string.IsNullOrEmpty(reading))
        {
            return null;
        }

        var total = 0L;
        var unitTotal = 0L; // 万・億の単位ごとの合計
        var current = 0L; // 桁を組み立てる際の途中値
        var pos = 0;

        while (pos < reading.Length)
        {
            // 特殊連声を優先試行
            var foundSpecial = false;
            foreach (var kv in SpecialCompounds)
            {
                if (StartsWith(reading, pos, kv.Key))
                {
                    unitTotal += kv.Value;
                    pos += kv.Key.Length;
                    foundSpecial = true;
                    break;
                }
            }

            if (foundSpecial)
            {
                continue;
            }

            // 桁修飾語 (じゅう/ひゃく/せん/まん/おく) を先に確認
            // 例: 「ひゃく」単独で 100、「にひゃく」で 200
            var (mult, mlen) = TryMatch(reading, pos, Multipliers);
            if (mult > 0)
            {
                if (current == 0)
                {
                    current = 1;
                }

                if (mult >= 10_000)
                {
                    // 万・億: 上位スコープを確定
                    total += (unitTotal + current) * mult;
                    unitTotal = 0;
                }
                else
                {
                    unitTotal += current * mult;
                }

                current = 0;
                pos += mlen;
                continue;
            }

            // 1-9 の数字
            var (digit, dlen) = TryMatch(reading, pos, Digits);
            if (digit > 0)
            {
                current = digit;
                pos += dlen;
                continue;
            }

            // 想定外の文字 → 数詞ではない
            return null;
        }

        total += unitTotal + current;
        return total > int.MaxValue ? null : (int)total;
    }

    public static IReadOnlyList<string> ToVariants(int n)
    {
        if (n < 0)
        {
            return Array.Empty<string>();
        }

        var list = new List<string>(4);
        list.Add(n.ToString());
        list.Add(ToFullWidth(n));
        var roman = ToRoman(n);
        if (roman != null)
        {
            list.Add(roman);
        }

        var kanji = ToKanji(n);
        if (kanji != null)
        {
            list.Add(kanji);
        }

        // 丸囲み（1-50だけ標準フォントにある）
        if (n is >= 1 and <= 50)
        {
            list.Add(CircledNumber(n));
        }

        return list;
    }

    public static string? ToRoman(int n)
    {
        if (n <= 0 || n > 3999)
        {
            return null;
        }

        var vals = new[] { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
        var syms = new[] { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
        var sb = new StringBuilder();
        for (var i = 0; i < vals.Length; i++)
        {
            while (n >= vals[i])
            {
                sb.Append(syms[i]);
                n -= vals[i];
            }
        }

        return sb.ToString();
    }

    public static string ToFullWidth(int n)
    {
        var s = n.ToString();
        var sb = new StringBuilder(s.Length);
        foreach (var c in s)
        {
            if (c >= '0' && c <= '9')
            {
                sb.Append((char)(c - '0' + '０'));
            }
            else
            {
                sb.Append(c);
            }
        }

        return sb.ToString();
    }

    public static string? ToKanji(int n)
    {
        if (n == 0)
        {
            return "〇";
        }

        if (n < 0)
        {
            return null;
        }

        if (n >= 1_0000_0000_0000L)
        {
            return null; // 兆オーバー
        }

        var sb = new StringBuilder();
        var kanjiDigit = new[] { "", "一", "二", "三", "四", "五", "六", "七", "八", "九" };

        var oku = n / 100_000_000;
        var manSection = n / 10_000 % 10_000;
        var lower = n % 10_000;

        if (oku > 0)
        {
            sb.Append(KanjiBelow10000(oku));
            sb.Append("億");
        }

        if (manSection > 0)
        {
            sb.Append(KanjiBelow10000(manSection));
            sb.Append("万");
        }

        if (lower > 0)
        {
            sb.Append(KanjiBelow10000(lower));
        }

        return sb.ToString();

        static string KanjiBelow10000(int x)
        {
            var sb2 = new StringBuilder();
            var d = new[] { "", "一", "二", "三", "四", "五", "六", "七", "八", "九" };
            var q1000 = x / 1000;
            var q100 = x / 100 % 10;
            var q10 = x / 10 % 10;
            var q1 = x % 10;
            if (q1000 > 0)
            {
                sb2.Append(q1000 == 1 ? "" : d[q1000]);
                sb2.Append("千");
            }

            if (q100 > 0)
            {
                sb2.Append(q100 == 1 ? "" : d[q100]);
                sb2.Append("百");
            }

            if (q10 > 0)
            {
                sb2.Append(q10 == 1 ? "" : d[q10]);
                sb2.Append("十");
            }

            if (q1 > 0)
            {
                sb2.Append(d[q1]);
            }

            return sb2.ToString();
        }
    }

    public static string CircledNumber(int n)
    {
        return n switch
        {
            >= 1 and <= 20 => ((char)('①' + n - 1)).ToString(),
            >= 21 and <= 35 => ((char)('㉑' + n - 21)).ToString(),
            >= 36 and <= 50 => ((char)('㊱' + n - 36)).ToString(),
            _ => n.ToString()
        };
    }

    private static (int Value, int Length) TryMatch(
        string s, int pos, (string Reading, int Value)[] table)
    {
        foreach (var entry in table)
        {
            if (StartsWith(s, pos, entry.Reading))
            {
                return (entry.Value, entry.Reading.Length);
            }
        }

        return (0, 0);
    }

    private static bool StartsWith(string s, int pos, string sub)
    {
        if (pos + sub.Length > s.Length)
        {
            return false;
        }

        for (var i = 0; i < sub.Length; i++)
        {
            if (s[pos + i] != sub[i])
            {
                return false;
            }
        }

        return true;
    }
}