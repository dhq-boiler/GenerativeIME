// conjgap_miner — 活用形ギャップの網羅調査ツール
//
// SKK-JISYO.L の okuri-ari エントリから動詞・形容詞の終止形を再構成し、
// 活用クラス (UniDic cType) ごとに活用形を機械生成、生成した
// (読み, 表記) ペアを IME の決定的変換パス (textservice.cpp の
// SKK 直接ヒット → MergeMecabVerbForms / SplitMecab 結合形) と同じ
// ロジックでプローブして、誤変換になる読みを洗い出す。
//
// 出力:
//   <outdir>/conj_auto.utf8   — 自動追加して安全なギャップ (SKK 形式行)。
//                               現在の出力がかな素通し / LooksSuspect /
//                               ReadsAs 不成立 (=読みが一致しないガベージ)
//                               のものだけ。
//   <outdir>/conj_review.tsv  — 要レビュー (現在の出力も正当な競合解釈)。
//                               bucket \t reading \t current \t expected...
//   stdout                    — 統計サマリ。
//
// ビルド/実行: scripts/mine/mine_conjugation_gaps.ps1 参照。
// 検証パイプラインの背景は memory: misconversion-tuning-workflow を参照。

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

#include "mecabanalyzer.h"
#include "skkdictionary.h"
#include "bunsetsu.h"
#include "modernranking.h"

HINSTANCE g_hInst = nullptr;  // ResolveDictPath: nullptr → exe と同じディレクトリ

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

bool HasKanji(const std::wstring& s)
{
    for (wchar_t c : s)
        if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF)) return true;
    return false;
}

bool IsPureHiragana(const std::wstring& s)
{
    if (s.empty()) return false;
    for (wchar_t c : s)
        if (c < 0x3041 || c > 0x309F) return false;
    return true;
}

// ---------------------------------------------------------------------
// 1. SKK-JISYO.L okuri-ari セクションの読み込み
// ---------------------------------------------------------------------
struct OkuriEntry
{
    std::wstring stemReading;   // 送り仮名を除いた読み ("まよ")
    wchar_t      letter;        // 送り仮名頭文字 ('u' 等)
    std::vector<std::wstring> stems;  // 漢字語幹 (SKK 記載順)
};

std::vector<OkuriEntry> LoadOkuriAri(const std::wstring& path)
{
    std::vector<OkuriEntry> out;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return out;
    std::string line;
    bool inOkuriAri = false;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(";; okuri-ari entries", 0) == 0) { inOkuriAri = true; continue; }
        if (line.rfind(";; okuri-nasi entries", 0) == 0) break;
        if (!inOkuriAri || line.empty() || line[0] == ';') continue;

        size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        std::wstring key = FromUtf8(line.substr(0, sp));
        if (key.size() < 2) continue;
        wchar_t letter = key.back();
        if (letter < L'a' || letter > L'z') continue;

        OkuriEntry e;
        e.stemReading = key.substr(0, key.size() - 1);
        e.letter = letter;
        // 読みは純ひらがなのみ (ー を含む語幹は稀なので除外して安全側に)
        if (!IsPureHiragana(e.stemReading)) continue;

        // "/振/触;annotation/降/" → 候補列。注釈と非日本語混じりは捨てる。
        std::string body = line.substr(sp + 1);
        size_t pos = 0;
        while (true)
        {
            size_t a = body.find('/', pos);
            if (a == std::string::npos) break;
            size_t b = body.find('/', a + 1);
            if (b == std::string::npos) break;
            std::string cand = body.substr(a + 1, b - a - 1);
            size_t semi = cand.find(';');
            if (semi != std::string::npos) cand.resize(semi);
            std::wstring w = FromUtf8(cand);
            bool clean = !w.empty();
            for (wchar_t c : w)
                if (c < 0x80) { clean = false; break; }  // ASCII 混じり ((concat ...) 等) は除外
            if (clean &&
                std::find(e.stems.begin(), e.stems.end(), w) == e.stems.end())
                e.stems.push_back(std::move(w));
            pos = b;
        }
        if (!e.stems.empty()) out.push_back(std::move(e));
    }
    return out;
}

// ---------------------------------------------------------------------
// 2. 終止形の再構成と検証
// ---------------------------------------------------------------------
struct Verb
{
    std::wstring terminal;        // 終止形表記 (迷う)
    std::wstring terminalReading; // 終止形読み (まよう)
    std::wstring stem;            // 漢字語幹 (迷)
    std::wstring stemReading;     // 語幹読み (まよ)
    wchar_t      endKana;         // 終止形末尾かな (う)
    std::wstring cType;           // UniDic 活用型 (五段-ワア行 等)
    bool         isAdjective = false;
    size_t       srcOrder = 0;    // 入力順 (SKK の並びを頻度近似として保持)
};

// 表記 surface が読み reading で読め、先頭形態素の語彙素が expectedLemma
// であることの厳密チェック。ReadsAs と違い文末は/わ等の緩和はしない。
bool ValidatesAs(const MecabAnalyzer& mecab,
                 const std::wstring& surface,
                 const std::wstring& reading,
                 const std::wstring& expectedLemma)
{
    auto ms = mecab.Analyze(surface);
    if (ms.empty()) return false;
    std::wstring pron;
    for (const auto& m : ms)
        pron += (!m.pronunciation.empty() ? m.pronunciation : m.surface);
    if (pron != reading) return false;
    if (ms[0].pos != L"動詞" && ms[0].pos != L"形容詞") return false;
    return ms[0].lemma == expectedLemma;
}

// ---------------------------------------------------------------------
// 3. 活用形生成
// ---------------------------------------------------------------------
struct GenForm
{
    std::wstring reading;
    std::wstring surface;
    std::wstring expectedLemma;   // 検証時に要求する語彙素
    const wchar_t* formName;
};

struct GodanRow { wchar_t term, a, i, e, o; const wchar_t* onbinTa; };
const GodanRow* RowOf(wchar_t termKana)
{
    static const GodanRow rows[] = {
        { L'う', L'わ', L'い', L'え', L'お', L"った" },
        { L'く', L'か', L'き', L'け', L'こ', L"いた" },   // 行く だけ促音便 → 両方生成し検証で絞る
        { L'ぐ', L'が', L'ぎ', L'げ', L'ご', L"いだ" },
        { L'す', L'さ', L'し', L'せ', L'そ', L"した" },
        { L'つ', L'た', L'ち', L'て', L'と', L"った" },
        { L'ぬ', L'な', L'に', L'ね', L'の', L"んだ" },
        { L'ぶ', L'ば', L'び', L'べ', L'ぼ', L"んだ" },
        { L'む', L'ま', L'み', L'め', L'も', L"んだ" },
        { L'る', L'ら', L'り', L'れ', L'ろ', L"った" },
    };
    for (const auto& r : rows)
        if (r.term == termKana) return &r;
    return nullptr;
}

std::vector<GenForm> GenerateForms(const Verb& v)
{
    std::vector<GenForm> out;
    auto add = [&](const std::wstring& sufR, const std::wstring& sufS,
                   const std::wstring& lemma, const wchar_t* name) {
        out.push_back({ v.stemReading + sufR, v.stem + sufS, lemma, name });
    };
    auto ta2te = [](std::wstring ta) {   // った→って / んだ→んで / いた→いて / いだ→いで
        if (!ta.empty()) ta.back() = (ta.back() == L'だ') ? L'で' : L'て';
        return ta;
    };

    if (v.isAdjective)
    {
        // 語幹 = 終止形 − い
        add(L"かった",   L"かった",   v.terminal, L"adj-past");
        add(L"くない",   L"くない",   v.terminal, L"adj-neg");
        add(L"くなかった", L"くなかった", v.terminal, L"adj-neg-past");
        add(L"く",       L"く",       v.terminal, L"adj-adv");
        add(L"ければ",   L"ければ",   v.terminal, L"adj-cond");
        return out;
    }

    const bool godan = v.cType.rfind(L"五段", 0) == 0;
    const bool ichidan = v.cType.find(L"一段") != std::wstring::npos;

    if (godan)
    {
        const GodanRow* r = RowOf(v.endKana);
        if (!r) return out;
        std::wstring A(1, r->a), I(1, r->i), E(1, r->e), O(1, r->o);
        std::wstring ta = r->onbinTa;
        add(ta,             ta,             v.terminal, L"past");
        add(ta2te(ta),      ta2te(ta),      v.terminal, L"te");
        add(ta + L"ら",     ta + L"ら",     v.terminal, L"tara");
        if (v.endKana == L'く')
        {
            // 行く / 逝く の促音便。書く 等は「書った」が検証で落ちる。
            add(L"った",  L"った",  v.terminal, L"past");
            add(L"って",  L"って",  v.terminal, L"te");
            add(L"ったら", L"ったら", v.terminal, L"tara");
        }
        add(A + L"ない",     A + L"ない",     v.terminal, L"nai");
        add(A + L"なかった", A + L"なかった", v.terminal, L"nakatta");
        add(I + L"ます",     I + L"ます",     v.terminal, L"masu");
        add(I + L"ました",   I + L"ました",   v.terminal, L"mashita");
        add(E + L"ば",       E + L"ば",       v.terminal, L"ba");
        add(O + L"う",       O + L"う",       v.terminal, L"volitional");
        add(E,               E,               v.terminal, L"imperative");
        // 可能動詞は語彙素が独立 (買える) なので expectedLemma を差し替え
        add(E + L"る", E + L"る", v.stem + E + L"る", L"potential");
    }
    else if (ichidan)
    {
        // 終止形 = 語幹+る。stemReading / stem は る を除いた形で来る。
        add(L"た",       L"た",       v.terminal, L"past");
        add(L"て",       L"て",       v.terminal, L"te");
        add(L"たら",     L"たら",     v.terminal, L"tara");
        add(L"ない",     L"ない",     v.terminal, L"nai");
        add(L"なかった", L"なかった", v.terminal, L"nakatta");
        add(L"ます",     L"ます",     v.terminal, L"masu");
        add(L"ました",   L"ました",   v.terminal, L"mashita");
        add(L"れば",     L"れば",     v.terminal, L"ba");
        add(L"よう",     L"よう",     v.terminal, L"volitional");
        add(L"ろ",       L"ろ",       v.terminal, L"imperative");
    }
    return out;
}

// ---------------------------------------------------------------------
// 4. 決定的パスのエミュレーション (textservice.cpp:1331-1397 + Phase B)
// ---------------------------------------------------------------------
std::wstring DeterministicTop(const std::wstring& reading,
                              const MecabAnalyzer& mecab,
                              SkkDictionary* skk)
{
    auto hits = skk->Lookup(reading);
    if (!hits.empty() && !skk->HasDirectEntry(reading))
    {
        std::vector<std::wstring> clean;
        for (auto& c : hits)
            if (bunsetsu::ReadsAs(c, reading, mecab)) clean.push_back(std::move(c));
        hits = std::move(clean);
    }
    hits = modernranking::PromoteToTop(reading, std::move(hits));
    if (!hits.empty())
    {
        hits = bunsetsu::MergeMecabVerbForms(reading, mecab, hits);
        return hits.empty() ? reading : hits[0];
    }
    auto parts = bunsetsu::SplitMecab(reading, mecab, skk);
    if (parts.empty()) return reading;
    return bunsetsu::JoinSelected(parts);
}
} // namespace

// corpus/goldens/wikipedia-top-combined.tsv → 表記→頻度カウント。
// エントリ内の同音異表記を現代頻度順に並べるのに使う (SKK-L の okuri-ari
// 並びは頻度順ではない: いr /射/居/鋳/ の先頭は 射)。
std::map<std::wstring, long long> LoadCorpusCounts(const std::wstring& path)
{
    std::map<std::wstring, long long> out;
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::wstring kanji = FromUtf8(line.substr(t1 + 1, t2 - t1 - 1));
        long long cnt = atoll(line.c_str() + t2 + 1);
        auto it = out.find(kanji);
        if (it == out.end() || it->second < cnt) out[kanji] = cnt;
    }
    return out;
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3)
    {
        std::printf("usage: conjgap_miner <SKK-JISYO.L.utf8> <outdir> [maxVerbs] [corpus.tsv]\n");
        return 2;
    }
    std::wstring skkPath = FromUtf8(argv[1]);
    std::wstring outDir  = FromUtf8(argv[2]);
    size_t maxVerbs = (argc > 3) ? (size_t)atoi(argv[3]) : 0;
    std::map<std::wstring, long long> corpus;
    if (argc > 4) corpus = LoadCorpusCounts(FromUtf8(argv[4]));

    auto* mecab = MecabAnalyzer::GetGlobal();
    if (!mecab || !mecab->IsReady()) { std::printf("MeCab not ready\n"); return 1; }
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("SKK not loaded\n"); return 1; }

    // --- 1. okuri-ari 読み込み ---
    auto entries = LoadOkuriAri(skkPath);
    std::printf("okuri-ari entries: %zu\n", entries.size());

    // --- 2. 終止形の再構成 ---
    // 送り仮名頭文字 → 終止形候補の末尾かな
    static const std::pair<wchar_t, wchar_t> kTerminalKana[] = {
        { L'u', L'う' }, { L'k', L'く' }, { L'g', L'ぐ' }, { L's', L'す' },
        { L't', L'つ' }, { L'n', L'ぬ' }, { L'b', L'ぶ' }, { L'm', L'む' },
        { L'r', L'る' }, { L'i', L'い' },
    };
    std::vector<Verb> verbs;
    std::set<std::wstring> seenTerminals;
    size_t order = 0;
    for (const auto& e : entries)
    {
        wchar_t endKana = 0;
        for (const auto& tk : kTerminalKana)
            if (tk.first == e.letter) { endKana = tk.second; break; }
        if (!endKana) continue;

        // かな表記が支配的な基本動詞のパラダイムは生成しない。これらの
        // 活用読み (いない/あれば/ならない/…) に同音動詞の漢字直接エントリ
        // (射ない/有れば/成らない) を与えると、現代表記の大多数ケースを
        // 乗っ取る回帰になる (bunsetsu.cpp IsArchaicShortVerbLemma と同じ
        // 判断)。同音の実動詞 (射る/炒る/鳴る/呉れる) も巻き添えで外すが、
        // それらの活用形が既定で出ないのは現状維持であり回帰ではない。
        // ない (無い) は形容詞だが同じ扱い: なかった /無かった/ を直接
        // エントリにすると MergeAdjacentBySkk が「美しく+なかっ+た」を
        // 無かった に併合し、否定助動詞の全域が 無 化する回帰になる
        // (bunsetsu.cpp IsShadowedAuxiliary が守っている領域)。
        static const std::wstring kKanaPreferredTerminals[] = {
            L"する", L"いる", L"ある", L"なる", L"やる", L"くれる", L"しまう",
            L"ない",
        };
        {
            std::wstring terminalReading = e.stemReading + endKana;
            bool blocked = false;
            for (const auto& k : kKanaPreferredTerminals)
                if (terminalReading == k) { blocked = true; break; }
            if (blocked) continue;
        }

        for (const auto& stem : e.stems)
        {
            std::wstring surface = stem + endKana;
            std::wstring reading = e.stemReading + endKana;
            if (seenTerminals.count(surface)) continue;

            auto ms = mecab->Analyze(surface);
            if (ms.size() != 1) continue;
            const auto& m = ms[0];
            std::wstring pron = !m.pronunciation.empty() ? m.pronunciation : m.surface;
            if (pron != reading) continue;
            if (m.lemma != surface) continue;
            if (m.cType.empty()) continue;

            bool isAdj = (endKana == L'い');
            if (isAdj && m.pos != L"形容詞") continue;
            if (!isAdj && m.pos != L"動詞") continue;

            Verb v;
            v.terminal        = surface;
            v.terminalReading = reading;
            v.endKana         = endKana;
            v.cType           = m.cType;
            v.isAdjective     = isAdj;
            v.srcOrder        = order++;
            if (isAdj || m.cType.find(L"一段") != std::wstring::npos)
            {
                // 一段/形容詞: 語幹 = 終止形 − 末尾 1 かな
                v.stem        = surface.substr(0, surface.size() - 1);
                v.stemReading = reading.substr(0, reading.size() - 1);
            }
            else
            {
                v.stem        = stem;
                v.stemReading = e.stemReading;
            }
            seenTerminals.insert(surface);
            verbs.push_back(std::move(v));
            if (maxVerbs && verbs.size() >= maxVerbs) goto verbsDone;
        }
    }
verbsDone:
    size_t nAdj = 0;
    for (const auto& v : verbs) if (v.isAdjective) ++nAdj;
    std::printf("validated lexicon: %zu (verbs=%zu adjectives=%zu)\n",
                verbs.size(), verbs.size() - nAdj, nAdj);

    // --- 3. 活用形生成 + 検証、読みごとにグルーピング ---
    struct ExpectedSurface { std::wstring surface; std::wstring terminal; };
    struct Expected { std::vector<ExpectedSurface> surfaces; };
    std::map<std::wstring, Expected> byReading;
    size_t generated = 0, validated = 0;
    for (const auto& v : verbs)
    {
        for (const auto& f : GenerateForms(v))
        {
            ++generated;
            if (f.reading.size() < 2) continue;
            if (!ValidatesAs(*mecab, f.surface, f.reading, f.expectedLemma)) continue;
            ++validated;
            auto& ex = byReading[f.reading];
            bool dup = false;
            for (const auto& s : ex.surfaces)
                if (s.surface == f.surface) { dup = true; break; }
            if (!dup) ex.surfaces.push_back({ f.surface, v.terminal });
        }
    }
    std::printf("generated forms: %zu, validated: %zu, unique readings: %zu\n",
                generated, validated, byReading.size());

    // 同音異表記を終止形のコーパス頻度降順で安定ソート。頻度データが無い
    // 表記は 0 扱いで SKK-L の並び (挿入順) を保つ。いない→居ない が
    // 射ない より先に、あつく→熱く が 厚く より先に来るようにする。
    if (!corpus.empty())
    {
        for (auto& [reading, ex] : byReading)
        {
            std::stable_sort(ex.surfaces.begin(), ex.surfaces.end(),
                [&corpus](const ExpectedSurface& a, const ExpectedSurface& b) {
                    auto ca = corpus.find(a.terminal);
                    auto cb = corpus.find(b.terminal);
                    long long na = (ca != corpus.end()) ? ca->second : 0;
                    long long nb = (cb != corpus.end()) ? cb->second : 0;
                    return na > nb;
                });
        }
    }

    // --- 4. プローブ + 分類 ---
    std::ofstream fAuto(ToUtf8(outDir + L"\\conj_auto.utf8").c_str(), std::ios::binary);
    std::ofstream fReview(ToUtf8(outDir + L"\\conj_review.tsv").c_str(), std::ios::binary);
    size_t nOk = 0, nRank = 0, nAuto = 0, nReview = 0, done = 0;
    for (const auto& [reading, ex] : byReading)
    {
        if (++done % 5000 == 0)
            std::printf("  probed %zu/%zu...\n", done, byReading.size());

        std::wstring top = DeterministicTop(reading, *mecab, skk);
        if (top == ex.surfaces[0].surface) { ++nOk; continue; }
        bool inExpected = false;
        for (const auto& s : ex.surfaces)
            if (s.surface == top) { inExpected = true; break; }
        if (inExpected) { ++nRank; continue; }

        // 直接エントリが既にある読みは AUTO 対象外。辞書管理者が意図的に
        // 選んだ先頭 (「ました /ました/」のかなパススルー等) を、後勝ちの
        // ロード順で上書きしてしまうため。これらは REVIEW 送りにして
        // 人が判断する。
        bool autoSafe =
            reading.size() >= 3 &&
            !skk->HasDirectEntry(reading) &&
            (!HasKanji(top) ||
             bunsetsu::LooksSuspect(reading, *mecab) ||
             !bunsetsu::ReadsAs(top, reading, *mecab));

        std::wstring exList;
        for (const auto& s : ex.surfaces) { exList += L"/"; exList += s.surface; }
        exList += L"/";

        if (autoSafe)
        {
            ++nAuto;
            fAuto << ToUtf8(reading + L" " + exList + L"\n");
        }
        else
        {
            ++nReview;
            fReview << ToUtf8(L"REVIEW\t" + reading + L"\t" + top + L"\t" + exList + L"\n");
        }
    }
    std::printf("probe result: OK=%zu RANK=%zu AUTO=%zu REVIEW=%zu\n",
                nOk, nRank, nAuto, nReview);
    return 0;
}
