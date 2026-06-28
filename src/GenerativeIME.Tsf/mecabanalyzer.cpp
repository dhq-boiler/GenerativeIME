#include "mecabanalyzer.h"
#include "globals.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <mutex>
#include <mecab/mecab.h>

#pragma comment(lib, "Shlwapi.lib")

namespace
{
    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                            s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring FromUtf8(const char* p, size_t n)
    {
        if (!p || n == 0) return {};
        int w = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, nullptr, 0);
        std::wstring out(w, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, p, (int)n, out.data(), w);
        return out;
    }

    // Resolved next to our DLL — same pattern as SKK-JISYO.L.utf8. The post-
    // build event copies `third_party\mecab\unidic-lite\` into the output dir
    // so this just works locally and in the deployed install.
    std::wstring ResolveDictDir()
    {
        wchar_t dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInst, dllPath, MAX_PATH) == 0) return L"";
        PathRemoveFileSpecW(dllPath);
        std::wstring p = dllPath;
        p += L"\\unidic-lite";
        return p;
    }

    // Split a UniDic feature CSV. Naive but adequate: features never contain
    // raw commas inside quoted fields (UniDic uses `"` only around the *whole*
    // feature row for some entries, which we strip if present).
    std::vector<std::wstring> SplitFeatureCsv(const char* feature)
    {
        std::vector<std::wstring> out;
        if (!feature) return out;
        std::string raw(feature);
        size_t pos = 0;
        while (pos <= raw.size())
        {
            size_t comma = raw.find(',', pos);
            if (comma == std::string::npos)
            {
                out.push_back(FromUtf8(raw.data() + pos, raw.size() - pos));
                break;
            }
            out.push_back(FromUtf8(raw.data() + pos, comma - pos));
            pos = comma + 1;
        }
        return out;
    }

    std::once_flag g_once;
    MecabAnalyzer* g_instance = nullptr;
}

MecabAnalyzer* MecabAnalyzer::GetGlobal()
{
    std::call_once(g_once, []()
    {
        g_instance = new MecabAnalyzer();
        bool ok = g_instance->Init();
        wchar_t buf[300];
        swprintf_s(buf, L"[GenerativeIME] MecabAnalyzer.Init ready=%d\n", (int)ok);
        OutputDebugStringW(buf);
    });
    return g_instance;
}

bool MecabAnalyzer::Init()
{
    std::wstring dictDir = ResolveDictDir();
    if (dictDir.empty())
    {
        OutputDebugStringW(L"[GenerativeIME] MeCab: no dict dir\n");
        return false;
    }

    // Log what we resolved + whether sys.dic actually exists at that path
    // so we can disambiguate "wrong path" from "MeCab won't accept it".
    {
        wchar_t buf[1024];
        std::wstring sysdic = dictDir + L"\\sys.dic";
        DWORD attr = GetFileAttributesW(sysdic.c_str());
        swprintf_s(buf,
                   L"[GenerativeIME] MeCab dictDir=%s sys.dic exists=%d (attr=0x%08X)\n",
                   dictDir.c_str(), (int)(attr != INVALID_FILE_ATTRIBUTES), attr);
        OutputDebugStringW(buf);
    }

    // MeCab's argv-style API takes UTF-8 (vcpkg build is UTF-8 capable).
    // We supply -d (dict dir) and -r (rc file) explicitly; without -r MeCab
    // tries to read the OS-default mecabrc from registry / install dirs we
    // don't control.
    //
    // CRITICAL: avoid the string-overload `createModel(const char*)` — its
    // built-in tokenizer keeps the surrounding double-quotes as part of the
    // argument value, so `-r "C:/.../mecabrc"` ends up trying to open a
    // file literally named with the quotes in it. The (argc, argv) overload
    // bypasses that tokenizer entirely. Also convert backslashes to forward
    // slashes so MeCab's path-handling never has to think about escapes.
    std::string dictUtf8 = ToUtf8(dictDir);
    std::replace(dictUtf8.begin(), dictUtf8.end(), '\\', '/');
    std::string rcUtf8   = dictUtf8 + "/mecabrc";

    {
        std::string s = "[GenerativeIME] MeCab args: -d " + dictUtf8 +
                        " -r " + rcUtf8 + "\n";
        OutputDebugStringA(s.c_str());
    }

    // Build a real argv. Strings need to outlive the createModel call; we
    // own them on the stack so that's fine.
    std::string a0 = "GenerativeIME";
    std::string a1 = "-d";
    std::string a3 = "-r";
    char* argv[] = {
        a0.data(),
        a1.data(),
        dictUtf8.data(),
        a3.data(),
        rcUtf8.data(),
    };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));

    auto* model = MeCab::createModel(argc, argv);
    if (!model)
    {
        // MeCab's last-error string can be quite long (full path + reason).
        char ebuf[2048];
        const char* err = MeCab::getLastError();
        sprintf_s(ebuf, "[GenerativeIME] MeCab::createModel failed: %s\n",
                  err ? err : "(null)");
        OutputDebugStringA(ebuf);
        return false;
    }

    auto* tagger = model->createTagger();
    if (!tagger)
    {
        OutputDebugStringW(L"[GenerativeIME] MeCab createTagger failed\n");
        delete model;
        return false;
    }

    m_model  = model;
    m_tagger = tagger;
    m_ready  = true;

    wchar_t buf[400];
    swprintf_s(buf, L"[GenerativeIME] MeCab loaded, dict=%s\n", dictDir.c_str());
    OutputDebugStringW(buf);
    return true;
}

MecabAnalyzer::~MecabAnalyzer()
{
    if (m_tagger) { delete static_cast<MeCab::Tagger*>(m_tagger); m_tagger = nullptr; }
    if (m_model)  { delete static_cast<MeCab::Model*>(m_model);   m_model  = nullptr; }
}

std::vector<MecabMorpheme> MecabAnalyzer::Analyze(const std::wstring& text) const
{
    std::vector<MecabMorpheme> out;
    if (!m_ready || text.empty()) return out;

    auto* model  = static_cast<MeCab::Model*>(m_model);
    auto* tagger = static_cast<MeCab::Tagger*>(m_tagger);

    std::string input = ToUtf8(text);

    // Per-call Lattice so multiple threads can analyze concurrently against
    // the same shared Model/Tagger.
    MeCab::Lattice* lattice = model->createLattice();
    if (!lattice) return out;

    lattice->set_sentence(input.c_str());
    if (!tagger->parse(lattice))
    {
        delete lattice;
        return out;
    }

    for (const MeCab::Node* n = lattice->bos_node(); n; n = n->next)
    {
        if (n->stat == MECAB_BOS_NODE || n->stat == MECAB_EOS_NODE) continue;
        if (!n->surface || n->length == 0) continue;

        MecabMorpheme m;
        m.surface = FromUtf8(n->surface, n->length);

        // UniDic feature CSV layout (the version shipped by unidic-lite is
        // the "short" 17-field schema):
        //   0: 品詞
        //   1: 品詞細分類1
        //   2: 品詞細分類2
        //   3: 品詞細分類3
        //   4: 活用型
        //   5: 活用形
        //   6: 語彙素読み (katakana lemma)
        //   7: 語彙素      (lemma — the canonical kanji form we want)
        //   8: 書字形出現形 (orth)
        // We grab pos (0) and lemma (7). If the entry is an unknown / OOV
        // morpheme MeCab fills the feature with '*' placeholders; we fall
        // back to the surface as the lemma in that case so the bunsetsu
        // splitter still has something useful.
        auto fields = SplitFeatureCsv(n->feature);
        if (!fields.empty()) m.pos = fields[0];
        if (fields.size() > 7 && !fields[7].empty() && fields[7] != L"*")
            m.lemma = fields[7];
        else
            m.lemma = m.surface;

        // Field 6 is 語彙素読み (lemma reading) in katakana. Shift it down
        // to hiragana so it lines up character-for-character with the
        // kana-typed surface. KanjifyByReading uses this to find where
        // the lemma's kanji ends and its kana suffix begins.
        if (fields.size() > 6 && !fields[6].empty() && fields[6] != L"*")
        {
            std::wstring r = fields[6];
            for (auto& c : r)
            {
                if (c >= 0x30A1 && c <= 0x30F6) c -= 0x60;
            }
            m.lemmaReading = std::move(r);
        }

        // Field 9 is 発音形出現形 (surface pronunciation) in katakana,
        // using ー for long vowels. Convert to hiragana and expand ー
        // against the previous kana's vowel — IME users type "セイ"
        // ("せい") for 精, not "セー" ("せー"). The expansion table
        // below covers all 清音 / 濁音 / 半濁音 + small variants; OOV
        // kana fall through unchanged (still useful for the consonant
        // before, just won't get a long-vowel expansion).
        if (fields.size() > 9 && !fields[9].empty() && fields[9] != L"*")
        {
            std::wstring p = fields[9];
            for (auto& c : p)
            {
                if (c >= 0x30A1 && c <= 0x30F6) c -= 0x60;
            }
            auto vowelOf = [](wchar_t c) -> wchar_t
            {
                // Returns the IME-input vowel kana that follows this kana
                // when the user types it. 0 if not a 清/濁/半濁音 we know.
                // e列 → い, o列 → う (long-vowel convention in modern Japanese
                // IME input). a/i/u列 → same column.
                switch (c)
                {
                    // a列 — return あ
                    case L'あ': case L'か': case L'が': case L'さ': case L'ざ':
                    case L'た': case L'だ': case L'な': case L'は': case L'ば':
                    case L'ぱ': case L'ま': case L'や': case L'ら': case L'わ':
                    case L'ゃ': case L'ぁ':
                        return L'あ';
                    // i列 — return い
                    case L'い': case L'き': case L'ぎ': case L'し': case L'じ':
                    case L'ち': case L'ぢ': case L'に': case L'ひ': case L'び':
                    case L'ぴ': case L'み': case L'り': case L'ぃ':
                        return L'い';
                    // u列 — return う
                    case L'う': case L'く': case L'ぐ': case L'す': case L'ず':
                    case L'つ': case L'づ': case L'ぬ': case L'ふ': case L'ぶ':
                    case L'ぷ': case L'む': case L'ゆ': case L'る': case L'ゅ':
                    case L'ぅ':
                        return L'う';
                    // e列 — return い (long-vowel convention)
                    case L'え': case L'け': case L'げ': case L'せ': case L'ぜ':
                    case L'て': case L'で': case L'ね': case L'へ': case L'べ':
                    case L'ぺ': case L'め': case L'れ': case L'ぇ':
                        return L'い';
                    // o列 — return う (long-vowel convention)
                    case L'お': case L'こ': case L'ご': case L'そ': case L'ぞ':
                    case L'と': case L'ど': case L'の': case L'ほ': case L'ぼ':
                    case L'ぽ': case L'も': case L'よ': case L'ろ': case L'を':
                    case L'ょ': case L'ぉ':
                        return L'う';
                }
                return 0;
            };
            std::wstring out;
            out.reserve(p.size());
            wchar_t prev = 0;
            for (wchar_t c : p)
            {
                if (c == L'ー' && prev != 0)
                {
                    wchar_t v = vowelOf(prev);
                    if (v) { out.push_back(v); continue; }
                }
                out.push_back(c);
                prev = c;
            }
            m.pronunciation = std::move(out);
        }
        else
        {
            // OOV / missing field — fall back to surface. If surface itself
            // is pure kana the reading-match check still works; if it's
            // kanji we'll just fail the match (and the candidate gets
            // dropped, which is the safe default for Ollama suggestions).
            m.pronunciation = m.surface;
        }

        {
            wchar_t buf[400];
            swprintf_s(buf,
                       L"[GenerativeIME] mecab morpheme: surface=%s pos=%s lemma=%s reading=%s pron=%s fields=%zu\n",
                       m.surface.c_str(), m.pos.c_str(), m.lemma.c_str(),
                       m.lemmaReading.empty() ? L"(none)" : m.lemmaReading.c_str(),
                       m.pronunciation.empty() ? L"(none)" : m.pronunciation.c_str(),
                       fields.size());
            OutputDebugStringW(buf);
        }

        out.push_back(std::move(m));
    }

    delete lattice;
    return out;
}
