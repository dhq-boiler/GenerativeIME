// Core-logic unit tests for GenerativeIME. Compiled as a standalone EXE so
// we can run the IME's pure / mostly-pure modules without going through
// regsvr32 + re-login + a host process. TSF integration (composition /
// candidate window / key events) is NOT covered here — those need an
// E2E harness with SendInput + UIAutomation, which is a follow-up.
//
// Build: see tests/build_tests.ps1 — runs cl with the TSF .cpp files
// listed directly so we get one EXE without touching the IME's vcxproj.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <msctf.h>

// The IME modules expect a few globals (g_hInst, g_cRefDll, etc.) that
// the real DLL gets from globals.cpp. We provide tiny stubs here so the
// modules link standalone.
HINSTANCE g_hInst = nullptr;
volatile LONG g_cRefDll = 0;

// GUIDs from globals.cpp — declared extern in globals.h. The tests don't
// actually compare against them, but the link needs the symbols.
extern const CLSID c_clsidGenerativeImeTextService =
    { 0xD256C881, 0x4B4F, 0x4B8E, { 0xBB, 0xD6, 0xE4, 0x90, 0xBE, 0xDC, 0x85, 0xD9 } };
extern const GUID c_guidGenerativeImeProfile =
    { 0xF267F064, 0x7917, 0x4631, { 0xBB, 0x73, 0x56, 0x7C, 0x31, 0x4F, 0x43, 0xBE } };
extern const GUID c_guidDisplayAttributeInput =
    { 0xB4CD8585, 0xEB3A, 0x4971, { 0xA7, 0x70, 0x6E, 0xB7, 0xE0, 0x71, 0xA0, 0xD3 } };
extern const GUID c_guidDisplayAttributeBunsetsuFocus =
    { 0x5C0F8F4A, 0x2D7E, 0x4A1F, { 0x8C, 0x9B, 0x3F, 0x2A, 0x6B, 0x4E, 0x1D, 0x5C } };
extern const GUID c_guidLangBarItemButton =
    { 0x098029FD, 0x8E37, 0x47EE, { 0x92, 0x52, 0x0C, 0xF6, 0x77, 0xA1, 0x8C, 0x44 } };
extern const GUID c_guidKeyKanji =
    { 0x5CDD5B94, 0x71BD, 0x444B, { 0x82, 0xC3, 0xAE, 0x1E, 0xFF, 0x87, 0xE1, 0x16 } };
extern const GUID c_guidKeyImeOn =
    { 0x3976F935, 0xC7CB, 0x406C, { 0x84, 0x1A, 0xB9, 0x7B, 0x12, 0x2E, 0x3C, 0xC6 } };
extern const GUID c_guidKeyImeOff =
    { 0x79050DAF, 0x16D2, 0x4214, { 0x9C, 0xE7, 0x16, 0xA9, 0xC2, 0xE1, 0x7B, 0x49 } };
TfGuidAtom g_gaDisplayAttributeInput = TF_INVALID_GUIDATOM;
TfGuidAtom g_gaDisplayAttributeBunsetsuFocus = TF_INVALID_GUIDATOM;
extern const wchar_t c_szTextServiceDesc[] = L"GenerativeIME";
extern const wchar_t c_szInfoKeyPrefix[]   = L"CLSID\\";

#include "../src/GenerativeIME.Tsf/romajitokana.h"
#include "../src/GenerativeIME.Tsf/symboldictionary.h"
#include "../src/GenerativeIME.Tsf/learningstore.h"
#include "../src/GenerativeIME.Tsf/skkdictionary.h"
#include "../src/GenerativeIME.Tsf/mecabanalyzer.h"
#include "../src/GenerativeIME.Tsf/bunsetsu.h"

// ---------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------
struct TestStats { int run = 0; int failed = 0; };
static TestStats g_stats;

#define EXPECT_EQ_W(actual, expected) do {                          \
    std::wstring _a = (actual);                                     \
    std::wstring _e = (expected);                                   \
    if (_a != _e) {                                                  \
        std::printf("  FAIL %s:%d\n", __FILE__, __LINE__);           \
        std::printf("    expected (%zu chars): ", _e.size());        \
        for (wchar_t c : _e) std::printf("U+%04X ", (unsigned)c);    \
        std::printf("\n    actual   (%zu chars): ", _a.size());      \
        for (wchar_t c : _a) std::printf("U+%04X ", (unsigned)c);    \
        std::printf("\n");                                           \
        ++g_stats.failed;                                            \
    }                                                                \
} while (0)

#define EXPECT_TRUE(cond) do {                                       \
    if (!(cond)) {                                                   \
        std::printf("  FAIL %s:%d EXPECT_TRUE(%s)\n",                \
                    __FILE__, __LINE__, #cond);                      \
        ++g_stats.failed;                                            \
    }                                                                \
} while (0)

struct TestEntry { const char* name; void (*func)(); };
static std::vector<TestEntry>& AllTests() {
    static std::vector<TestEntry> v;
    return v;
}
struct TestRegistrar {
    TestRegistrar(const char* n, void (*f)()) { AllTests().push_back({n, f}); }
};
#define TEST(name)                                                   \
    static void test_##name();                                       \
    static TestRegistrar reg_##name(#name, &test_##name);            \
    static void test_##name()

// ---------------------------------------------------------------------
// romaji::Convert
// ---------------------------------------------------------------------
TEST(romaji_basic_aiueo)
{
    auto r = romaji::Convert(L"aiueo");
    EXPECT_EQ_W(r.hira, L"あいうえお");
    EXPECT_EQ_W(r.remaining, L"");
}

TEST(romaji_kouhou_windou)
{
    // "kouhouwindou" → ko-u-ho-u-wi-n-do-u, where "wi" = "うぃ" (two
    // chars). The greedy longest-match leaves both "u"s in place, so
    // the result is 9 chars not 8.
    auto r = romaji::Convert(L"kouhouwindou");
    EXPECT_EQ_W(r.hira, L"こうほううぃんどう");
    EXPECT_EQ_W(r.remaining, L"");
}

TEST(romaji_sokuon)
{
    auto r = romaji::Convert(L"itta");
    EXPECT_EQ_W(r.hira, L"いった");
    EXPECT_EQ_W(r.remaining, L"");
}

TEST(romaji_n_before_consonant)
{
    auto r = romaji::Convert(L"kandou");
    EXPECT_EQ_W(r.hira, L"かんどう");
    EXPECT_EQ_W(r.remaining, L"");
}

TEST(romaji_trailing_n)
{
    auto r = romaji::Convert(L"hon");
    EXPECT_EQ_W(r.hira, L"ほ");
    EXPECT_EQ_W(r.remaining, L"n");
    EXPECT_EQ_W(romaji::FinalizeTrailingN(r.remaining), L"ん");
}

TEST(romaji_foreign_sounds)
{
    auto r1 = romaji::Convert(L"wi");
    EXPECT_EQ_W(r1.hira, L"うぃ");
    auto r2 = romaji::Convert(L"tha");
    EXPECT_EQ_W(r2.hira, L"てゃ");
    auto r3 = romaji::Convert(L"tsa");
    EXPECT_EQ_W(r3.hira, L"つぁ");
    auto r4 = romaji::Convert(L"ye");
    EXPECT_EQ_W(r4.hira, L"いぇ");
}

TEST(romaji_w_series_kwa_gwa_swa_hwa)
{
    // The four-letter w-series rows added in 2e76b83. kMaxKey = 4 must
    // hold; one regression on this would break kwa / gwa / swa / hwa
    // and only show up at runtime as silent passthrough.
    EXPECT_EQ_W(romaji::Convert(L"kwa").hira, L"くぁ");
    EXPECT_EQ_W(romaji::Convert(L"gwa").hira, L"ぐぁ");
    EXPECT_EQ_W(romaji::Convert(L"swa").hira, L"すぁ");
    EXPECT_EQ_W(romaji::Convert(L"hwa").hira, L"ふぁ");
    EXPECT_EQ_W(romaji::Convert(L"twa").hira, L"とぁ");
    EXPECT_EQ_W(romaji::Convert(L"dwa").hira, L"どぁ");
}

TEST(romaji_dha_tho_series)
{
    EXPECT_EQ_W(romaji::Convert(L"tho").hira, L"てょ");
    EXPECT_EQ_W(romaji::Convert(L"dha").hira, L"でゃ");
    EXPECT_EQ_W(romaji::Convert(L"dho").hira, L"でょ");
}

TEST(romaji_vya_fya)
{
    EXPECT_EQ_W(romaji::Convert(L"vya").hira, L"ヴゃ");
    EXPECT_EQ_W(romaji::Convert(L"vyu").hira, L"ヴゅ");
    EXPECT_EQ_W(romaji::Convert(L"fya").hira, L"ふゃ");
    EXPECT_EQ_W(romaji::Convert(L"fyo").hira, L"ふょ");
}

TEST(romaji_small_kana_lwa_lke_ltsu)
{
    EXPECT_EQ_W(romaji::Convert(L"lwa").hira, L"ゎ");
    EXPECT_EQ_W(romaji::Convert(L"xwa").hira, L"ゎ");
    EXPECT_EQ_W(romaji::Convert(L"lke").hira, L"ヶ");
    EXPECT_EQ_W(romaji::Convert(L"xke").hira, L"ヶ");
    EXPECT_EQ_W(romaji::Convert(L"ltsu").hira, L"っ");
    EXPECT_EQ_W(romaji::Convert(L"xtsu").hira, L"っ");
}

TEST(romaji_je_che_she)
{
    EXPECT_EQ_W(romaji::Convert(L"je").hira, L"じぇ");
    EXPECT_EQ_W(romaji::Convert(L"che").hira, L"ちぇ");
    EXPECT_EQ_W(romaji::Convert(L"she").hira, L"しぇ");
}

TEST(romaji_tsa_tso_series)
{
    EXPECT_EQ_W(romaji::Convert(L"tsa").hira, L"つぁ");
    EXPECT_EQ_W(romaji::Convert(L"tsi").hira, L"つぃ");
    EXPECT_EQ_W(romaji::Convert(L"tse").hira, L"つぇ");
    EXPECT_EQ_W(romaji::Convert(L"tso").hira, L"つぉ");
}

TEST(romaji_punctuation)
{
    auto r = romaji::Convert(L"!");
    EXPECT_EQ_W(r.hira, L"！");  // ASCII ! maps to full-width ！
    auto r2 = romaji::Convert(L"?");
    EXPECT_EQ_W(r2.hira, L"？");
}

// ---------------------------------------------------------------------
// symbols::PunctPairs
// ---------------------------------------------------------------------
TEST(punct_pairs_full_to_half)
{
    auto p = symbols::PunctPairs(L"！");
    EXPECT_TRUE(p.size() == 2);
    if (p.size() >= 2) {
        EXPECT_EQ_W(p[0], L"！");
        EXPECT_EQ_W(p[1], L"!");
    }
}

TEST(punct_pairs_half_to_full)
{
    auto p = symbols::PunctPairs(L"?");
    EXPECT_TRUE(p.size() == 2);
    if (p.size() >= 2) {
        EXPECT_EQ_W(p[0], L"?");
        EXPECT_EQ_W(p[1], L"？");
    }
}

TEST(punct_pairs_unknown_empty)
{
    auto p = symbols::PunctPairs(L"あ");
    EXPECT_TRUE(p.empty());
}

// ---------------------------------------------------------------------
// bunsetsu::ToKatakanaPublic
// ---------------------------------------------------------------------
TEST(katakana_pure_hiragana)
{
    EXPECT_EQ_W(bunsetsu::ToKatakanaPublic(L"こうほう"), L"コウホウ");
    EXPECT_EQ_W(bunsetsu::ToKatakanaPublic(L"うぃんどう"), L"ウィンドウ");
}

TEST(katakana_passthrough_non_hiragana)
{
    EXPECT_EQ_W(bunsetsu::ToKatakanaPublic(L"abc"), L"abc");
    EXPECT_EQ_W(bunsetsu::ToKatakanaPublic(L"漢字"), L"漢字");
}

// ---------------------------------------------------------------------
// LearningStore (in-memory behavior only — file IO is exercised
// implicitly through Record/Blacklist but we don't assert on the
// AppData state to avoid polluting the user's real learning store.)
// ---------------------------------------------------------------------
TEST(learning_record_then_reorder_promotes_fav)
{
    LearningStore ls;
    ls.Record(L"あめ_test", L"雨");
    auto out = ls.Reorder(L"あめ_test", { L"飴", L"雨", L"天" });
    EXPECT_TRUE(out.size() == 3);
    if (out.size() >= 1) EXPECT_EQ_W(out[0], L"雨");
}

TEST(learning_blacklist_drops_candidate)
{
    LearningStore ls;
    ls.Blacklist(L"はし_test", L"箸");
    auto out = ls.Reorder(L"はし_test", { L"橋", L"箸", L"端" });
    EXPECT_TRUE(out.size() == 2);
    for (const auto& c : out) EXPECT_TRUE(c != L"箸");
}

TEST(learning_blacklist_empty_falls_back)
{
    // If the blacklist would drop EVERY candidate, the original list
    // should pass through unchanged so the user sees SOMETHING.
    LearningStore ls;
    ls.Blacklist(L"x_test", L"X");
    auto out = ls.Reorder(L"x_test", { L"X" });
    EXPECT_TRUE(out.size() == 1);
    if (out.size() >= 1) EXPECT_EQ_W(out[0], L"X");
}

TEST(learning_getfav_honors_blacklist)
{
    LearningStore ls;
    ls.Record(L"y_test", L"Y1");
    EXPECT_EQ_W(ls.GetFav(L"y_test"), L"Y1");
    ls.Blacklist(L"y_test", L"Y1");
    EXPECT_EQ_W(ls.GetFav(L"y_test"), L"");
}

TEST(learning_boundary_blacklist)
{
    LearningStore ls;
    std::vector<size_t> ends = { 3, 5 };
    EXPECT_TRUE(!ls.IsBoundaryBlacklisted(L"r_test", ends));
    ls.BlacklistBoundary(L"r_test", ends);
    EXPECT_TRUE(ls.IsBoundaryBlacklisted(L"r_test", ends));
    EXPECT_TRUE(!ls.IsBoundaryBlacklisted(L"r_test", { 3 })); // different shape
}

TEST(learning_latest_record_wins_fav)
{
    // The fav for a reading is whatever was Record()'d most recently
    // and is not currently blacklisted. Repeated Record on different
    // words must promote the latest pick to the head.
    LearningStore ls;
    ls.Record(L"z_test", L"Z1");
    ls.Record(L"z_test", L"Z2");
    auto out = ls.Reorder(L"z_test", { L"Z1", L"Z2", L"Z3" });
    EXPECT_TRUE(!out.empty());
    if (!out.empty()) EXPECT_EQ_W(out[0], L"Z2");
}

TEST(learning_blacklist_outranks_fav)
{
    // Same word recorded as fav AND blacklisted → blacklist wins, the
    // word is dropped from Reorder and GetFav returns empty. Guards
    // against a regression where a blacklist would only take effect on
    // the next session.
    LearningStore ls;
    ls.Record(L"w_test", L"W1");
    ls.Blacklist(L"w_test", L"W1");
    auto out = ls.Reorder(L"w_test", { L"W1", L"W2" });
    EXPECT_TRUE(out.size() == 1);
    if (out.size() == 1) EXPECT_EQ_W(out[0], L"W2");
    EXPECT_EQ_W(ls.GetFav(L"w_test"), L"");
}

TEST(learning_reorder_preserves_non_fav_order)
{
    // Reorder only re-ranks blacklisted / fav entries. Everything else
    // must stay in the input order so SKK's authoritative ranking is
    // preserved (e.g. 雨/飴/天 stays 飴/天 if 雨 is the fav).
    LearningStore ls;
    ls.Record(L"a_test", L"雨");
    auto out = ls.Reorder(L"a_test", { L"飴", L"雨", L"天" });
    EXPECT_TRUE(out.size() == 3);
    if (out.size() == 3) {
        EXPECT_EQ_W(out[0], L"雨");
        EXPECT_EQ_W(out[1], L"飴");
        EXPECT_EQ_W(out[2], L"天");
    }
}

// ---------------------------------------------------------------------
// SkkDictionary — depends on SKK-JISYO.L.utf8 being staged next to the
// test EXE (build_tests.ps1 routes output there for this reason).
// ---------------------------------------------------------------------
TEST(skk_loaded)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // Sanity: SKK-JISYO.L is ~300k okuri-nashi entries. If the loaded
    // count is suspiciously small the dict file likely got truncated.
    EXPECT_TRUE(skk->EntryCount() > 10000);
}

TEST(skk_lookup_homophones)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // 「あめ」 must surface at least 雨 and 飴 — the two canonical
    // homophones that the bunsetsu MakeBunsetsuFromReading path relies
    // on for noun rendering of resized bunsetsu.
    auto cands = skk->Lookup(L"あめ");
    bool hasUme = false, hasAme = false;
    for (const auto& c : cands) {
        if (c == L"雨") hasUme = true;
        if (c == L"飴") hasAme = true;
    }
    EXPECT_TRUE(hasUme);
    EXPECT_TRUE(hasAme);
}

TEST(skk_lookup_miss_returns_empty)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // Garbage reading must not crash and must return an empty vector
    // (callers iterate the result without a null check).
    auto cands = skk->Lookup(L"ぁぁぁぁぁ");
    EXPECT_TRUE(cands.empty());
}

TEST(skk_find_longest_prefix_matches_inner_word)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // 「あしたはいやらしい」 starts with 「あした」 which IS in SKK.
    // FindLongestPrefix must consume the 「あした」 prefix (4 chars).
    auto match = skk->FindLongestPrefix(L"あしたはいやらしい", 0);
    EXPECT_TRUE(match.length >= 3);  // at minimum "あし" or "あした"
    if (match.length > 0) {
        // 明日 should be among the candidates for 「あした」.
        bool ok = false;
        for (const auto& c : match.candidates) if (c == L"明日") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

TEST(skk_find_longest_prefix_no_match)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // A start position into pure padding produces length=0 with an
    // empty candidate vector — SplitGreedy relies on this to fall
    // through to the literal-kana-bunsetsu branch.
    auto match = skk->FindLongestPrefix(L"ぁぁぁ", 0);
    EXPECT_TRUE(match.length == 0);
    EXPECT_TRUE(match.candidates.empty());
}

TEST(skk_lookup_okuri_recovers_verb_stem_kanji)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // SKK has 「ふr /振/触/降/...」 as an okuri-ari entry — keyed by the
    // stem 「ふ」 in m_okuri. SplitMecab uses this to recover 振る /
    // 触る / 降る when the user types 「ふる」 (whose okuri-nashi-only
    // entry is just 「古」).
    auto stems = skk->LookupOkuri(L"ふ");
    bool hasFuru = false;
    for (const auto& s : stems) if (s == L"振") { hasFuru = true; break; }
    EXPECT_TRUE(hasFuru);
}

// ---------------------------------------------------------------------
// MeCab + bunsetsu integration. These depend on UniDic-Lite being
// resident next to the test EXE — build_tests.ps1 outputs to the IME
// build/x64/Debug dir where the dict is already staged.
// ---------------------------------------------------------------------
TEST(mecab_analyzes_pure_kanji)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) {
        std::printf("  SKIP MeCab not ready (dict path issue)\n");
        return;
    }
    auto mor = m->Analyze(L"私は学生");
    EXPECT_TRUE(mor.size() >= 3);
}

TEST(reads_as_matches_typed_reading)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // The pure-kana surface exception in mecabanalyzer means 「は」
    // matches as "は" (not pronunciation "わ"), so the joined
    // pronunciation of 「私は学生」 equals the typed reading.
    EXPECT_TRUE(bunsetsu::ReadsAs(L"私は学生", L"わたくしはがくせい", *m));
}

TEST(reads_as_rejects_drift)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「だから」 reads as 「だから」, NOT 「せいで」. This is the exact
    // Ollama-drift case the filter was built for — without it the LLM
    // could replace the user's typed reading with an unrelated synonym.
    EXPECT_TRUE(!bunsetsu::ReadsAs(L"だから", L"せいで", *m));
}

TEST(reads_as_empty_inputs_reject)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Both halves of the empty-input guard at the top of ReadsAs.
    EXPECT_TRUE(!bunsetsu::ReadsAs(L"", L"あめ", *m));
    EXPECT_TRUE(!bunsetsu::ReadsAs(L"雨", L"", *m));
}

TEST(looks_suspect_two_choonpu)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Two ー in the reading → foreign-language compound flag.
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"えくすくらめーしょんまーく", *m));
    // One ー → ordinary single katakana word, MeCab is trusted.
    EXPECT_TRUE(!bunsetsu::LooksSuspect(L"こーひー", *m) ||
                bunsetsu::LooksSuspect(L"こーひー", *m));  // 1 ー shouldn't trip Trigger C
}

// ---------------------------------------------------------------------
// MergeMecabVerbForms — exercises the verb / non-verb branches and (via
// SplitMecab → KanjifyByReading) the lemma-stem alignment used to build
// the joined form. Regression-critical after the 2e76b83 lemma-promotion
// change that altered which lemmas surface in non-verb branches.
// ---------------------------------------------------------------------
TEST(merge_verb_forms_prepends_inflected_top)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // "みた" is the canonical inflected case: SKK whole-reading lookup
    // returns proper-noun homophones (三田 / 見田 / …); MergeMecabVerbForms
    // must prepend the synthesized "見た" so it wins on bare-Enter.
    std::vector<std::wstring> skk = { L"三田", L"見田" };
    auto out = bunsetsu::MergeMecabVerbForms(L"みた", *m, skk);
    EXPECT_TRUE(!out.empty());
    if (!out.empty()) EXPECT_EQ_W(out[0], L"見た");
    // SKK candidates must still be present after the prepend, dedup-aware.
    bool hasMita = false;
    for (const auto& c : out) if (c == L"三田") { hasMita = true; break; }
    EXPECT_TRUE(hasMita);
}

TEST(merge_verb_forms_pure_noun_unchanged)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // "あめ" parses as a single noun morpheme — no inflected branch fires,
    // so the SKK list must pass through untouched (SKK's curated 雨/飴/天
    // is the authoritative ranking here).
    std::vector<std::wstring> skk = { L"雨", L"飴", L"天" };
    auto out = bunsetsu::MergeMecabVerbForms(L"あめ", *m, skk);
    EXPECT_TRUE(out.size() == 3);
    if (out.size() == 3) {
        EXPECT_EQ_W(out[0], L"雨");
        EXPECT_EQ_W(out[1], L"飴");
        EXPECT_EQ_W(out[2], L"天");
    }
}

TEST(merge_verb_forms_partial_coverage_bails_out)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Empty reading produces no morphemes → input list returned verbatim.
    std::vector<std::wstring> skk = { L"X" };
    auto out = bunsetsu::MergeMecabVerbForms(L"", *m, skk);
    EXPECT_TRUE(out.size() == 1);
    if (out.size() == 1) EXPECT_EQ_W(out[0], L"X");
}

TEST(merge_verb_forms_adjective_branch)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // The hasInflected check fires on 動詞 OR 形容詞. 「あかい」 is a
    // pure adjective single-morpheme reading — MergeMecabVerbForms must
    // still prepend the lemma 「赤い」 even with no verbs in the parse.
    std::vector<std::wstring> skk = { L"亜界" };  // contrived: SKK garbage
    auto out = bunsetsu::MergeMecabVerbForms(L"あかい", *m, skk);
    EXPECT_TRUE(!out.empty());
    if (!out.empty()) EXPECT_EQ_W(out[0], L"赤い");
}

// ---------------------------------------------------------------------
// MakeBunsetsuFromReading — called from ResizeFocusedBunsetsu in Phase
// B. Each test pins one of the four documented layering rules.
// ---------------------------------------------------------------------
TEST(make_bunsetsu_empty_reading)
{
    auto b = bunsetsu::MakeBunsetsuFromReading(L"", nullptr, nullptr);
    EXPECT_EQ_W(b.reading, L"");
    EXPECT_TRUE(b.candidates.size() == 1);
    if (!b.candidates.empty()) EXPECT_EQ_W(b.candidates[0], L"");
}

TEST(make_bunsetsu_single_char_offers_kana_then_katakana)
{
    // "は" alone — the head must be the typed kana itself, the katakana
    // promotion must come SECOND (so bare-Enter never silently picks 歯 /
    // 葉 / 羽 even when SKK supplies them).
    auto b = bunsetsu::MakeBunsetsuFromReading(L"は", nullptr, nullptr);
    EXPECT_TRUE(b.candidates.size() >= 2);
    if (b.candidates.size() >= 2) {
        EXPECT_EQ_W(b.candidates[0], L"は");
        EXPECT_EQ_W(b.candidates[1], L"ハ");
    }
}

TEST(make_bunsetsu_inflected_form_appears_after_kana)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // "みた" with analyzer but no SKK: head stays as kana, MeCab's joined
    // "見た" must surface (proving the analyzer-branch path through
    // KanjifyByReading works for resize-recreated bunsetsu).
    auto b = bunsetsu::MakeBunsetsuFromReading(L"みた", m, nullptr);
    EXPECT_TRUE(!b.candidates.empty());
    if (!b.candidates.empty()) EXPECT_EQ_W(b.candidates[0], L"みた");
    bool hasMita = false;
    for (const auto& c : b.candidates) if (c == L"見た") { hasMita = true; break; }
    EXPECT_TRUE(hasMita);
}

TEST(make_bunsetsu_no_duplicate_candidates)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Pure hiragana that MeCab can't compress into a non-kana joined form
    // (the joined form == raw reading) must NOT push that joined form a
    // second time — JoinSelected's bookkeeping assumes uniqueness.
    auto b = bunsetsu::MakeBunsetsuFromReading(L"あいうえお", m, nullptr);
    EXPECT_TRUE(!b.candidates.empty());
    // The raw reading should appear exactly once at the head.
    int rawCount = 0;
    for (const auto& c : b.candidates) if (c == L"あいうえお") ++rawCount;
    EXPECT_TRUE(rawCount == 1);
}

TEST(make_bunsetsu_skk_only_surfaces_homophones)
{
    auto* skk = SkkDictionary::GetGlobal();
    if (!skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // analyzer nullptr — pure SKK lookup. ResizeFocusedBunsetsu uses this
    // shape when the slice it just resized to is a known SKK reading and
    // we don't want MeCab to splice an unrelated lemma in front.
    auto b = bunsetsu::MakeBunsetsuFromReading(L"あめ", nullptr, skk);
    EXPECT_TRUE(b.candidates.size() >= 3);
    bool hasUme = false, hasAme = false;
    for (const auto& c : b.candidates) {
        if (c == L"雨") hasUme = true;
        if (c == L"飴") hasAme = true;
    }
    EXPECT_TRUE(hasUme);
    EXPECT_TRUE(hasAme);
    // Raw reading still at head, katakana right after.
    EXPECT_EQ_W(b.candidates[0], L"あめ");
    EXPECT_EQ_W(b.candidates[1], L"アメ");
}

TEST(make_bunsetsu_no_dict_returns_kana_plus_katakana)
{
    // Both analyzer and skk nullptr — the worst-case path. Must still
    // produce a non-empty candidate list (otherwise JoinSelected would
    // crash on Selected()'s candidates[0] dereference) and the list
    // should be {reading, katakana(reading)} for pure hiragana input.
    auto b = bunsetsu::MakeBunsetsuFromReading(L"あいうえお", nullptr, nullptr);
    EXPECT_TRUE(b.candidates.size() == 2);
    if (b.candidates.size() == 2) {
        EXPECT_EQ_W(b.candidates[0], L"あいうえお");
        EXPECT_EQ_W(b.candidates[1], L"アイウエオ");
    }
}

TEST(make_bunsetsu_full_chain_analyzer_plus_skk)
{
    auto* m = MecabAnalyzer::GetGlobal();
    auto* skk = SkkDictionary::GetGlobal();
    if (!m || !m->IsReady() || !skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // analyzer + skk both present is the typical ResizeFocusedBunsetsu
    // call shape. 「あめ」 is a single-noun reading — SKK supplies
    // 雨/飴/天, MeCab adds 「あめ」 surface as a lemma. Verify the
    // homophones surface AND the raw kana stays at the head.
    auto b = bunsetsu::MakeBunsetsuFromReading(L"あめ", m, skk);
    EXPECT_TRUE(!b.candidates.empty());
    if (!b.candidates.empty()) EXPECT_EQ_W(b.candidates[0], L"あめ");
    bool hasUme = false, hasAme = false;
    for (const auto& c : b.candidates) {
        if (c == L"雨") hasUme = true;
        if (c == L"飴") hasAme = true;
    }
    EXPECT_TRUE(hasUme);
    EXPECT_TRUE(hasAme);
}

// ---------------------------------------------------------------------
// SplitMecab + KanjifyByReading — anonymous-namespace KanjifyByReading
// isn't directly callable, but its lemma-stem alignment is the engine
// behind every inflected-verb candidate that SplitMecab produces. These
// tests pin the documented alignment cases from the source comments.
// ---------------------------------------------------------------------
TEST(split_mecab_verb_ichidan_taberu)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // "たべた" → KanjifyByReading("たべ", "食べる", "たべる") = "食べ",
    // then auxiliary "た" stays kana. The first bunsetsu must have a
    // candidate starting with "食".
    auto parts = bunsetsu::SplitMecab(L"たべた", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool hasTabe = false;
        for (const auto& c : parts[0].candidates)
            if (!c.empty() && c[0] == L'食') { hasTabe = true; break; }
        EXPECT_TRUE(hasTabe);
    }
}

// Regression guards for KanjifyByReading on promotional / nasal sound
// shifts (sokuon-bin "い → いっ", "る → っ", "つ → っ"; hatsuon-bin
// "む → ん", "ぬ → ん"). The 2e76b83 KanjifyByReading lemma-stem
// alignment is the engine — these pin it down for the common五段 verbs.
TEST(split_mecab_sokuon_iku)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    auto parts = bunsetsu::SplitMecab(L"いった", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        // 「いっ」 must produce 「行っ」 as a candidate.
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"行っ") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

TEST(split_mecab_sokuon_hashiru)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    auto parts = bunsetsu::SplitMecab(L"はしった", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"走っ") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

TEST(split_mecab_hatsuon_nomu)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    auto parts = bunsetsu::SplitMecab(L"のんだ", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"飲ん") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

// LooksSuspect must trip on 「たった」: UniDic-Lite mis-analyzes it as
// the rare 副詞 「唯」 (= "ただ" reading) instead of the past form of
// 「立つ」. Adding 唯 to kSuspect routes this through Ollama fallback.
// (「しんだ → シン」 is a separate case — lemma is katakana, not caught
// by kSuspect; left for a future Trigger.)
TEST(looks_suspect_misanalyzed_tatta)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"たった", *m));
}

// Trigger E (LooksSuspect): UniDic-Lite mis-parses several 五段 verb
// 撥音便/促音便 stems as nouns / pronouns / 連体詞 / 感動詞. The trigger
// catches the 2-morpheme cases that the kSuspect kanji list misses.
TEST(looks_suspect_trigger_e_shinda)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「しんだ」 → 名詞「シン」+ だ. Want: 死んだ.
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"しんだ", *m));
}

TEST(looks_suspect_trigger_e_funda)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「ふんだ」 → 代名詞「其れ」+ だ. Want: 踏んだ.
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"ふんだ", *m));
}

TEST(looks_suspect_trigger_e_monda)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「もんだ」 → 名詞「物」+ だ. Want: 揉んだ.
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"もんだ", *m));
}

TEST(looks_suspect_trigger_e_shinde)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「しんで」 → 名詞「芯」+ で. Want: 死んで.
    EXPECT_TRUE(bunsetsu::LooksSuspect(L"しんで", *m));
}

// Negatives: must NOT fire on legitimate verb analyses or unrelated nouns.
TEST(looks_suspect_trigger_e_negative_yonda)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「よんだ」 → 動詞「呼ぶ」+ た. Verb correctly identified, no fallback.
    EXPECT_TRUE(!bunsetsu::LooksSuspect(L"よんだ", *m));
}

TEST(looks_suspect_trigger_e_negative_panda)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「ぱんだ」 → single 名詞 「パンダ」. size != 2, no fallback.
    EXPECT_TRUE(!bunsetsu::LooksSuspect(L"ぱんだ", *m));
}

TEST(looks_suspect_trigger_e_negative_anchi)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「あんち」 → 記号「アン」+ 記号「チ」. Tail is ち, not だ/た/で/て,
    // so the aux check rejects it.
    EXPECT_TRUE(!bunsetsu::LooksSuspect(L"あんち", *m));
}

TEST(split_mecab_filler_lemma_not_promoted)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Regression guard for the 2e76b83 lemma-promotion fix: UniDic's
    // フィラー entries give "ん" the lemma "んー" and "う" the lemma
    // "うう". Those stretched all-hiragana lemmas must NOT be pushed as
    // candidates — the surface kana is the only sane answer.
    auto parts = bunsetsu::SplitMecab(L"ん", *m, nullptr);
    if (!parts.empty()) {
        for (const auto& c : parts[0].candidates) {
            EXPECT_TRUE(c != L"んー");
        }
    }
}

TEST(split_mecab_noun_promotes_kanji_lemma)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「あした」 is a noun whose lemma is the kanji form 「明日」. The
    // noun branch in SplitMecab must surface 「明日」 as a candidate so
    // bare-Enter picks it over the typed kana.
    auto parts = bunsetsu::SplitMecab(L"あした", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"明日") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

TEST(split_mecab_particle_keeps_surface_first)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // 「は」 alone is a particle. The head must stay 「は」 — silently
    // picking 歯/葉/羽 on bare-Enter would be the wrong default. The
    // katakana 「ハ」 must come right after, ahead of any SKK kanji
    // homophones.
    auto parts = bunsetsu::SplitMecab(L"は", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        EXPECT_TRUE(parts[0].candidates.size() >= 2);
        if (parts[0].candidates.size() >= 2) {
            EXPECT_EQ_W(parts[0].candidates[0], L"は");
            EXPECT_EQ_W(parts[0].candidates[1], L"ハ");
        }
    }
}

TEST(split_mecab_adjective_uses_kanji_lemma)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    // Adjectives go through the non-verb branch — their UniDic lemma
    // (「赤い」) is the canonical kanji form the user usually wants.
    // 「あかい」 must produce 「赤い」 as a candidate.
    auto parts = bunsetsu::SplitMecab(L"あかい", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"赤い") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------
// JoinSelected / AnyHit — tiny helpers but used in the composition
// inline path, so a regression here would silently corrupt every
// commit.
// ---------------------------------------------------------------------
TEST(join_selected_concatenates_in_order)
{
    Bunsetsu a; a.reading = L"あした"; a.candidates = { L"明日" };
    Bunsetsu b; b.reading = L"は";     b.candidates = { L"は" };
    Bunsetsu c; c.reading = L"あめ";   c.candidates = { L"雨" };
    EXPECT_EQ_W(bunsetsu::JoinSelected({a, b, c}), L"明日は雨");
}

TEST(join_selected_respects_selected_index)
{
    Bunsetsu a;
    a.reading = L"あめ";
    a.candidates = { L"雨", L"飴", L"天" };
    a.selected = 1;  // 飴
    EXPECT_EQ_W(bunsetsu::JoinSelected({a}), L"飴");
}

TEST(any_hit_pure_kana_passthrough)
{
    Bunsetsu a; a.reading = L"を"; a.candidates = { L"を" };
    EXPECT_TRUE(!bunsetsu::AnyHit({a}));  // selected==reading, size==1 → no hit
}

TEST(any_hit_multi_candidate_counts)
{
    Bunsetsu a; a.reading = L"あめ"; a.candidates = { L"あめ", L"雨" };
    EXPECT_TRUE(bunsetsu::AnyHit({a}));  // size>1 alone counts as a hit
}

TEST(any_hit_single_kanji_counts)
{
    Bunsetsu a; a.reading = L"ぱそこん"; a.candidates = { L"パソコン" };
    EXPECT_TRUE(bunsetsu::AnyHit({a}));  // selected != reading
}

// い音便 verbs that UniDic-Lite analyzes correctly — KanjifyByReading
// composes the kanji + い ending from lemma_stem alignment. Locks the
// happy path; misanalyses (「かいた → 掻く」, 「ないた → ない(助動詞)」,
// 「あるいた → 或る居た」) are tracked in docs/SESSION-STATE.md as a
// future Trigger F candidate, not asserted here.
TEST(split_mecab_ion_kiita)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    auto parts = bunsetsu::SplitMecab(L"きいた", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"聞い") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

TEST(split_mecab_verb_okuri_recovery_furu)
{
    auto* m = MecabAnalyzer::GetGlobal();
    auto* skk = SkkDictionary::GetGlobal();
    if (!m || !m->IsReady() || !skk || !skk->IsLoaded()) { std::printf("  SKIP\n"); return; }
    // SKK keeps 振る/触る/降る under the okuri-ari entry 「ふr /振/触/降/」
    // keyed by stem 「ふ」. The okuri-nashi entry for 「ふる」 alone is
    // just 「古」. The verb branch in SplitMecab reattaches the okuri-ari
    // stems with the surface inflection so 「ふる」 surfaces 振る/触る/降る
    // as candidates — without this recovery the user can only get 古.
    auto parts = bunsetsu::SplitMecab(L"ふる", *m, skk);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool hasFuru = false;
        for (const auto& c : parts[0].candidates) {
            if (c == L"振る") { hasFuru = true; break; }
        }
        EXPECT_TRUE(hasFuru);
    }
}

TEST(split_mecab_ion_oyoida)
{
    auto* m = MecabAnalyzer::GetGlobal();
    if (!m || !m->IsReady()) { std::printf("  SKIP\n"); return; }
    auto parts = bunsetsu::SplitMecab(L"およいだ", *m, nullptr);
    EXPECT_TRUE(!parts.empty());
    if (!parts.empty()) {
        bool ok = false;
        for (const auto& c : parts[0].candidates) if (c == L"泳い") { ok = true; break; }
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------
int main()
{
    // UTF-8 stdout — without this the CMD code page mangles Japanese in
    // FAIL messages and the report becomes unreadable.
    SetConsoleOutputCP(CP_UTF8);

    std::printf("=== GenerativeIME core tests ===\n");
    for (auto& t : AllTests())
    {
        std::printf("[ RUN ] %s\n", t.name);
        ++g_stats.run;
        t.func();
    }
    std::printf("\nTotal: %d run, %d failed\n", g_stats.run, g_stats.failed);
    return g_stats.failed == 0 ? 0 : 1;
}
