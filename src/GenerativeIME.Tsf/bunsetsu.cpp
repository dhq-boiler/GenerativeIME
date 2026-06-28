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

    // Produces a kanji+hira form for a conjugated verb surface.
    // 例: ("たべ", "食べる")  -> "食べ"
    // 例: ("あるく", "歩く")  -> "歩く"
    // 例: ("もえ", "燃える")  -> "燃え"
    // 例: ("いっ", "行く")    -> "いっ"  (no usable suffix match, fall back)
    //
    // Heuristic: the tail of the surface that matches the start of the
    // lemma's hiragana suffix is the conjugated kana ending; whatever
    // precedes it stands in for the lemma's kanji stem. This works for
    // most regular conjugations but bows out on sound-change forms
    // (promotional 促音便 / イ音便 / ウ音便) where the surface kana
    // doesn't appear in the basic-form spelling.
    std::wstring KanjifyVerbSurface(const std::wstring& surface,
                                    const std::wstring& lemma)
    {
        if (surface.empty() || lemma.empty()) return surface;

        std::wstring kanji, hira;
        SplitLemma(lemma, kanji, hira);
        if (kanji.empty()) return surface;       // 全部ひらがなの lemma — nothing to add
        if (hira.empty())  return kanji;         // 全部漢字の lemma — use it directly

        // Walk match length from longest possible down to 1. The first
        // length where surface's tail equals hira's head wins.
        size_t maxMatch = (std::min)(surface.size(), hira.size());
        for (size_t n = maxMatch; n >= 1; --n)
        {
            if (surface.compare(surface.size() - n, n, hira, 0, n) == 0)
            {
                std::wstring suffix = surface.substr(surface.size() - n);
                return kanji + suffix;
            }
        }
        return surface;  // no match — keep what the user typed
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
            std::wstring kanji = KanjifyVerbSurface(m.surface, m.lemma);
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

} // namespace bunsetsu
