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
    std::wstring reading;
    std::vector<std::wstring> candidates;
    size_t selected = 0;

    // `selected` mirrors the candidate window's index, and async list swaps
    // (Ollama results landing mid-Phase-B) can leave it pointing past this
    // bunsetsu's own list. An unchecked candidates[selected] then reads
    // whatever the heap holds next — real crash: reinterpreted garbage as a
    // 65k-char wstring and took Chrome down inside memcpy. Fall back to the
    // reading (== candidates.front() shape) instead of trusting the index.
    const std::wstring& Selected() const
    {
        if (selected < candidates.size()) return candidates[selected];
        return candidates.empty() ? reading : candidates.front();
    }
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

    // True iff `candidate` reads as `expectedReading` when MeCab decomposes
    // it. We sum each morpheme's `pronunciation` (UniDic field 9, hiragana
    // + long-vowel expanded) and compare against expectedReading verbatim.
    // Used to drop Ollama suggestions whose reading drifted from the user's
    // input (e.g. "せいで" prompt → "だから" answer): SKK / MeCab candidates
    // are reading-perfect by construction so this filter is meant for the
    // LLM path only.
    bool ReadsAs(const std::wstring& candidate,
                 const std::wstring& expectedReading,
                 const MecabAnalyzer& analyzer);

    // Split `text` into pieces whose per-piece readings line up with the
    // given `expectedReadings` list, one entry per requested bunsetsu.
    // Returns an equally-sized vector on success (each element is the
    // surface slice for that bunsetsu), or empty when the split fails —
    // either MeCab's morpheme boundaries in `text` don't align with the
    // reading-length seams the caller requires, or the per-piece
    // pronunciation doesn't equal `expectedReadings[i]` verbatim.
    //
    // Used to distribute an Ollama whole-phrase suggestion into an active
    // Phase B session's per-bunsetsu candidate lists: only suggestions
    // whose morphology matches the user's already-visible split get
    // spliced in, keeping Phase B's per-clause navigation intact.
    std::vector<std::wstring> SplitByReadings(
        const std::wstring& text,
        const std::vector<std::wstring>& expectedReadings,
        const MecabAnalyzer& analyzer);

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
    // when no morpheme was inflected (pure-noun cases — let SKK win), when
    // MeCab couldn't produce a form different from the bare kana, or when
    // `skkForDirectCheck` is non-null AND has a direct okuri-nashi entry
    // for `reading` (the dict maintainer wrote it by hand, so their entries
    // beat any MeCab noun-compound stitch — see the 「かいさい → 回際」
    // regression from WDAC iteration 2).
    std::vector<std::wstring> MergeMecabVerbForms(
        const std::wstring& reading,
        const MecabAnalyzer& analyzer,
        const std::vector<std::wstring>& skkCandidates,
        const SkkDictionary* skkForDirectCheck = nullptr);

    // Build a single Bunsetsu from `reading` without splitting it across
    // multiple clauses. Tries SKK first (whole-reading lookup for nouns
    // like "あめ" → 雨/飴/天), then MeCab for inflected forms ("みた"
    // → 見た) and finally falls back to the literal kana. Used by Phase B
    // when the user shrinks / grows a bunsetsu and we need fresh
    // candidates for the new reading slice.
    Bunsetsu MakeBunsetsuFromReading(const std::wstring& reading,
                                     const MecabAnalyzer* analyzer,
                                     const SkkDictionary* skk);

    // Promote a pure-hiragana string to its katakana counterpart. Chars
    // outside the hiragana plane pass through unchanged. Exposed so
    // textservice can try katakana-recovered foreign-word candidates
    // ("えくすくらめーしょんまーく" → "エクスクラメーションマーク")
    // when the hiragana reading looks like MeCab is going to shred it.
    std::wstring ToKatakanaPublic(const std::wstring& s);
}
