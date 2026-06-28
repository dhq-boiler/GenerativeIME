#pragma once

#include <string>
#include <vector>

class SkkDictionary;
class MecabAnalyzer;

// A single bunsetsu (文節) — the IME's analog of a "chunk" of the reading
// that converts as a unit. We hold the source kana, the list of dictionary
// candidates for that kana, and the index of the one currently shown.
// `candidates` is guaranteed non-empty; the first entry is the dictionary's
// top suggestion (or the literal kana if the splitter couldn't match it).
struct Bunsetsu
{
    std::wstring              reading;
    std::vector<std::wstring> candidates;
    size_t                    selected = 0;

    const std::wstring& Selected() const { return candidates[selected]; }
};

namespace bunsetsu
{
    // Greedy longest-match split: at each position, take the longest prefix
    // of `reading` (from that point) that exists in the dictionary, treat
    // the rest the same way. Positions with no dictionary hit become a
    // single-char bunsetsu whose only candidate is the kana itself.
    // Cheap, deterministic, and good enough for the MVP — a Viterbi /
    // minimum-cost splitter can replace this later without touching callers.
    std::vector<Bunsetsu> SplitGreedy(const std::wstring& reading, const SkkDictionary& dict);

    // Concatenates the `Selected()` of each bunsetsu in order. This is the
    // text the IME inlines into the composition.
    std::wstring JoinSelected(const std::vector<Bunsetsu>& parts);

    // True iff at least one bunsetsu's selected text isn't literally the
    // kana reading — i.e. the split actually produced a conversion somewhere.
    // We use this to skip the bunsetsu path for runs that are all-hiragana,
    // since there's nothing to show that the user couldn't read off the
    // composition already.
    bool AnyHit(const std::vector<Bunsetsu>& parts);

    // MeCab-driven split. Each Bunsetsu's first candidate is the UniDic
    // lemma (canonical kanji form). Optionally append SKK candidates for the
    // same reading as additional choices. Particles / auxiliaries stay as
    // their kana surface — never substituted with kanji.
    std::vector<Bunsetsu> SplitMecab(const std::wstring& reading,
                                     const MecabAnalyzer& analyzer,
                                     const SkkDictionary* skk);

    // True iff MeCab's analysis of `reading` looks dubious enough that we
    // should ask Ollama for a second opinion. Triggers when the split has
    // 3+ morphemes AND at least one morpheme's lemma uses kanji that almost
    // always come out wrong in modern writing (顎 / 所為 / 為 / 居る /
    // 出来る / 御 / etc.) — exactly the cases where UniDic-Lite's
    // dictionary-perfect lemma is the formally correct but practically
    // useless answer the user almost certainly didn't want.
    bool LooksSuspect(const std::wstring& reading,
                      const MecabAnalyzer& analyzer);

    // When SKK's whole-reading lookup hits for an inflected form, the result
    // is usually wrong: SKK doesn't index conjugated verbs, so lookups like
    // "みた" return proper nouns (三田/見田/美田) or okuri-ari leftovers
    // (見立) instead of the obvious "見た". MeCab DOES know the inflection
    // (lemma="見る", 連用形), and we can synthesize "見た" from that. This
    // helper runs MeCab on `reading` and, when the split contains a verb or
    // adjective, prepends the kanjified combined form to `skkCandidates`
    // (with dedup). Returns `skkCandidates` unchanged when MeCab declined,
    // when no morpheme was inflected (pure-noun cases — let SKK win), or
    // when MeCab couldn't produce a form different from the bare kana.
    std::vector<std::wstring> MergeMecabVerbForms(
        const std::wstring& reading,
        const MecabAnalyzer& analyzer,
        const std::vector<std::wstring>& skkCandidates);
}
