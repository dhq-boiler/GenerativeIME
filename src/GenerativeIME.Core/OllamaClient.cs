using System.Net.Http.Json;
using System.Text.Json.Serialization;

namespace GenerativeIME.Core;

public sealed class OllamaClient : IDisposable
{
    private readonly Uri _baseUri;
    private readonly HttpClient _http;
    private readonly bool _ownsHttp;

    public OllamaClient(Uri? baseUri = null, HttpClient? http = null)
    {
        _baseUri = baseUri ?? new Uri("http://127.0.0.1:11434");
        if (http is null)
        {
            _http = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
            _ownsHttp = true;
        }
        else
        {
            _http = http;
            _ownsHttp = false;
        }
    }

    public void Dispose()
    {
        if (_ownsHttp)
        {
            _http.Dispose();
        }
    }

    public async Task<string> GenerateAsync(
        string model,
        string prompt,
        string? format = null,
        double? temperature = null,
        int? numPredict = null,
        string? keepAlive = null,
        bool? think = null,
        CancellationToken cancellationToken = default)
    {
        GenerateOptions? options = null;
        if (temperature.HasValue || numPredict.HasValue)
        {
            options = new GenerateOptions();
            if (temperature.HasValue)
            {
                options.Temperature = temperature.Value;
            }

            if (numPredict.HasValue)
            {
                options.NumPredict = numPredict.Value;
            }
        }

        var req = new GenerateRequest
        {
            Model = model,
            Prompt = prompt,
            Stream = false,
            Format = format,
            KeepAlive = keepAlive,
            Think = think,
            Options = options
        };

        using var res = await _http.PostAsJsonAsync(
            new Uri(_baseUri, "/api/generate"),
            req,
            cancellationToken).ConfigureAwait(false);
        res.EnsureSuccessStatusCode();

        var body = await res.Content.ReadFromJsonAsync<GenerateResponse>(
            cancellationToken).ConfigureAwait(false);
        return body?.Response ?? string.Empty;
    }

    private sealed class GenerateRequest
    {
        [JsonPropertyName("model")] public string Model { get; set; } = "";
        [JsonPropertyName("prompt")] public string Prompt { get; set; } = "";
        [JsonPropertyName("stream")] public bool Stream { get; set; }

        [JsonPropertyName("format")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public string? Format { get; set; }

        [JsonPropertyName("keep_alive")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public string? KeepAlive { get; set; }

        [JsonPropertyName("think")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public bool? Think { get; set; }

        [JsonPropertyName("options")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
        public GenerateOptions? Options { get; set; }
    }

    private sealed class GenerateOptions
    {
        [JsonPropertyName("temperature")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public double Temperature { get; set; }

        [JsonPropertyName("num_predict")]
        [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
        public int NumPredict { get; set; }
    }

    private sealed class GenerateResponse
    {
        [JsonPropertyName("response")] public string Response { get; set; } = "";
    }
}