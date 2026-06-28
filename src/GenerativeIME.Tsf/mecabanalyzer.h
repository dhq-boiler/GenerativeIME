#pragma once

#include <string>
#include <vector>

// One morpheme out of the MeCab tagger. `surface` is the slice of the input
// the morpheme covered; `lemma` is the dictionary's canonical written form
// (UniDic gives us 漢字 here even for a kana surface — that's the entire
// reason we picked UniDic-Lite over IPADIC); `pos` is the high-level
// part-of-speech tag (名詞 / 助詞 / etc.).
struct MecabMorpheme
{
    std::wstring surface;
    std::wstring lemma;
    std::wstring pos;
    // The lemma's reading in hiragana (UniDic field 6, kana-cased down to
    // hiragana). For "見る" this is "みる". We need it to map surface kana
    // back to kanji for inflected forms — KanjifyByReading aligns
    // surface against lemmaReading to figure out how much of lemma's
    // leading kanji applies to the surface.
    std::wstring lemmaReading;
};

// Thin wrapper around mecab::Model / mecab::Tagger configured to load the
// UniDic-Lite dictionary that ships alongside the DLL. The Tagger itself
// is process-wide singleton (it's expensive to create — sys.dic is ~188 MB —
// and thread-safe for read-only Analyze calls via the per-call Lattice).
class MecabAnalyzer
{
public:
    static MecabAnalyzer* GetGlobal();

    bool IsReady() const { return m_ready; }

    // Returns the morphemes for `text`, or empty on failure. `text` is the
    // raw hiragana reading the user typed; MeCab's surface analysis gives
    // us back the bunsetsu the user almost certainly intended, with the
    // kanji form already attached as `lemma`.
    std::vector<MecabMorpheme> Analyze(const std::wstring& text) const;

private:
    MecabAnalyzer() = default;
    ~MecabAnalyzer();
    MecabAnalyzer(const MecabAnalyzer&)            = delete;
    MecabAnalyzer& operator=(const MecabAnalyzer&) = delete;

    bool Init();

    // We hold these as void* in the header to keep <mecab.h> out of the
    // public surface (it pulls in <iostream> and friends). The .cpp casts
    // back to the real types.
    void* m_model  = nullptr;   // MeCab::Model*
    void* m_tagger = nullptr;   // MeCab::Tagger*
    bool  m_ready  = false;
};
