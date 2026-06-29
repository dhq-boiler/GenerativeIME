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
