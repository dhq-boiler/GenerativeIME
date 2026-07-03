#include "ollamaclient.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring FromUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    // Escape a UTF-8 string for inclusion as a JSON string literal value (no surrounding quotes).
    std::string JsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                }
                else
                {
                    out += (char)c;
                }
            }
        }
        return out;
    }

    std::string BuildRequestBody(const ollama::GenerateOptions& opts)
    {
        std::string model  = JsonEscape(ToUtf8(opts.model));
        std::string prompt = JsonEscape(ToUtf8(opts.prompt));
        std::string keep   = JsonEscape(ToUtf8(opts.keepAlive));

        std::string body;
        body.reserve(prompt.size() + 256);
        body += "{";
        body += "\"model\":\"" + model + "\",";
        body += "\"prompt\":\"" + prompt + "\",";
        body += "\"stream\":false,";
        if (opts.jsonFormat) body += "\"format\":\"json\",";
        if (!keep.empty())   body += "\"keep_alive\":\"" + keep + "\",";
        body += "\"think\":";
        body += (opts.think ? "true" : "false");
        body += ",\"options\":{";

        char numbuf[64];
        snprintf(numbuf, sizeof(numbuf), "%.4f", opts.temperature);
        body += "\"temperature\":";
        body += numbuf;
        body += ",";
        snprintf(numbuf, sizeof(numbuf), "%d", opts.numPredict);
        body += "\"num_predict\":";
        body += numbuf;
        if (opts.numCtx > 0)
        {
            snprintf(numbuf, sizeof(numbuf), "%d", opts.numCtx);
            body += ",\"num_ctx\":";
            body += numbuf;
        }
        body += "}}";
        return body;
    }

    // Find the JSON string value for the given key in an UTF-8 JSON object.
    // Tolerates whitespace; decodes basic escapes (\n, \r, \t, \", \\, \uXXXX).
    // Not a real JSON parser — fine for Ollama's flat top-level response.
    bool ExtractJsonStringField(const std::string& body, const char* key, std::string& out)
    {
        std::string needle = "\"";
        needle += key;
        needle += "\"";
        size_t pos = body.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) pos++;
        if (pos >= body.size() || body[pos] != ':') return false;
        pos++;
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) pos++;
        if (pos >= body.size() || body[pos] != '"') return false;
        pos++;

        out.clear();
        while (pos < body.size())
        {
            char c = body[pos++];
            if (c == '"') return true;
            if (c == '\\' && pos < body.size())
            {
                char esc = body[pos++];
                switch (esc)
                {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                    if (pos + 4 <= body.size())
                    {
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++)
                        {
                            cp <<= 4;
                            char h = body[pos + i];
                            if      (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else return false;
                        }
                        pos += 4;
                        wchar_t w = (wchar_t)cp;
                        out += ToUtf8(std::wstring(1, w));
                    }
                    else return false;
                    break;
                default:
                    out += esc;
                }
            }
            else
            {
                out += c;
            }
        }
        return false; // unterminated string
    }
}

namespace ollama
{
    GenerateResult Generate(const GenerateOptions& opts)
    {
        GenerateResult result;

        HINTERNET hSession = WinHttpOpen(L"GenerativeIME/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) { result.hr = HRESULT_FROM_WIN32(GetLastError()); return result; }

        // Apply a unified timeout across DNS resolve / connect / send / receive
        // so a stuck Ollama daemon can't lock the IME thread indefinitely.
        WinHttpSetTimeouts(hSession, (int)opts.timeoutMs, (int)opts.timeoutMs,
                                     (int)opts.timeoutMs, (int)opts.timeoutMs);

        HINTERNET hConnect = WinHttpConnect(hSession, opts.host.c_str(),
            (INTERNET_PORT)opts.port, 0);
        if (!hConnect)
        {
            result.hr = HRESULT_FROM_WIN32(GetLastError());
            WinHttpCloseHandle(hSession);
            return result;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/generate",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hRequest)
        {
            result.hr = HRESULT_FROM_WIN32(GetLastError());
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        std::string body = BuildRequestBody(opts);
        const wchar_t* headers = L"Content-Type: application/json\r\n";

        BOOL ok = WinHttpSendRequest(hRequest, headers, (DWORD)-1L,
            (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
        if (!ok)
        {
            result.hr = HRESULT_FROM_WIN32(GetLastError());
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        ok = WinHttpReceiveResponse(hRequest, nullptr);
        if (!ok)
        {
            result.hr = HRESULT_FROM_WIN32(GetLastError());
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        result.httpStatus = statusCode;

        std::string raw;
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail))
            {
                result.hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            if (avail == 0) { result.hr = S_OK; break; }
            std::vector<char> buf(avail);
            DWORD got = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &got))
            {
                result.hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
            raw.append(buf.data(), got);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        result.rawBody = FromUtf8(raw);

        if (statusCode == 200 && SUCCEEDED(result.hr))
        {
            std::string respUtf8;
            if (ExtractJsonStringField(raw, "response", respUtf8))
            {
                result.response = FromUtf8(respUtf8);
            }
        }
        else if (SUCCEEDED(result.hr))
        {
            // 200 was expected; non-2xx is a protocol failure even if transport succeeded.
            result.hr = E_FAIL;
        }
        return result;
    }
}
