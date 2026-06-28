#pragma once

#include <windows.h>
#include <string>

// Minimal port of GenerativeIME.Core.OllamaClient. Synchronous HTTP POST
// to /api/generate on the local Ollama daemon. Designed for IME use: the
// caller blocks waiting for the candidate list, so timeouts are short.
//
// For now the caller composes its own prompt; this just sends/receives.
namespace ollama
{
    struct GenerateOptions
    {
        std::wstring model;             // e.g. L"qwen2.5:3b-instruct"
        std::wstring prompt;            // full user prompt
        bool         jsonFormat = true; // sets format=json
        double       temperature = 0.2;
        int          numPredict = 256;
        std::wstring keepAlive  = L"30m";
        bool         think      = false;
        std::wstring host       = L"127.0.0.1";
        int          port       = 11434;
        DWORD        timeoutMs  = 30000; // total connect+send+receive
    };

    struct GenerateResult
    {
        HRESULT      hr           = E_FAIL;
        DWORD        httpStatus   = 0;     // HTTP response status, 0 on transport error
        std::wstring response;              // "response" field from Ollama JSON; empty on failure
        std::wstring rawBody;               // full response body for diagnostics
    };

    GenerateResult Generate(const GenerateOptions& opts);
}
