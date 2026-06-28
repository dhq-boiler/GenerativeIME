namespace GenerativeIME.Core;

public interface IConversionEngine
{
    Task<IReadOnlyList<ConversionCandidate>> ConvertAsync(
        ConversionContext context,
        int maxCandidates = 6,
        CancellationToken cancellationToken = default);
}

public sealed class ConversionTrace
{
    public List<string> ContextWords { get; } = new();
    public List<(string Word, string Reading, bool Hit)> ContextWordReadings { get; } = new();
    public string PromptSent { get; set; } = "";
    public string RawModelOutput { get; set; } = "";
    public List<string> ParsedCandidates { get; } = new();
    public List<string> AfterJunkFilter { get; } = new();
    public List<(string Text, string Reading, bool Pass)> ReadingCheckDetails { get; } = new();
    public List<string> FinalOutput { get; } = new();
    public bool ReaderAvailable { get; set; }
}
