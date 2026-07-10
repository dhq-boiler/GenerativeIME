using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Input;
using GenerativeIME.Core;

namespace GenerativeIME.Sandbox;

public partial class MainWindow : Window
{
    private const int MaxContextChars = 160;
    private string _compHira = string.Empty;
    private CancellationTokenSource? _convertCts;
    private bool _popupOpen;
    private JapaneseReader? _reader;
    private string _romajiBuf = string.Empty;

    public MainWindow()
    {
        InitializeComponent();
        UpdateStatus("Ready");
        Loaded += (_, _) =>
        {
            Editor.Focus();
            try
            {
                _reader = new JapaneseReader();
                var probe = _reader.ToHiragana("資料館");
                UpdateStatus($"Ready (MeCab OK: 資料館→{probe})");
            }
            catch (Exception ex)
            {
                UpdateStatus($"MeCab load failed: {ex.Message}");
            }
        };
        Closed += (_, _) => _reader?.Dispose();
    }

    private void Editor_OnPreviewTextInput(object sender, TextCompositionEventArgs e)
    {
        if (_popupOpen)
        {
            e.Handled = true;
            return;
        }

        var text = e.Text;
        if (string.IsNullOrEmpty(text))
        {
            return;
        }

        var ch = text[0];

        // ASCII letters / hyphen → romaji → ひらがな パイプライン
        if (IsRomajiChar(ch))
        {
            _romajiBuf += ch;
            FlushRomaji();
            e.Handled = true;
            return;
        }

        // 半角数字 → ローマ字テーブルを介さず、そのまま composing buffer に追加。
        // "ろーま5" → Ⅴ のように、変換対象キーの一部として扱う。
        if (ch >= '0' && ch <= '9')
        {
            FlushTrailingN();
            _compHira += ch;
            UpdateCompDisplay();
            e.Handled = true;
            return;
        }

        // any other typed char → commit composing first, then let textbox handle
        CommitComposing();
    }

    private void Editor_OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        // Popup active: navigation/commit/cancel handled here
        if (_popupOpen)
        {
            switch (e.Key)
            {
                case Key.Down:
                case Key.Space:
                case Key.Tab:
                    MoveCandidate(+1);
                    e.Handled = true;
                    return;
                case Key.Up:
                case Key.Back:
                    MoveCandidate(-1);
                    e.Handled = true;
                    return;
                case Key.Enter:
                    AcceptSelectedCandidate();
                    e.Handled = true;
                    return;
                case Key.Escape:
                    ClosePopupKeepingHira();
                    e.Handled = true;
                    return;
                case Key.D1:
                case Key.D2:
                case Key.D3:
                case Key.D4:
                case Key.D5:
                case Key.D6:
                case Key.D7:
                case Key.D8:
                case Key.D9:
                    var idx = e.Key - Key.D1;
                    if (idx < CandidateList.Items.Count)
                    {
                        CandidateList.SelectedIndex = idx;
                        AcceptSelectedCandidate();
                    }

                    e.Handled = true;
                    return;
            }

            e.Handled = true;
            return;
        }

        switch (e.Key)
        {
            case Key.Space:
                if (_compHira.Length > 0 || _romajiBuf.Length > 0)
                {
                    FlushTrailingN();
                    _ = StartConvertAsync();
                    e.Handled = true;
                }

                return;

            case Key.Enter:
                if (_compHira.Length > 0 || _romajiBuf.Length > 0)
                {
                    CommitComposing();
                    e.Handled = true;
                }

                return;

            case Key.Back:
                if (_romajiBuf.Length > 0)
                {
                    _romajiBuf = _romajiBuf[..^1];
                    UpdateCompDisplay();
                    e.Handled = true;
                    return;
                }

                if (_compHira.Length > 0)
                {
                    _compHira = _compHira[..^1];
                    UpdateCompDisplay();
                    e.Handled = true;
                }

                return;

            case Key.Escape:
                if (_compHira.Length > 0 || _romajiBuf.Length > 0)
                {
                    _compHira = string.Empty;
                    _romajiBuf = string.Empty;
                    UpdateCompDisplay();
                    e.Handled = true;
                }

                return;
        }
    }

    private void FlushRomaji()
    {
        var (hira, remaining) = RomajiToHiragana.Convert(_romajiBuf);
        _compHira += hira;
        _romajiBuf = remaining;
        UpdateCompDisplay();
    }

    private void FlushTrailingN()
    {
        if (_romajiBuf == "n")
        {
            _compHira += "ん";
            _romajiBuf = string.Empty;
        }

        UpdateCompDisplay();
    }

    private void UpdateCompDisplay()
    {
        CompText.Text = _compHira;
        RomajiText.Text = _romajiBuf.Length > 0 ? $"[{_romajiBuf}]" : string.Empty;
    }

    private void CommitComposing()
    {
        FlushTrailingN();
        if (_compHira.Length == 0)
        {
            return;
        }

        InsertAtCursor(_compHira);
        _compHira = string.Empty;
        _romajiBuf = string.Empty;
        UpdateCompDisplay();
    }

    private void InsertAtCursor(string s)
    {
        var pos = Editor.CaretIndex;
        Editor.Text = Editor.Text.Insert(pos, s);
        Editor.CaretIndex = pos + s.Length;
    }

    private async Task StartConvertAsync()
    {
        if (_compHira.Length == 0)
        {
            return;
        }

        _convertCts?.Cancel();
        _convertCts = new CancellationTokenSource();
        var ct = _convertCts.Token;

        var (before, after) = GetCurrentLineSplitAroundCursor();
        var reading = _compHira;
        var model = (ModelBox.Text ?? "").Trim();
        var endpoint = (EndpointBox.Text ?? "").Trim();
        var max = int.TryParse(MaxBox.Text, out var m) ? Math.Clamp(m, 1, 9) : 6;

        UpdateStatus($"変換中…  reading=「{reading}」");
        var sw = Stopwatch.StartNew();

        try
        {
            using var client = new OllamaClient(new Uri(endpoint));
            var engine = new OllamaConversionEngine(client, model, _reader);
            var ctx = new ConversionContext(before, after, reading);
            var list = await engine.ConvertAsync(ctx, max, ct);

            sw.Stop();
            if (ct.IsCancellationRequested)
            {
                return;
            }

            ShowCandidates(list, reading);
            DumpTrace(engine.LastTrace, ctx, sw.ElapsedMilliseconds, model);
            UpdateStatus($"候補 {list.Count} 件  {sw.ElapsedMilliseconds} ms  model={model}  log={GetLogPath()}");
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            sw.Stop();
            UpdateStatus($"エラー: {ex.Message}");
        }
    }

    private (string before, string after) GetCurrentLineSplitAroundCursor()
    {
        var rawText = Editor.Text ?? "";
        var rawCaret = Math.Clamp(Editor.CaretIndex, 0, rawText.Length);

        // Normalize CRLF / CR to LF so paragraph-break detection sees one newline char per break.
        var (text, caret) = NormalizeNewlines(rawText, rawCaret);

        var before = ExpandBackwardToNearestParagraph(text, caret);
        var after = ExpandForwardToNearestParagraph(text, caret);
        return (before, after);
    }

    private static (string text, int caret) NormalizeNewlines(string raw, int rawCaret)
    {
        var sb = new StringBuilder(raw.Length);
        var newCaret = rawCaret;
        for (var i = 0; i < raw.Length; i++)
        {
            var c = raw[i];
            if (c == '\r')
            {
                sb.Append('\n');
                if (i + 1 < raw.Length && raw[i + 1] == '\n')
                {
                    if (rawCaret > i + 1)
                    {
                        newCaret--;
                    }

                    i++;
                }
            }
            else
            {
                sb.Append(c);
            }
        }

        return (sb.ToString(), newCaret);
    }

    // Walk backward through any number of paragraph breaks, taking up to MaxContextChars
    // of reachable text. We want as much surrounding signal as fits in the budget — the
    // model can ignore irrelevant parts, but missing relevant ones silently breaks lookup.
    private static string ExpandBackwardToNearestParagraph(string text, int caret)
    {
        var scan = caret;
        while (scan > 0 && IsContextWhitespace(text[scan - 1]))
        {
            scan--;
        }

        if (scan == 0)
        {
            return string.Empty;
        }

        var contentEnd = scan;
        var start = Math.Max(0, contentEnd - MaxContextChars);
        return text.Substring(start, contentEnd - start);
    }

    private static string ExpandForwardToNearestParagraph(string text, int caret)
    {
        var scan = caret;
        while (scan < text.Length && IsContextWhitespace(text[scan]))
        {
            scan++;
        }

        if (scan == text.Length)
        {
            return string.Empty;
        }

        var contentStart = scan;
        var end = Math.Min(text.Length, contentStart + MaxContextChars);
        return text.Substring(contentStart, end - contentStart);
    }

    private static bool IsNewline(char c)
    {
        return c == '\n' || c == '\r';
    }

    private static bool IsContextWhitespace(char c)
    {
        return IsNewline(c) || c == ' ' || c == '\t' || c == '　';
    }

    private void ShowCandidates(IReadOnlyList<ConversionCandidate> list, string reading)
    {
        CandidateList.Items.Clear();
        var i = 1;
        foreach (var c in list)
        {
            CandidateList.Items.Add(new { Index = i, c.Text });
            i++;
        }

        // Always include raw hiragana as the last fallback
        CandidateList.Items.Add(new { Index = i, Text = reading });

        if (CandidateList.Items.Count == 0)
        {
            return;
        }

        CandidateList.SelectedIndex = 0;

        var rect = Editor.GetRectFromCharacterIndex(Editor.CaretIndex);
        CandidatePopup.HorizontalOffset = rect.Left;
        CandidatePopup.VerticalOffset = rect.Bottom + 2;
        CandidatePopup.IsOpen = true;
        _popupOpen = true;
    }

    private void MoveCandidate(int delta)
    {
        if (CandidateList.Items.Count == 0)
        {
            return;
        }

        var n = CandidateList.Items.Count;
        var i = CandidateList.SelectedIndex + delta;
        if (i < 0)
        {
            i = n - 1;
        }

        if (i >= n)
        {
            i = 0;
        }

        CandidateList.SelectedIndex = i;
        CandidateList.ScrollIntoView(CandidateList.SelectedItem);
    }

    private void AcceptSelectedCandidate()
    {
        if (CandidateList.SelectedItem is null)
        {
            return;
        }

        var t = CandidateList.SelectedItem.GetType().GetProperty("Text")
            ?.GetValue(CandidateList.SelectedItem) as string;
        if (string.IsNullOrEmpty(t))
        {
            return;
        }

        InsertAtCursor(t);
        _compHira = string.Empty;
        _romajiBuf = string.Empty;
        UpdateCompDisplay();
        ClosePopup();
    }

    private void ClosePopup()
    {
        CandidatePopup.IsOpen = false;
        _popupOpen = false;
    }

    private void ClosePopupKeepingHira()
    {
        ClosePopup();
        UpdateStatus("キャンセル。未確定を維持");
    }

    private void CandidateList_OnMouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        AcceptSelectedCandidate();
    }

    private void UpdateStatus(string s)
    {
        StatusText.Text = s;
    }

    private static bool IsRomajiChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
    }

    private static string GetLogPath()
    {
        return Path.Combine(Path.GetTempPath(), "GenerativeIME.trace.log");
    }

    private static void DumpTrace(ConversionTrace? t, ConversionContext ctx, long ms, string model)
    {
        if (t is null)
        {
            return;
        }

        try
        {
            var sb = new StringBuilder();
            sb.AppendLine($"=== {DateTime.Now:HH:mm:ss.fff}  reading=「{ctx.Reading}」  {ms}ms  model={model} ===");
            sb.AppendLine($"reader_available: {t.ReaderAvailable}");
            sb.AppendLine($"LineBefore: {ctx.LineBeforeCursor.Replace("\n", "\\n")}");
            sb.AppendLine($"LineAfter : {ctx.LineAfterCursor.Replace("\n", "\\n")}");
            sb.AppendLine($"context_words ({t.ContextWords.Count}): [{string.Join(", ", t.ContextWords)}]");
            sb.AppendLine("context_word_readings:");
            foreach (var (w, r, h) in t.ContextWordReadings)
            {
                sb.AppendLine($"  {w} -> {r}  hit={h}");
            }

            sb.AppendLine($"--- PROMPT ---\n{t.PromptSent}\n--- /PROMPT ---");
            sb.AppendLine($"raw_model_output: {t.RawModelOutput}");
            sb.AppendLine($"parsed: [{string.Join(", ", t.ParsedCandidates)}]");
            sb.AppendLine($"after_junk: [{string.Join(", ", t.AfterJunkFilter)}]");
            sb.AppendLine("reading_check:");
            foreach (var (text, r, p) in t.ReadingCheckDetails)
            {
                sb.AppendLine($"  {text} -> {r}  pass={p}");
            }

            sb.AppendLine($"final: [{string.Join(", ", t.FinalOutput)}]");
            sb.AppendLine();
            File.AppendAllText(GetLogPath(), sb.ToString());
        }
        catch
        {
        }
    }
}