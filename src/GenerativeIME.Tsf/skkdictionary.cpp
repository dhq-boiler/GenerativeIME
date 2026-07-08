#include "skkdictionary.h"
#include "globals.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <algorithm>
#include <fstream>
#include <mutex>

#pragma comment(lib, "Shlwapi.lib")

namespace
{
    std::wstring FromUtf8(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    // Path to the dictionary file. Resolved next to our DLL so the IME
    // works regardless of which user / app loaded us — AppData would force
    // a per-user copy and break system-wide deployment.
    std::wstring ResolveDictPath()
    {
        wchar_t dllPath[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInst, dllPath, MAX_PATH) == 0) return L"";
        PathRemoveFileSpecW(dllPath);
        std::wstring p = dllPath;
        p += L"\\SKK-JISYO.L.utf8";
        return p;
    }

    std::once_flag g_skkOnce;
    SkkDictionary* g_pSkk = nullptr;
}

std::wstring SkkDictionary::UserDictDir()
{
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        return L"";
    std::wstring dir = appData;
    CoTaskMemFree(appData);
    dir += L"\\GenerativeIME";
    CreateDirectoryW(dir.c_str(), nullptr);   // parent (shared with learning.txt)
    dir += L"\\dict";
    CreateDirectoryW(dir.c_str(), nullptr);   // the user-dictionary folder itself
    return dir;
}

std::vector<std::wstring> SkkDictionary::EnumerateUserDictFiles(const std::wstring& dir)
{
    std::vector<std::wstring> files;
    if (dir.empty()) return files;

    std::wstring pattern = dir + L"\\*.utf8";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    // Deterministic load order regardless of the filesystem's enumeration
    // order, so a reading present in two user dicts resolves the same way
    // every launch.
    std::sort(files.begin(), files.end());
    return files;
}

SkkDictionary* SkkDictionary::GetGlobal()
{
    std::call_once(g_skkOnce, []()
    {
        g_pSkk = new SkkDictionary();
        std::wstring path = ResolveDictPath();
        HRESULT hr = g_pSkk->Load(path);
        wchar_t buf[512];
        swprintf_s(buf,
                   L"[GenerativeIME] SkkDictionary.Load hr=0x%08X count=%zu path=%s\n",
                   (unsigned)hr, g_pSkk->EntryCount(), path.c_str());
        OutputDebugStringW(buf);
    });
    return g_pSkk;
}

HRESULT SkkDictionary::Load(const std::wstring& path)
{
    m_entries.clear();
    m_loaded = false;
    if (path.empty()) return E_INVALIDARG;

    // Defer okuri-ari stem entries (other than the i-adjective synthesis)
    // until we've finished the okuri-nashi section. SKK lists okuri-ari
    // FIRST in the file, so merging eagerly would put e.g. "見立" (from the
    // verb stem "みたt") in front of the okuri-nashi "みた /三田/見田/美田/"
    // — and our IME lookup of "みた" would surface "見立" as the top hit,
    // which we definitely don't want. Holding these aside and merging at
    // the end pushes them to the tail of each candidate list instead.
    std::unordered_map<std::wstring, std::vector<std::wstring>> deferredOkuri;

    // Resolve the containing directory once — used by both the pre-main
    // (godan) and post-main (emoji/loanwords) companion passes below.
    std::wstring dir;
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir = path.substr(0, slash + 1);
    }

    // Highest-priority pre-main pass: the user's own dictionaries, loaded
    // from %APPDATA%\GenerativeIME\dict\*.utf8. Any number of files coexist
    // and adding one is just dropping a file in the folder ("import"), with
    // no admin and no rebuild — the folder is user-writable and survives
    // reinstall (unlike the bundled dicts next to the DLL in Program Files).
    // Parsed before the bundled companions so an imported pick outranks
    // everything. Sorted filename order keeps cross-dict ties deterministic.
    //
    // NOTE: nothing personal ships in the repo/MSI — user coinages like
    // 「ふろった → 風呂った」 live only here, on the user's machine.
    for (const auto& f : EnumerateUserDictFiles(UserDictDir()))
        ParseFile(f, deferredOkuri, /*userDict=*/true);

    // Pre-main companion: hand-curated godan / ichidan 終止形 entries. Must
    // be parsed BEFORE the main dict so its verb candidates (買う/飼う/
    // 立つ/走る/…) land at the HEAD of each reading's slot. The main dict
    // then push_back's its noun/name candidates (斯う/龍/蛙/汁/…) to the
    // tail, which is exactly the ranking we want: SKK-JISYO.L organizes
    // ワ行五段 verbs under okuri-ari stems like `かw`, so a lookup of the
    // 終止形 reading `かう` from a modern IME (no SKK-style Shift-typing)
    // has no path back to 買う without this file. Optional: absence is
    // not an error.
    if (!dir.empty())
        ParseFile(dir + L"SKK-JISYO.godan.utf8", deferredOkuri);

    // Second pre-main companion: machine-generated conjugation-gap entries
    // (scripts/mine/mine_conjugation_gaps.ps1 — see the header inside the
    // file for the generation pipeline). Parsed AFTER godan so the hand-
    // curated entries keep the head on shared readings, and BEFORE the
    // main dict so the verb forms outrank SKK-JISYO.L's noun homophones.
    // Optional: absence is not an error.
    if (!dir.empty())
        ParseFile(dir + L"SKK-JISYO.conjugations.utf8", deferredOkuri);

    HRESULT hr = ParseFile(path, deferredOkuri);
    if (FAILED(hr)) return hr;

    // Generated companion dictionaries (scripts/mine/fetch_emoji_dict.ps1 /
    // fetch_loanword_dict.ps1) ride along from the same directory when
    // present. Parsed AFTER the main dict so their candidates merge onto
    // the TAIL of shared readings (いぬ → 犬 … 🐶, こんぴゅーたー →
    // コンピューター … computer), and their kana keys register as direct
    // okuri-nashi entries — which is exactly right: emoji and ASCII words
    // can never pass the MeCab ReadsAs filter, so they need the same
    // dict-maintainer-explicit bypass that 「こんにちわ /今日は/」 gets.
    // Optional: absence is not an error.
    if (!dir.empty())
    {
        static const wchar_t* kCompanions[] = {
            L"SKK-JISYO.propernouns.utf8",
            L"SKK-JISYO.emoji.utf8",
            L"SKK-JISYO.loanwords.utf8",
        };
        for (const wchar_t* name : kCompanions)
            ParseFile(dir + name, deferredOkuri);
    }

    FinalizeLoad(deferredOkuri);
    return S_OK;
}

HRESULT SkkDictionary::ParseFile(
    const std::wstring& path,
    std::unordered_map<std::wstring, std::vector<std::wstring>>& deferredOkuri,
    bool userDict)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    std::string raw;
    while (std::getline(f, raw))
    {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.empty() || raw[0] == ';') continue;       // comments / headers

        std::wstring line = FromUtf8(raw);

        // Layout: "<reading> /<cand1>/<cand2>/.../"
        size_t sp = line.find(L' ');
        if (sp == std::wstring::npos) continue;

        std::wstring reading = line.substr(0, sp);
        if (reading.empty()) continue;

        // SKK splits its dictionary into okuri-nasi (送り仮名なし) entries with
        // pure-hiragana readings, and okuri-ari entries whose reading ends in
        // a single ASCII letter denoting the conjugation stem (e.g. "あかk"
        // covers 赤k → 赤い/赤く/etc., "おくr" covers 送 r → 送る/送り/etc.).
        // For an IME's lookup-by-reading use case we don't have okuri info
        // from the composition, so we strip the trailing ASCII letter and
        // store the stem reading mapped to the candidate stems. The bunsetsu
        // splitter can then match "あか" -> "赤" and treat the following い/く
        // as a separate (hiragana) bunsetsu. Without this, common forms like
        // 赤い / 青い / 明るい / 送る are completely missing from the dictionary.
        wchar_t back = reading.back();
        bool okuriAri = (back < 128 && iswalpha((wint_t)back));
        wchar_t okuriCode = okuriAri ? back : L'\0';
        if (okuriAri) reading.pop_back();
        if (reading.empty()) continue;

        std::wstring rest = line.substr(sp + 1);
        if (rest.size() < 2 || rest.front() != L'/') continue;

        std::vector<std::wstring> cands;
        cands.reserve(4);
        size_t pos = 1;                                   // skip leading '/'
        while (pos < rest.size())
        {
            size_t next = rest.find(L'/', pos);
            if (next == std::wstring::npos) break;
            std::wstring cand = rest.substr(pos, next - pos);
            // Annotation: "<word>;<gloss>" — keep only the word.
            size_t semi = cand.find(L';');
            if (semi != std::wstring::npos) cand.resize(semi);
            // For okuri-ari, strip the same trailing ASCII letter from the
            // candidate so 赤k -> 赤. We don't validate that the suffix
            // actually matches the reading's stripped letter; a few rare
            // entries use a different letter as a separator but the kanji
            // half is what we want either way.
            if (okuriAri && !cand.empty())
            {
                wchar_t cb = cand.back();
                if (cb < 128 && iswalpha((wint_t)cb)) cand.pop_back();
            }
            if (!cand.empty()) cands.push_back(std::move(cand));
            pos = next + 1;
        }

        if (cands.empty()) continue;

        // For i-adjective stems (okuri code 'i'), synthesize the 終止形 form
        // and front-prepend it to m_entries["<stem>い"]. Without this, a
        // lookup of "あかい" matches the okuri-nashi entry "あかい /赤井/"
        // (the surname) and the adjective 赤い is unreachable because no
        // okuri-nashi entry exists for it. Doing it BEFORE merging the stem
        // into `slot` keeps the suffix-attachment loop iterating over the
        // freshly-read candidate list, not the (possibly already populated)
        // merged slot.
        if (okuriCode == L'i' && !cands.empty())
        {
            std::wstring fullReading = reading + L"い";
            auto& fullSlot = m_entries[fullReading];
            // Build "<cand>い" and insert at the FRONT of fullSlot. Reverse-
            // iterate over `cands` so a successive insert(begin(), …) keeps
            // the original cand order at the head of the vector.
            for (auto it = cands.rbegin(); it != cands.rend(); ++it)
            {
                std::wstring full = *it + L"い";
                // Dedupe against whatever's already in fullSlot.
                if (std::find(fullSlot.begin(), fullSlot.end(), full) == fullSlot.end())
                {
                    fullSlot.insert(fullSlot.begin(), std::move(full));
                }
            }
        }

        // Track okuri-nashi origin so HasDirectEntry can distinguish
        // dict-maintainer-explicit entries from okuri-ari flatten-through.
        // Called before the slot merge below so we mark the reading even
        // when candidate dedup drops the actual new candidate.
        if (!okuriAri) m_directReadings[reading] = 1;
        if (!okuriAri && userDict) m_userDictReadings[reading] = 1;

        // okuri-nashi entries merge directly into m_entries. okuri-ari
        // stems get held in deferredOkuri so they end up at the TAIL of
        // their candidate lists rather than the head.
        auto& slot = okuriAri ? deferredOkuri[reading] : m_entries[reading];
        for (auto& c : cands)
        {
            if (std::find(slot.begin(), slot.end(), c) == slot.end())
            {
                slot.push_back(std::move(c));
            }
        }
    }
    return S_OK;
}

void SkkDictionary::FinalizeLoad(
    std::unordered_map<std::wstring, std::vector<std::wstring>>& deferredOkuri)
{
    // Flush deferred okuri-ari stems to the end of each entry's list.
    // We ALSO keep them in m_okuri keyed by stem reading so the bunsetsu
    // splitter can reconstruct inflected alternates (ふ→[振,触,降] gives
    // us "降る" / "触る" candidates the okuri-nashi map alone can't).
    for (auto& kv : deferredOkuri)
    {
        auto& slot = m_entries[kv.first];
        for (const auto& c : kv.second)
        {
            if (std::find(slot.begin(), slot.end(), c) == slot.end())
            {
                slot.push_back(c);
            }
        }
        m_okuri[kv.first] = std::move(kv.second);
    }

    // Sorted key index for PredictCompletions' prefix-range scan. Built
    // AFTER the deferred-okuri flush above so every m_entries node exists;
    // no inserts happen past this point, so the key pointers stay valid.
    m_sortedReadings.clear();
    m_sortedReadings.reserve(m_entries.size());
    for (const auto& kv : m_entries) m_sortedReadings.push_back(&kv.first);
    std::sort(m_sortedReadings.begin(), m_sortedReadings.end(),
              [](const std::wstring* a, const std::wstring* b) { return *a < *b; });

    m_loaded = true;
}

std::vector<SkkDictionary::Prediction> SkkDictionary::PredictCompletions(
    const std::wstring& prefix, size_t maxResults) const
{
    std::vector<Prediction> out;
    if (!m_loaded || prefix.empty() || maxResults == 0) return out;

    auto it = std::lower_bound(
        m_sortedReadings.begin(), m_sortedReadings.end(), prefix,
        [](const std::wstring* a, const std::wstring& b) { return *a < b; });

    // Collect readings in the prefix range. The scan cap keeps a very common
    // 2-char prefix (しょ / かん / …) from walking thousands of long-tail
    // keys on every keystroke; lexicographic order means the cap biases
    // toward the front of the range, which is acceptable for prediction.
    constexpr size_t kScanCap = 3000;
    std::vector<const std::wstring*> hits;
    size_t scanned = 0;
    for (; it != m_sortedReadings.end() && scanned < kScanCap; ++it, ++scanned)
    {
        const std::wstring& key = **it;
        if (key.compare(0, prefix.size(), prefix) != 0) break;
        if (key.size() == prefix.size()) continue;  // exact hit → Space handles it
        if (m_directReadings.find(key) == m_directReadings.end()) continue;
        hits.push_back(*it);
    }

    // Nearest completion first: fewer remaining chars beats lexicographic
    // order (こんにちは above こんにちわ+α for prefix こんに). stable_sort
    // keeps the lexicographic order as the tie-break within a length.
    std::stable_sort(hits.begin(), hits.end(),
                     [](const std::wstring* a, const std::wstring* b)
                     { return a->size() < b->size(); });

    for (const auto* key : hits)
    {
        const auto& cands = m_entries.at(*key);
        if (cands.empty()) continue;
        const std::wstring& word = cands.front();
        bool dup = false;
        for (const auto& p : out)
        {
            if (p.word == word) { dup = true; break; }
        }
        if (dup) continue;
        out.push_back({ *key, word });
        if (out.size() >= maxResults) break;
    }
    return out;
}

std::vector<std::wstring> SkkDictionary::LookupOkuri(const std::wstring& stemReading) const
{
    auto it = m_okuri.find(stemReading);
    if (it == m_okuri.end()) return {};
    return it->second;
}

std::vector<std::wstring> SkkDictionary::Lookup(const std::wstring& reading) const
{
    auto it = m_entries.find(reading);
    if (it == m_entries.end()) return {};
    return it->second;
}

bool SkkDictionary::HasDirectEntry(const std::wstring& reading) const
{
    return m_directReadings.find(reading) != m_directReadings.end();
}

bool SkkDictionary::IsUserDictReading(const std::wstring& reading) const
{
    return m_userDictReadings.find(reading) != m_userDictReadings.end();
}

namespace
{
    // Conservative joshi list for bunsetsu-boundary detection. We DELIBERATELY
    // keep this short. Each char we add gives us cleaner sentence-end splits
    // ("いんだよ"→ん/だ/よ) but breaks any word that starts with it as a
    // morpheme ("たんとう"→た/んとう, "だんしゃく"→だ/んしゃく). The list
    // below sticks to chars whose word-initial frequency is low — case-
    // marking 助詞 (は/が/を/へ) and the most-common 終助詞 (よ/ね/わ/ぞ/ぜ).
    //
    // Deliberately EXCLUDED:
    //   に, で, と  — common word-initials (にほん, でんわ, とり, ...)
    //   も, や, か  — common word-initials (もの, やま, かさ, ...)
    //   ん, だ      — common mid-word kana, sentence ends like "んだ" survive
    //                  as kanji-substituted but at least don't shred 担当/段
    bool IsJoshiChar(wchar_t c)
    {
        static const std::wstring kJoshi = L"はがをへよねわぞぜ";
        return kJoshi.find(c) != std::wstring::npos;
    }
}

SkkDictionary::PrefixMatch SkkDictionary::FindLongestPrefix(
    const std::wstring& reading, size_t start) const
{
    if (start >= reading.size()) return {};

    // Rule (a): joshi position → forced 1-char hiragana bunsetsu. We don't
    // even check the dictionary, because "は" → 葉/歯/羽 is exactly the
    // mis-substitution we want to prevent.
    if (IsJoshiChar(reading[start]))
    {
        PrefixMatch m;
        m.length     = 1;
        m.candidates = { reading.substr(start, 1) };
        return m;
    }

    // Rule (b): cap the search length at the next joshi position (exclusive).
    // Stops "あしたは…" from collapsing into a single match.
    size_t maxLen = reading.size() - start;
    for (size_t i = 1; i < maxLen; ++i)
    {
        if (IsJoshiChar(reading[start + i])) { maxLen = i; break; }
    }

    // Linear scan from longest candidate length down. Each lookup is O(1).
    for (size_t len = maxLen; len >= 1; --len)
    {
        auto it = m_entries.find(reading.substr(start, len));
        if (it != m_entries.end())
        {
            PrefixMatch m;
            m.length     = len;
            m.candidates = it->second;
            return m;
        }
    }
    return {};
}
