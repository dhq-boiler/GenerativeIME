using System.Text;
using System.Text.Json;

namespace GenerativeIME.Core;

public sealed class OllamaConversionEngine : IConversionEngine
{
    private readonly OllamaClient _client;
    private readonly string _model;
    private readonly JapaneseReader? _reader;

    public OllamaConversionEngine(OllamaClient client, string model, JapaneseReader? reader = null)
    {
        _client = client;
        _model = model;
        _reader = reader;
    }

    public ConversionTrace? LastTrace { get; private set; }

    public async Task<IReadOnlyList<ConversionCandidate>> ConvertAsync(
        ConversionContext context,
        int maxCandidates = 6,
        CancellationToken cancellationToken = default)
    {
        var trace = new ConversionTrace { ReaderAvailable = _reader is not null };

        // Built-in symbol/unit dictionary lookup. These bypass MeCab because MeCab does
        // not know readings for Unicode-compat symbols like ㎢ / ℃ / →.
        var symbolHits = SymbolDictionary.Lookup(context.Reading)
            .Select(s => new ConversionCandidate(s, "symbol"))
            .ToList();

        var contextWords = ExtractKanjiRuns(context.LineBeforeCursor)
            .Concat(ExtractKanjiRuns(context.LineAfterCursor))
            .Distinct()
            .ToList();
        trace.ContextWords.AddRange(contextWords);

        var contextHits = new List<ConversionCandidate>();
        if (_reader is not null)
        {
            foreach (var word in contextWords)
            {
                var actualReading = _reader.ToHiragana(word);
                var hit = _reader.MatchesReading(word, context.Reading);
                trace.ContextWordReadings.Add((word, actualReading, hit));
                if (hit)
                    contextHits.Add(new ConversionCandidate(word, "context-hit"));
            }
        }

        var prompt = BuildPrompt(context, maxCandidates, contextWords);
        trace.PromptSent = prompt;
        var raw = await _client.GenerateAsync(
            _model,
            prompt,
            format: "json",
            temperature: 0.2,
            numPredict: 256,
            keepAlive: "30m",
            think: false,
            cancellationToken: cancellationToken).ConfigureAwait(false);
        trace.RawModelOutput = raw;

        var parsed = ParseCandidates(raw, context.Reading);
        trace.ParsedCandidates.AddRange(parsed.Select(c => c.Text));
        var filtered = FilterObviousJunk(parsed, context.Reading);
        trace.AfterJunkFilter.AddRange(filtered.Select(c => c.Text));

        IReadOnlyList<ConversionCandidate> readingChecked;
        if (_reader is not null)
        {
            var list = new List<ConversionCandidate>(filtered.Count);
            foreach (var c in filtered)
            {
                var actual = _reader.ToHiragana(c.Text);
                var pass = _reader.MatchesReading(c.Text, context.Reading);
                trace.ReadingCheckDetails.Add((c.Text, actual, pass));
                if (pass) list.Add(c);
            }
            // Reading-mismatched candidates are dropped hard. If contextHits is also empty
            // the caller will fall back to the raw reading via the sandbox UI.
            readingChecked = list;
        }
        else
        {
            readingChecked = filtered;
        }

        var merged = MergeSymbolsContextHitsAndRest(symbolHits, contextHits, readingChecked);
        var deduped = Deduplicate(merged);
        var final = BoostByContextEcho(deduped, context);
        trace.FinalOutput.AddRange(final.Select(c => c.Text));
        LastTrace = trace;
        return final;
    }

    private static IReadOnlyList<ConversionCandidate> MergeSymbolsContextHitsAndRest(
        IReadOnlyList<ConversionCandidate> symbols,
        IReadOnlyList<ConversionCandidate> contextHits,
        IReadOnlyList<ConversionCandidate> rest)
    {
        if (symbols.Count == 0 && contextHits.Count == 0) return rest;
        var merged = new List<ConversionCandidate>(symbols.Count + contextHits.Count + rest.Count);
        merged.AddRange(contextHits);
        merged.AddRange(symbols);
        merged.AddRange(rest);
        return merged;
    }

    private IReadOnlyList<ConversionCandidate> FilterByReading(
        IReadOnlyList<ConversionCandidate> candidates, string reading)
    {
        if (_reader is null) return candidates;
        var list = new List<ConversionCandidate>(candidates.Count);
        foreach (var c in candidates)
        {
            if (_reader.MatchesReading(c.Text, reading))
                list.Add(c);
        }
        // If reader rejected everything, surface the candidate set as-is so the popup is not empty.
        return list.Count > 0 ? list : candidates;
    }

    private static IReadOnlyList<ConversionCandidate> Deduplicate(IReadOnlyList<ConversionCandidate> input)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var list = new List<ConversionCandidate>(input.Count);
        foreach (var c in input)
        {
            if (seen.Add(c.Text)) list.Add(c);
        }
        return list;
    }

    // Drop pure-hiragana candidates whose reading does not equal the input reading,
    // and drop candidates whose length is wildly off from a plausible conversion.
    private static IReadOnlyList<ConversionCandidate> FilterObviousJunk(
        IReadOnlyList<ConversionCandidate> candidates, string reading)
    {
        var maxLen = Math.Max(12, reading.Length + 4);
        var list = new List<ConversionCandidate>(candidates.Count);
        foreach (var c in candidates)
        {
            if (string.IsNullOrEmpty(c.Text)) continue;
            if (c.Text.Length > maxLen) continue;
            if (IsAllHiraganaOrPunct(c.Text) && c.Text != reading) continue;
            list.Add(c);
        }
        return list;
    }

    private static bool IsAllHiraganaOrPunct(string s)
    {
        foreach (var ch in s)
        {
            if (ch >= 0x3041 && ch <= 0x309F) continue; // hiragana
            if (ch == 'ー' || ch == '・' || ch == '、' || ch == '。') continue;
            return false;
        }
        return true;
    }

    private static List<string> ExtractKanjiRuns(string text)
    {
        var runs = new List<string>();
        if (string.IsNullOrEmpty(text)) return runs;

        var sb = new StringBuilder();
        foreach (var ch in text)
        {
            var isKanji = (ch >= 0x4E00 && ch <= 0x9FFF) ||
                          (ch >= 0x3400 && ch <= 0x4DBF);
            if (isKanji)
            {
                sb.Append(ch);
            }
            else
            {
                if (sb.Length > 0) { runs.Add(sb.ToString()); sb.Clear(); }
            }
        }
        if (sb.Length > 0) runs.Add(sb.ToString());
        return runs;
    }

    // Promote any candidate whose text already appears in the surrounding context
    // to the top while keeping the model's relative order otherwise.
    private static IReadOnlyList<ConversionCandidate> BoostByContextEcho(
        IReadOnlyList<ConversionCandidate> candidates,
        ConversionContext ctx)
    {
        if (candidates.Count <= 1) return candidates;
        var haystack = (ctx.LineBeforeCursor ?? "") + "\0" + (ctx.LineAfterCursor ?? "");
        if (haystack.Length == 0) return candidates;

        var inContext = new List<ConversionCandidate>();
        var others = new List<ConversionCandidate>();
        foreach (var c in candidates)
        {
            if (LooksContextual(c.Text) && haystack.Contains(c.Text, StringComparison.Ordinal))
                inContext.Add(c);
            else
                others.Add(c);
        }
        if (inContext.Count == 0) return candidates;

        var ordered = new List<ConversionCandidate>(candidates.Count);
        ordered.AddRange(inContext);
        ordered.AddRange(others);
        return ordered;
    }

    // Skip very-short / pure-hiragana strings to avoid trivial matches like "い" in "いい".
    private static bool LooksContextual(string text)
    {
        if (string.IsNullOrEmpty(text)) return false;
        if (text.Length < 2) return false;
        foreach (var ch in text)
        {
            if (ch >= 0x4E00 && ch <= 0x9FFF) return true; // CJK ideograph
            if (ch >= 0x30A0 && ch <= 0x30FF) return true; // katakana
        }
        return false;
    }

    private static string BuildPrompt(ConversionContext ctx, int max, IReadOnlyList<string> contextWords)
    {
        var sb = new StringBuilder();
        sb.AppendLine("あなたは日本語のかな漢字変換辞書です。");
        sb.AppendLine("入力された「読み」（ひらがな）を、一般的な漢字かな混じりの候補に変換します。");
        sb.AppendLine("文脈は一切考慮しません。読みだけ見て、国語辞典に載っている／日常的に使われる候補のみを返します。");
        sb.AppendLine();
        sb.AppendLine("# ルール");
        sb.AppendLine("1. text の読みが入力『読み』と完全一致すること（送り仮名で1文字の揺れは可）。一致しない候補は絶対に禁止。");
        sb.AppendLine("   例: 読みが「さいきどう」のとき「砕け度合い」(くだけどあい)は不一致なので禁止。");
        sb.AppendLine("2. 実在しない漢字熟語・造語は禁止。辞書語・パソコン用語・日常語のみ。");
        sb.AppendLine($"3. 候補は思いつく限り多めに（同音異義語・同訓異字・送り仮名違い・カタカナ表記を含む）、最大 {max} 件まで、よく使われる順。重複させない。");
        sb.AppendLine("4. JSON のみ。形式: {\"candidates\":[{\"text\":\"…\"}]}");
        sb.AppendLine();
        sb.AppendLine("# 例（候補は最大件数まで搾り出す）");
        sb.AppendLine("読み: \"きしゃ\" → {\"candidates\":[{\"text\":\"記者\"},{\"text\":\"貴社\"},{\"text\":\"汽車\"},{\"text\":\"帰社\"},{\"text\":\"喜捨\"}]}");
        sb.AppendLine("読み: \"はし\" → {\"candidates\":[{\"text\":\"橋\"},{\"text\":\"端\"},{\"text\":\"箸\"},{\"text\":\"梯\"}]}");
        sb.AppendLine("読み: \"さいきどう\" → {\"candidates\":[{\"text\":\"再起動\"},{\"text\":\"再気道\"},{\"text\":\"再軌道\"}]}");
        sb.AppendLine("読み: \"しゅくしゃく\" → {\"candidates\":[{\"text\":\"縮尺\"},{\"text\":\"縮図\"},{\"text\":\"縮酌\"}]}");
        sb.AppendLine("読み: \"しりょうかん\" → {\"candidates\":[{\"text\":\"資料館\"},{\"text\":\"飼料缶\"},{\"text\":\"史料館\"}]}");
        sb.AppendLine("読み: \"こうしょう\" → {\"candidates\":[{\"text\":\"交渉\"},{\"text\":\"高尚\"},{\"text\":\"工匠\"},{\"text\":\"考証\"},{\"text\":\"校章\"},{\"text\":\"鉱床\"}]}");
        sb.AppendLine();
        sb.AppendLine("# 今回の入力");
        sb.AppendLine("  読み: " + JsonEncode(ctx.Reading));
        sb.AppendLine();
        sb.AppendLine("# 出力");
        return sb.ToString();
    }

    private static readonly JsonSerializerOptions s_jsonOpts = new()
    {
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };
    private static string JsonEncode(string s) => JsonSerializer.Serialize(s, s_jsonOpts);

    private static IReadOnlyList<ConversionCandidate> ParseCandidates(string raw, string fallbackReading)
    {
        if (string.IsNullOrWhiteSpace(raw))
            return Array.Empty<ConversionCandidate>();

        try
        {
            using var doc = JsonDocument.Parse(raw);
            if (doc.RootElement.TryGetProperty("candidates", out var arr)
                && arr.ValueKind == JsonValueKind.Array)
            {
                var list = new List<ConversionCandidate>();
                foreach (var item in arr.EnumerateArray())
                {
                    string? text = null;
                    string? reason = null;
                    if (item.ValueKind == JsonValueKind.Object)
                    {
                        if (item.TryGetProperty("text", out var t) && t.ValueKind == JsonValueKind.String)
                            text = t.GetString();
                        if (item.TryGetProperty("reason", out var r) && r.ValueKind == JsonValueKind.String)
                            reason = r.GetString();
                    }
                    else if (item.ValueKind == JsonValueKind.String)
                    {
                        text = item.GetString();
                    }
                    if (string.IsNullOrEmpty(text)) continue;
                    list.Add(new ConversionCandidate(text, reason));
                }
                if (list.Count > 0) return list;
            }
        }
        catch (JsonException)
        {
        }

        return new[] { new ConversionCandidate(fallbackReading, "fallback") };
    }
}
