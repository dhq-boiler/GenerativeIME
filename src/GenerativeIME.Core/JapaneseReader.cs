using System.Text;
using NMeCab.Specialized;

namespace GenerativeIME.Core;

/// MeCab(IPA)で漢字混じり文をひらがな読みに正規化する。
/// インスタンスはスレッドセーフではないので呼び出し側で同期するか、1スレッド占有で使う。
public sealed class JapaneseReader : IDisposable
{
    private readonly MeCabIpaDicTagger _tagger;

    public JapaneseReader()
    {
        var dicDir = Path.Combine(AppContext.BaseDirectory, "IpaDic");
        _tagger = MeCabIpaDicTagger.Create(dicDir);
    }

    public void Dispose()
    {
        _tagger.Dispose();
    }

    public string ToHiragana(string text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return string.Empty;
        }

        var nodes = _tagger.Parse(text);
        var sb = new StringBuilder(text.Length * 2);
        foreach (var node in nodes)
        {
            var reading = node.Reading;
            if (!string.IsNullOrEmpty(reading))
            {
                AppendKatakanaAsHiragana(sb, reading);
            }
            else
            {
                sb.Append(node.Surface ?? string.Empty);
            }
        }

        return sb.ToString();
    }

    public bool MatchesReading(string candidateText, string targetHiragana, int slack = 1)
    {
        if (string.IsNullOrEmpty(candidateText))
        {
            return false;
        }

        if (string.IsNullOrEmpty(targetHiragana))
        {
            return false;
        }

        var actual = ToHiragana(candidateText);
        if (actual == targetHiragana)
        {
            return true;
        }

        if (slack <= 0)
        {
            return false;
        }

        // 送り仮名・促音の揺れを slack 文字まで許容（編集距離簡易版）
        return LevenshteinSmall(actual, targetHiragana, slack) <= slack;
    }

    private static void AppendKatakanaAsHiragana(StringBuilder sb, string kata)
    {
        foreach (var ch in kata)
        {
            if (ch >= 0x30A1 && ch <= 0x30F6)
            {
                sb.Append((char)(ch - 0x60));
            }
            else if (ch == 'ヵ')
            {
                sb.Append('ゕ');
            }
            else if (ch == 'ヶ')
            {
                sb.Append('ゖ');
            }
            else
            {
                sb.Append(ch);
            }
        }
    }

    // 簡易レーベンシュタイン。slack を超えたら早期打ち切り。
    private static int LevenshteinSmall(string a, string b, int max)
    {
        if (Math.Abs(a.Length - b.Length) > max)
        {
            return max + 1;
        }

        var prev = new int[b.Length + 1];
        var curr = new int[b.Length + 1];
        for (var j = 0; j <= b.Length; j++)
        {
            prev[j] = j;
        }

        for (var i = 1; i <= a.Length; i++)
        {
            curr[0] = i;
            var rowMin = curr[0];
            for (var j = 1; j <= b.Length; j++)
            {
                var cost = a[i - 1] == b[j - 1] ? 0 : 1;
                curr[j] = Math.Min(Math.Min(curr[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
                if (curr[j] < rowMin)
                {
                    rowMin = curr[j];
                }
            }

            if (rowMin > max)
            {
                return max + 1;
            }

            (prev, curr) = (curr, prev);
        }

        return prev[b.Length];
    }
}