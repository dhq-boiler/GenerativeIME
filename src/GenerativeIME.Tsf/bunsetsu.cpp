#include "bunsetsu.h"
#include "skkdictionary.h"
#include "mecabanalyzer.h"

#include <algorithm>

namespace
{
    constexpr bool IsHiragana(wchar_t c) { return c >= 0x3040 && c <= 0x309F; }

    // Splits a verb lemma "食べる" into ("食", "べる"). When the lemma has
    // no leading kanji (e.g. "する"), kanjiPrefix is empty and hiraSuffix
    // is the whole string.
    void SplitLemma(const std::wstring& lemma,
                    std::wstring& kanjiPrefix, std::wstring& hiraSuffix)
    {
        size_t i = 0;
        while (i < lemma.size() && !IsHiragana(lemma[i])) ++i;
        kanjiPrefix = lemma.substr(0, i);
        hiraSuffix  = lemma.substr(i);
    }

    // Produces a kanji+hira form for an inflected verb / adjective surface
    // by aligning the lemma against its UniDic reading. This is more reliable
    // than tail-matching against lemma's hira suffix because it handles
    // 連用形 (where the kana ending is empty on 一段 verbs): surface "み"
    // against lemma "見る" / reading "みる" yields "見", which the old
    // tail-match couldn't do.
    //
    // 例:
    //   ("たべ", "食べる", "たべる")  -> "食べ"
    //   ("み",   "見る",   "みる")    -> "見"
    //   ("もえ", "燃える", "もえる")  -> "燃え"
    //   ("おり", "下りる", "おりる")  -> "下り"
    //   ("はし", "走る",   "はしる")  -> "走"
    //
    // Algorithm: peel matching kana suffix off the END of lemma and
    // lemmaReading symmetrically — this isolates the kanji prefix in lemma
    // and the corresponding reading prefix in lemmaReading. Then if surface
    // starts with that reading prefix, we know the prefix part comes from
    // the kanji stem, and the remaining surface kana is the inflection.
    //
    // Bows out (returns surface unchanged) when:
    //   - lemmaReading is empty (UniDic gave us nothing)
    //   - surface doesn't start with the reading prefix (irregular forms
    //     like 促音便 "いっ" against 行く / いく — surface starts with い
    //     which IS in the reading prefix, but only partially; the conjugation
    //     mangled the rest. We handle the partial-prefix case below.)
    std::wstring KanjifyByReading(const std::wstring& surface,
                                  const std::wstring& lemma,
                                  const std::wstring& lemmaReading)
    {
        if (surface.empty() || lemma.empty()) return surface;
        if (lemmaReading.empty())
        {
            // Fall back to the old tail-match heuristic when MeCab didn't
            // give us a reading. Same code as the original implementation.
            std::wstring kanji, hira;
            SplitLemma(lemma, kanji, hira);
            if (kanji.empty()) return surface;
            if (hira.empty())  return kanji;
            size_t maxMatch = (std::min)(surface.size(), hira.size());
            for (size_t n = maxMatch; n >= 1; --n)
            {
                if (surface.compare(surface.size() - n, n, hira, 0, n) == 0)
                    return kanji + surface.substr(surface.size() - n);
            }
            return surface;
        }

        // Strip matching kana tails off lemma and lemmaReading in parallel.
        // For 見る / みる: both end with る — strip one char from each →
        // lemmaStem = "見", readingStem = "み". For 燃える / もえる: both
        // end with る → strip one → "燃え" / "もえ"; then both end with え
        // → strip one → "燃" / "も"; then lemma ends with 燃 (kanji), stop.
        std::wstring lemmaStem   = lemma;
        std::wstring readingStem = lemmaReading;
        while (!lemmaStem.empty() && !readingStem.empty() &&
               IsHiragana(lemmaStem.back()) &&
               lemmaStem.back() == readingStem.back())
        {
            lemmaStem.pop_back();
            readingStem.pop_back();
        }
        // If we stripped everything, the lemma was pure kana — nothing to do.
        if (lemmaStem.empty()) return surface;

        // Surface must start with the kanji stem's reading for the alignment
        // to be meaningful. If it does, the rest of surface is inflection
        // kana that stays as-is.
        if (surface.size() >= readingStem.size() &&
            surface.compare(0, readingStem.size(), readingStem) == 0)
        {
            return lemmaStem + surface.substr(readingStem.size());
        }
        return surface;
    }
}

namespace bunsetsu
{

std::vector<Bunsetsu> SplitGreedy(const std::wstring& reading, const SkkDictionary& dict)
{
    std::vector<Bunsetsu> result;
    size_t pos = 0;
    while (pos < reading.size())
    {
        auto match = dict.FindLongestPrefix(reading, pos);
        if (match.length > 0)
        {
            Bunsetsu b;
            b.reading    = reading.substr(pos, match.length);
            b.candidates = std::move(match.candidates);
            // Fallback: SKK entries shouldn't be empty, but if a malformed
            // dictionary line slipped through Load we keep the literal kana
            // so JoinSelected never crashes on an empty candidates vector.
            if (b.candidates.empty()) b.candidates.push_back(b.reading);
            result.push_back(std::move(b));
            pos += match.length;
        }
        else
        {
            // No dictionary entry covers this position. Consume one char as
            // a literal-hiragana bunsetsu so we still produce a valid split.
            Bunsetsu b;
            b.reading    = reading.substr(pos, 1);
            b.candidates = { b.reading };
            result.push_back(std::move(b));
            pos += 1;
        }
    }
    return result;
}

std::wstring JoinSelected(const std::vector<Bunsetsu>& parts)
{
    std::wstring out;
    out.reserve(parts.size() * 4);
    for (const auto& b : parts) out += b.Selected();
    return out;
}

bool AnyHit(const std::vector<Bunsetsu>& parts)
{
    for (const auto& b : parts)
    {
        // A "hit" means the chosen rendering differs from the raw kana —
        // either a kanji, a multi-candidate entry, or just a one-candidate
        // entry whose text differs from the reading (e.g. "を" -> "を" is
        // not a hit, but "ぱそこん" -> "パソコン" is).
        if (b.Selected() != b.reading) return true;
        if (b.candidates.size() > 1) return true;
    }
    return false;
}

std::vector<Bunsetsu> SplitMecab(const std::wstring& reading,
                                 const MecabAnalyzer& analyzer,
                                 const SkkDictionary* skk)
{
    std::vector<Bunsetsu> result;
    auto morphemes = analyzer.Analyze(reading);
    if (morphemes.empty()) return result;

    for (const auto& m : morphemes)
    {
        Bunsetsu b;
        b.reading = m.surface;

        // Particles & auxiliaries: keep as the literal kana the user typed.
        // UniDic's lemma for 助詞/助動詞 IS the kana surface, so this is
        // technically a no-op for those — but we make the intent explicit
        // here so a future POS class doesn't accidentally kanji-substitute
        // a particle.
        const bool isParticle =
            m.pos == L"助詞" || m.pos == L"助動詞" || m.pos == L"記号";
        // Verbs only get surface-first treatment. UniDic gives back the
        // dictionary basic form as the lemma — "し" (連用形) -> "為る" —
        // which is a kanji nobody actually uses in modern writing, and
        // worse, sticking it in place of the surface changes the inflection
        // the user typed ("したひと" -> "為るた人"). Surface keeps "した".
        //
        // Adjectives are NOT in this bucket. Their UniDic lemma is the
        // canonical kanji form ("嫌らしい", "赤い") and is genuinely the
        // form the user usually wants. The basic-form / inflected-form
        // gap is small for adjectives in everyday writing (we don't see
        // many "あかかった -> 赤かった" cases vs the common "あかい -> 赤い").
        const bool isInflected = m.pos == L"動詞";

        if (isParticle)
        {
            b.candidates = { m.surface };
        }
        else if (isInflected)
        {
            // Verbs: build a kanji+kana form by mixing the lemma's kanji
            // stem ("食") with the surface's matched kana ending ("べ") to
            // get "食べ". When that succeeds (most regular conjugations)
            // it becomes the top candidate, beating the raw surface
            // "たべ". When the heuristic can't find a clean stitch
            // (promotional sounds — "いっ" / "すくっ" / "つっ" etc.) it
            // returns the surface unchanged, so we don't put garbage at
            // the front. The surface remains available as a fallback.
            std::wstring kanji = KanjifyByReading(m.surface, m.lemma, m.lemmaReading);
            if (kanji != m.surface)
            {
                b.candidates.push_back(kanji);
            }
            b.candidates.push_back(m.surface);
            if (skk && skk->IsLoaded())
            {
                auto skkCands = skk->Lookup(m.surface);
                for (auto& c : skkCands)
                {
                    if (std::find(b.candidates.begin(), b.candidates.end(), c)
                        == b.candidates.end())
                    {
                        b.candidates.push_back(std::move(c));
                    }
                }
            }
            if (!m.lemma.empty() && m.lemma != m.surface &&
                std::find(b.candidates.begin(), b.candidates.end(), m.lemma)
                == b.candidates.end())
            {
                b.candidates.push_back(m.lemma);
            }
        }
        else
        {
            // Nouns and friends: UniDic lemma is the canonical kanji form
            // (e.g. surface "あした" -> lemma "明日"). That's the right
            // top choice. Append SKK alternates and the bare surface as
            // fallbacks.
            if (!m.lemma.empty() && m.lemma != m.surface)
            {
                b.candidates.push_back(m.lemma);
            }
            if (skk && skk->IsLoaded())
            {
                auto skkCands = skk->Lookup(m.surface);
                for (auto& c : skkCands)
                {
                    if (std::find(b.candidates.begin(), b.candidates.end(), c)
                        == b.candidates.end())
                    {
                        b.candidates.push_back(std::move(c));
                    }
                }
            }
            if (std::find(b.candidates.begin(), b.candidates.end(), m.surface)
                == b.candidates.end())
            {
                b.candidates.push_back(m.surface);
            }
            if (b.candidates.empty()) b.candidates.push_back(m.surface);
        }
        result.push_back(std::move(b));
    }
    return result;
}

bool ReadsAs(const std::wstring& candidate,
             const std::wstring& expectedReading,
             const MecabAnalyzer& analyzer)
{
    if (candidate.empty() || expectedReading.empty()) return false;

    auto morphemes = analyzer.Analyze(candidate);
    if (morphemes.empty()) return false;

    std::wstring reading;
    reading.reserve(expectedReading.size());
    for (const auto& m : morphemes)
    {
        if (!m.pronunciation.empty()) reading += m.pronunciation;
        else                          reading += m.surface;
    }
    return reading == expectedReading;
}

bool LooksSuspect(const std::wstring& reading,
                  const MecabAnalyzer& analyzer)
{
    auto morphemes = analyzer.Analyze(reading);
    if (morphemes.empty()) return false;

    // Trigger A: lemma contains a kanji that's nearly always wrong in modern
    // writing. Fires regardless of morpheme count — "せいで" parses as exactly
    // 2 morphemes (所為 / で) and we still want to ask the LLM, so we can't
    // gate this on a 3+ split count.
    //
    // Multi-char compounds are checked char-by-char: a hit on "為" catches
    // both 所為 and 為に; a hit on 何 catches 如何. Expand as we hit new
    // "MeCab is being too literal" patterns in real usage.
    static const std::wstring kSuspect =
        L"顎為居出御様等処時故沢殆凡矢兎宛何嘗只迄謂勿論尤所如";

    for (const auto& m : morphemes)
    {
        for (wchar_t c : m.lemma)
        {
            if (kSuspect.find(c) != std::wstring::npos) return true;
        }
    }

    // Trigger B: input was long but MeCab shredded it into many small pieces.
    // Compound nouns, katakana loanwords, and proper nouns tend to fragment
    // this way (UniDic-Lite has no "中学生" so it gives back 中 / 学生 or
    // even 中 / 学 / 生). The LLM almost always recombines these correctly.
    if (morphemes.size() >= 5 && reading.size() >= 6) return true;

    return false;
}

std::vector<std::wstring> MergeMecabVerbForms(
    const std::wstring& reading,
    const MecabAnalyzer& analyzer,
    const std::vector<std::wstring>& skkCandidates)
{
    auto morphemes = analyzer.Analyze(reading);
    if (morphemes.empty()) return skkCandidates;

    // Confirm MeCab analyzed the whole reading. Partial coverage means
    // something's off (an unknown leading char, encoding mismatch, …) and
    // we don't want to splice a partial answer into the candidate list.
    std::wstring covered;
    for (const auto& m : morphemes) covered += m.surface;
    if (covered != reading) return skkCandidates;

    // Only intervene when at least one morpheme is inflected. Pure-noun
    // multi-morpheme inputs ("おべんとう"→お/弁当) and single-noun inputs
    // ("あめ"→雨) are exactly where SKK's whole-word match shines — its
    // hand-curated alternates (雨/飴/天) beat MeCab's single guess. We
    // keep SKK first for those.
    bool hasInflected = false;
    for (const auto& m : morphemes)
    {
        if (m.pos == L"動詞" || m.pos == L"形容詞") { hasInflected = true; break; }
    }
    if (!hasInflected) return skkCandidates;

    // Compose the combined top form using the same per-morpheme rules
    // SplitMecab uses (verbs go through KanjifyByReading; nouns &
    // adjectives use lemma; particles / auxiliaries stay as their kana
    // surface). Keep this in lockstep with SplitMecab's verb / particle
    // branches so the two paths agree on what "MeCab's top answer" means.
    std::wstring mecabTop;
    for (const auto& m : morphemes)
    {
        const bool isParticle =
            m.pos == L"助詞" || m.pos == L"助動詞" || m.pos == L"記号";
        if (isParticle)
        {
            mecabTop += m.surface;
        }
        else if (m.pos == L"動詞")
        {
            mecabTop += KanjifyByReading(m.surface, m.lemma, m.lemmaReading);
        }
        else
        {
            mecabTop += (m.lemma.empty() ? m.surface : m.lemma);
        }
    }

    // If MeCab produced nothing but the raw kana (KanjifyByReading bowed
    // out on every verb, lemma was empty everywhere) there's nothing to
    // prepend — let SKK win.
    if (mecabTop == reading) return skkCandidates;

    std::vector<std::wstring> merged;
    merged.reserve(1 + skkCandidates.size());
    merged.push_back(mecabTop);
    for (const auto& c : skkCandidates)
    {
        if (std::find(merged.begin(), merged.end(), c) == merged.end())
        {
            merged.push_back(c);
        }
    }
    return merged;
}

} // namespace bunsetsu
