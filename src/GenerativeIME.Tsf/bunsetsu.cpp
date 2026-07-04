#include "bunsetsu.h"
#include "modernranking.h"
#include "skkdictionary.h"
#include "mecabanalyzer.h"

#include <algorithm>

namespace
{
    constexpr bool IsHiragana(wchar_t c) { return c >= 0x3040 && c <= 0x309F; }

    // Promote a pure-hiragana reading to katakana. We use this to offer the
    // katakana spelling as a candidate alongside the hiragana surface for
    // 助詞 / 助動詞 / 記号 — typing "は" should let the user pick "ハ" too,
    // not just "は" or "歯". Characters outside the hiragana plane pass
    // through unchanged so mixed input doesn't get mangled.
    std::wstring ToKatakana(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            if (c >= 0x3041 && c <= 0x3096) out.push_back(c + 0x60);
            else                            out.push_back(c);
        }
        return out;
    }

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

namespace
{
    // Auxiliary morphemes whose SKK top / MeCab lemma is a homophonic
    // kanji (ない→無い, ます→鱒, じゃ→邪, なかっ→無かっ, なく→無く, …)
    // that shadows the productive auxiliary the user typed. Both the
    // isParticle branch (promoteSkkTop) and the isInflected branch
    // (KanjifyByReading) must consult this list — a morpheme flagged
    // 助動詞 by MeCab lands in isParticle, one flagged 形容詞 lands in
    // isInflected, and 「ない」/「なかっ」 in particular appear in BOTH
    // depending on whether they follow a verb (助動詞) or an い-adjective
    // く-form (形容詞). Extracted from the two branch-local copies on
    // 2026-07-03 to keep them from drifting apart.
    bool IsShadowedAuxiliary(const std::wstring& surface)
    {
        static const std::wstring kList[] = {
            L"ない", L"ます", L"です", L"だっ", L"でし",
            L"まし", L"ませ", L"だろ",
            L"じゃ", L"なかっ", L"なく",
        };
        for (const auto& a : kList)
            if (surface == a) return true;
        return false;
    }

    // Greedy left-to-right pass: merge adjacent MeCab morphemes when their
    // joined reading has a whole-word SKK entry. Fixes UniDic-Lite over-
    // fragmentation on common compound nouns and short verbs that UniDic
    // shreds into 1-char pieces:
    //   がく + せい     -> がくせい (SKK: 学生/学制/楽聖)
    //   は + る         -> はる (SKK: 春/治/晴/…)
    //   ちゅう + がくせい -> ちゅうがくせい (SKK: 中学生)
    // Merged morpheme is tagged 名詞 with lemma == surface so the noun path
    // skips lemma promotion and lets SKK Lookup drive candidates.
    std::vector<MecabMorpheme> MergeAdjacentBySkk(
        std::vector<MecabMorpheme> in,
        const SkkDictionary* skk)
    {
        if (!skk || !skk->IsLoaded() || in.size() < 2) return in;
        std::vector<MecabMorpheme> out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); )
        {
            auto tryMake = [&](size_t span) -> bool {
                if (i + span > in.size()) return false;
                // Never merge across a 助詞 or 記号 boundary — those are
                // grammatical joints, not compound stems. Otherwise
                // "き(名詞)+が(助詞)+する(動詞)" collapses to "きが(名詞)"
                // just because SKK has "きが /飢餓/", and the top-candidate
                // path then serves "違う飢餓為る" instead of "違う気がする".
                //
                // 助動詞 boundary was blanket-forbidden until 2026-07-03,
                // but that trapped every 音便 past-tense form: UniDic-Lite
                // splits しんだ as [しん名詞 + だ助動詞], かった as
                // [かっ動詞 + た助動詞], and refused to reassemble them
                // even when SKK-JISYO.godan has the direct entry
                // (しんだ /死んだ/, かった /買った/勝った/). Narrow relief:
                // when the tail morpheme is one of the past-tense / te-form
                // auxiliaries (た/だ/て/で/たら/だら/たり/だり) AND the
                // joined reading is a direct SKK entry (not okuri-ari
                // flatten-through), merge is safe — those entries were
                // hand-curated for exactly this shape.
                static const std::wstring kMergableAux[] = {
                    L"た", L"だ", L"て", L"で", L"たら", L"だら", L"たり", L"だり",
                };
                for (size_t k = 0; k < span; ++k) {
                    const auto& p = in[i + k].pos;
                    if (p == L"助詞" || p == L"記号") return false;
                    if (p == L"助動詞") {
                        bool isTailAux = (k + 1 == span);
                        bool inList = false;
                        if (isTailAux) {
                            for (const auto& a : kMergableAux)
                                if (in[i + k].surface == a) { inList = true; break; }
                        }
                        if (!isTailAux || !inList) return false;
                    }
                }
                std::wstring joined;
                for (size_t k = 0; k < span; ++k) joined += in[i + k].surface;
                if (skk->Lookup(joined).empty()) return false;
                // For the 助動詞 relief branch, require a direct dict
                // entry (guards against okuri-ari flatten-through
                // synthesizing bogus past-tense forms).
                bool hasAux = false;
                for (size_t k = 0; k < span; ++k)
                    if (in[i + k].pos == L"助動詞") { hasAux = true; break; }
                if (hasAux && !skk->HasDirectEntry(joined)) return false;
                MecabMorpheme merged;
                merged.surface       = joined;
                merged.lemma         = joined;
                merged.lemmaReading  = joined;
                merged.pronunciation = joined;
                merged.pos           = L"名詞";
                out.push_back(std::move(merged));
                i += span;
                return true;
            };
            // Prefer the longer merge — triple first, then pair.
            if (tryMake(3)) continue;
            if (tryMake(2)) continue;
            out.push_back(std::move(in[i]));
            ++i;
        }
        return out;
    }
}

std::vector<Bunsetsu> SplitMecab(const std::wstring& reading,
                                 const MecabAnalyzer& analyzer,
                                 const SkkDictionary* skk)
{
    std::vector<Bunsetsu> result;
    auto morphemes = analyzer.Analyze(reading);
    if (morphemes.empty()) return result;
    morphemes = MergeAdjacentBySkk(std::move(morphemes), skk);

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
        // Verbs and adjectives get the KanjifyByReading stitch treatment.
        //
        // For verbs, UniDic returns dictionary basic form as the lemma —
        // "し" (連用形) -> "為る" — which is a kanji nobody actually uses
        // in modern writing, and worse, sticking it in place of the surface
        // changes the inflection the user typed ("したひと" -> "為るた人").
        // Surface keeps "した".
        //
        // Adjectives went through the noun branch until 2026-07-02 based on
        // "adjective inflection is rare in everyday writing" - but that was
        // wrong. User report: くわしく / 詳しく / わずらわしく / 恥ずかしく
        // etc. are 連用形 (く-form) and appear all the time. The 終止形
        // lemma (詳しい / 煩わしい) doesn't stitch to the 連用形 the user
        // typed. Same treatment as verbs: KanjifyByReading peels matching
        // kana tails off (lemma / lemmaReading) to isolate the kanji stem,
        // then rebuilds with the surface's kana ending. Works for all
        // regular い-adjectives because their lemma ends in い, reading
        // ends in い, both strip cleanly.
        const bool isInflected = m.pos == L"動詞" || m.pos == L"形容詞";

        if (isParticle)
        {
            // 助詞 / 助動詞 / 記号: hiragana surface first, then the
            // katakana version, then SKK kanji homophones at the tail
            // (歯 / 葉 / 羽 for "は"). The kana stays at the head so a
            // bare-Enter on "は" never silently picks "歯", but the
            // homophones are reachable with ↓ for the user who actually
            // means them.
            //
            // Exception (2026-07-02 fix): UniDic-Lite occasionally mislabels
            // a semantic noun as 助動詞 when the reading is ambiguous with
            // a verb suffix — 「はる」→ 助動詞 lemma「はる」, where the
            // user almost always wants 春. When pos == 助動詞 AND surface
            // is 2+ chars AND SKK's top for the surface reads back cleanly
            // as the surface via MeCab, promote SKK top over the kana.
            // Single-char 助詞 (は/を/に) is untouched. The ReadsAs filter
            // rejects okuri-ari-synthesized tops (「ですg /出過/」flattens
            // as SKK top「出過」whose MeCab reading is しゅつか, not です),
            // so most okuri-only shadow candidates are filtered out.
            //
            // Regression (2026-07-03 fix, BUG-5/6): 「ない」「ます」「です」
            // are high-confidence auxiliaries whose 訓読み-kanji form (無い/
            // 鱒/出す/…) IS the SKK top and DOES pass ReadsAs — so the
            // 2026-07-02 promotion path shipped 「食べ無い」「食べ鱒」as
            // real regressions on the most common inflections in Japanese.
            // A hard deny-list is the surgical fix: these tokens are never
            // what a modern IME user wants at bare-Enter — 鱒 as the trout
            // fish, 無い as the archaic literary negative, etc. are still
            // reachable via the fallback hits below, just not at the head.
            b.candidates = { m.surface };
            auto kata = ToKatakana(m.surface);
            if (kata != m.surface)
                b.candidates.push_back(std::move(kata));
            if (skk && skk->IsLoaded())
            {
                auto hits = skk->Lookup(m.surface);
                bool promoteSkkTop =
                    (m.pos == L"助動詞") &&
                    (m.surface.size() >= 2) &&
                    !IsShadowedAuxiliary(m.surface) &&
                    !hits.empty() &&
                    bunsetsu::ReadsAs(hits[0], m.surface, analyzer);
                if (promoteSkkTop)
                {
                    b.candidates.insert(b.candidates.begin(), hits[0]);
                }
                for (auto& c : hits)
                {
                    if (std::find(b.candidates.begin(), b.candidates.end(), c)
                        == b.candidates.end())
                    {
                        b.candidates.push_back(std::move(c));
                    }
                }
            }
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
            // Reject the archaic short-verb kanji lemmas UniDic-Lite hands us
            // for する/いる/ある/なる — 為る/居る/有る/成る are dictionary-correct
            // but modern writing uses the kana surface almost exclusively.
            // We check the lemma, not the stitched result, because conjugated
            // forms yield partial kanji ("い" surface + "居る" lemma stitches
            // to just "居", which isn't the archaic full form but still comes
            // from the same archaic reading). Without this, innsuto-ru's
            // MeCab-inferred い(居る未然形)+ん led to 居ん at the top.
            auto isArchaicShortVerbLemma = [](const std::wstring& l) {
                return l == L"為る" || l == L"居る" || l == L"有る" || l == L"成る";
            };
            // 2026-07-03: same auxiliary deny-list as the isParticle branch —
            // 形容詞+く+「ない」 flows through this branch (UniDic-Lite tags
            // the trailing ない as 形容詞 with lemma 無い, KanjifyByReading
            // then synthesizes 無い and puts it at head, yielding e.g.
            // 「美しく無い」/「大きく無い」. Keep the auxiliary as kana at head;
            // 無い is still reachable at the tail via the skkCands loop.
            if (kanji != m.surface &&
                !isArchaicShortVerbLemma(m.lemma) &&
                !IsShadowedAuxiliary(m.surface))
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

                // SKK keeps verb-stem alternates under okuri-ari entries
                // keyed by stem reading: "ふr /振/触/降/...". The
                // okuri-nashi lookup of "ふる" returns only "古". Recover
                // 振る / 触る / 降る by aligning lemma+lemmaReading the
                // same way KanjifyByReading does, then asking SKK for the
                // okuri-ari stems and reattaching the surface inflection.
                if (!m.lemma.empty() && !m.lemmaReading.empty())
                {
                    std::wstring ls = m.lemma, rs = m.lemmaReading;
                    while (!ls.empty() && !rs.empty() &&
                           IsHiragana(ls.back()) && ls.back() == rs.back())
                    {
                        ls.pop_back();
                        rs.pop_back();
                    }
                    // rs is now the kanji-stem's reading (e.g. "ふ"). The
                    // surface must start with that reading for the
                    // reattachment to be sound — promotional 促音便 like
                    // "ふっ" wouldn't (no rs alignment), and we'd just skip.
                    if (!rs.empty() &&
                        m.surface.size() >= rs.size() &&
                        m.surface.compare(0, rs.size(), rs) == 0)
                    {
                        std::wstring tail = m.surface.substr(rs.size());
                        auto okuriStems = skk->LookupOkuri(rs);
                        for (const auto& stem : okuriStems)
                        {
                            std::wstring full = stem + tail;
                            if (std::find(b.candidates.begin(), b.candidates.end(), full)
                                == b.candidates.end())
                            {
                                b.candidates.push_back(std::move(full));
                            }
                        }
                    }
                }
            }
            if (!m.lemma.empty() && m.lemma != m.surface &&
                std::find(b.candidates.begin(), b.candidates.end(), m.lemma)
                == b.candidates.end())
            {
                b.candidates.push_back(m.lemma);
            }
            // Modern-usage top override for inflected forms, applied in a
            // deliberately conservative "move-only" mode: promote the
            // corpus-preferred kanji ONLY when it's already a candidate for
            // this exact surface (never front-insert). This fixes
            // つかって→浸かって (使っ is present via the SKK okuri-ari path
            // above, but the KanjifyByReading lemma 浸かる sat at the head)
            // while the ≥2-mora guard keeps single-mora 連用形 like「し」
            // from pulling in noun-usage overrides (し→市) that belong to
            // the noun branch. GetPreferred returns empty for readings not
            // in the table, so most verbs are untouched.
            // IsShadowedAuxiliary guard: kTable is noun-frequency-derived, so
            // auxiliary surfaces like ない/ます/です would be rewritten to their
            // noun homophones (ない→内 etc.) — block those explicitly.
            if (m.surface.size() >= 2 && !IsShadowedAuxiliary(m.surface))
            {
                std::wstring pref = modernranking::GetPreferred(m.surface);
                if (!pref.empty())
                {
                    auto pit = std::find(b.candidates.begin(), b.candidates.end(), pref);
                    if (pit != b.candidates.end() && pit != b.candidates.begin())
                    {
                        b.candidates.erase(pit);
                        b.candidates.insert(b.candidates.begin(), pref);
                    }
                }
            }
        }
        else
        {
            // Nouns and friends: UniDic lemma is the canonical kanji form
            // (e.g. surface "あした" -> lemma "明日"). That's the right
            // top choice. Append SKK alternates and the bare surface as
            // fallbacks.
            //
            // Exception: UniDic flags 感動詞 / フィラー like 「ん」 and
            // 「う」 with lemmas 「んー」 / 「うう」 — a phonetic-stretched
            // version of the surface that the user never typed and never
            // wants. Skip promoting the lemma when it's pure hiragana
            // (plus 長音記号 ー) longer than the surface AND contains the
            // surface — that pattern catches exactly those filler
            // stretches without touching legitimate noun lemmas (which
            // contain kanji). ー is U+30FC, outside the hiragana plane,
            // so it has to be allowlisted explicitly or 「ん→んー」 slips
            // through.
            auto isFillerKana = [](const std::wstring& s) {
                if (s.empty()) return false;
                for (wchar_t c : s) {
                    if ((c < 0x3041 || c > 0x309F) && c != L'ー') return false;
                }
                return true;
            };
            bool lemmaIsStretchedSurface =
                isFillerKana(m.lemma) &&
                m.lemma.size() > m.surface.size() &&
                m.lemma.find(m.surface) != std::wstring::npos;

            // SKK candidates for the surface. We may prefer SKK's top over
            // MeCab's lemma — UniDic-Lite occasionally returns pronoun-class
            // kanji (其れ / 彼 / 此処) as lemma for common hiragana surfaces
            // (ほん / かれ / ここ) where the user almost always wants the
            // noun homophone (本 / 彼女 / etc.) instead.
            //
            // GOTCHA: SkkDictionary::Load flattens okuri-ari stem entries
            // (e.g.「あかるi /明/」i-adjective synthesis, 「ですg /出過/」
            // verb 出過ぐ stem) into m_entries as SYNTHESIZED tops like
            // 「明い」and 「出過」. Those *look* like SKK candidates but
            // do NOT read back as the surface (「明い」reads as あかい,
            // not あかるい; 「出過」reads as しゅつか, not です). Promoting
            // them blindly regresses adjective + auxiliary readings.
            // Filter with `ReadsAs(top, surface, analyzer)` — only promote
            // when MeCab confirms the top actually reads as the surface.
            std::vector<std::wstring> skkCands;
            if (skk && skk->IsLoaded())
                skkCands = skk->Lookup(m.surface);

            bool skkTopIsCleanForSurface =
                !skkCands.empty() &&
                bunsetsu::ReadsAs(skkCands[0], m.surface, analyzer);

            // 1. SKK top (if it survives the ReadsAs filter).
            if (skkTopIsCleanForSurface)
                b.candidates.push_back(skkCands[0]);

            // 2. MeCab lemma, unless it's the filler-stretched form or
            //    duplicates something already in the list.
            if (!lemmaIsStretchedSurface &&
                !m.lemma.empty() && m.lemma != m.surface &&
                std::find(b.candidates.begin(), b.candidates.end(), m.lemma)
                == b.candidates.end())
            {
                b.candidates.push_back(m.lemma);
            }

            // 3. Rest of SKK candidates. If skkTop wasn't promoted (didn't
            //    pass ReadsAs), start from index 0 so it lands after lemma
            //    rather than getting dropped entirely.
            size_t skkStart = skkTopIsCleanForSurface ? 1 : 0;
            for (size_t k = skkStart; k < skkCands.size(); ++k)
            {
                if (std::find(b.candidates.begin(), b.candidates.end(),
                              skkCands[k]) == b.candidates.end())
                {
                    b.candidates.push_back(std::move(skkCands[k]));
                }
            }

            // 4. Surface kana as a final fallback.
            if (std::find(b.candidates.begin(), b.candidates.end(), m.surface)
                == b.candidates.end())
            {
                b.candidates.push_back(m.surface);
            }
            if (b.candidates.empty()) b.candidates.push_back(m.surface);

            // 5. Modern-usage top override. Corpus mining (see
            //    scripts/mine/probe_skk_coverage.ps1) shows raw SKK ranks
            //    single-mora suffix readings by standalone-word frequency
            //    (だい→大, し→死, けん→件), while modern IME users almost
            //    always want them as suffixes (だい→第, し→市, けん→県).
            //    Applied only in the noun branch so 助詞 kana tops (は/を/に)
            //    aren't disturbed.
            b.candidates = modernranking::PromoteToTop(m.surface, std::move(b.candidates));
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

    // Sentence-final は ↔ わ equivalence: greetings like
    // 「こんにちわ /今日は/」 spell the 助詞 as は in the candidate but
    // users type it as わ (romaji "wa"). Treat the two as equivalent
    // when they appear at the sentence end so this SKK entry survives
    // the direct-hit path's ReadsAs filter. Applies only to the LAST
    // character so 「はな→わな」 style shifts don't leak into normal
    // homophone lookups.
    auto normalizeTailHaWa = [](std::wstring s) {
        if (!s.empty() && s.back() == L'わ') s.back() = L'は';
        return s;
    };
    return normalizeTailHaWa(reading) == normalizeTailHaWa(expectedReading);
}

bool LooksSuspect(const std::wstring& reading,
                  const MecabAnalyzer& analyzer)
{
    // Trigger C (cheap, runs before any MeCab work): multiple 長音 marks
    // are a strong 外来語 signal — "えくすくらめーしょんまーく" has two
    // ー and UniDic-Lite has no entry for it, so the split shreds into
    // nonsense ("えー樟眩め～小んーまーく"). One ー is fine (common in
    // single katakana words MeCab knows), two suggests at least two
    // foreign words concatenated, which is exactly what the LLM
    // recombines well.
    {
        int choonpu = 0;
        for (wchar_t c : reading)
            if (c == L'ー') ++choonpu;
        if (choonpu >= 2) return true;
    }

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
        L"顎為居出御様等処時故沢殆凡矢兎宛何嘗只迄謂勿論尤所如唯";

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

    // Trigger E: 2-morpheme hiragana inputs where UniDic-Lite misanalyzes
    // a 五段 verb's 撥音便/促音便 stem as a noun / pronoun / 連体詞 /
    // 感動詞 instead of a 動詞. Real-world misses observed:
    //   しんだ → シン(名詞) + だ        (want: 死んだ)
    //   ふんだ → 其れ(代名詞) + だ      (want: 踏んだ)
    //   かんだ → 彼(代名詞) + だ        (want: 噛んだ)
    //   もんだ → 物(名詞) + だ          (want: 揉んだ)
    //   あんだ → 餡(名詞) + だ          (want: 編んだ)
    //   うんだ → うん(感動詞) + だ      (want: 産んだ)
    //   しんで → 芯(名詞) + だ          (want: 死んで)
    // Conditions kept narrow to avoid disturbing legitimate parses:
    //   - exactly 2 morphemes
    //   - morpheme[0].pos is not 動詞 / 形容詞 (= MeCab missed the verb)
    //   - morpheme[0].surface contains ん or っ (撥音便/促音便 marker)
    //   - morpheme[1].surface ∈ {だ, た, で, て} (past / connective aux)
    // Negative checks confirmed: ぱんだ / うんちく / せんだ → 1 morpheme
    // (skipped by size check); あんち → tail is ち (skipped by aux check);
    // のんだ / よんだ / すんだ / くんだ → morpheme[0].pos == 動詞
    // (skipped by pos check, so正常な MeCab 動詞解析は触らない).
    if (morphemes.size() == 2)
    {
        const auto& a = morphemes[0];
        const auto& b = morphemes[1];
        const bool tailIsAux =
            b.surface == L"だ" || b.surface == L"た" ||
            b.surface == L"で" || b.surface == L"て";
        const bool hasOnbinMarker =
            a.surface.find(L'ん') != std::wstring::npos ||
            a.surface.find(L'っ') != std::wstring::npos;
        const bool notVerbal = (a.pos != L"動詞" && a.pos != L"形容詞");
        if (tailIsAux && hasOnbinMarker && notVerbal) return true;
    }

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
            std::wstring k = KanjifyByReading(m.surface, m.lemma, m.lemmaReading);
            // Modern-usage override for homophonic 音便 stems. UniDic-Lite
            // picks a dictionary-correct but low-frequency lemma for some
            // て/た forms — つかっ → lemma 浸かる, so the stitch yields 浸かっ
            // and the whole reading「つかってよい」merges as「浸かって良い」.
            // Corpus frequency wants 使っ. When modernranking has a preferred
            // surface for this exact morpheme reading, use it in place of the
            // lemma stitch. The ≥2-mora guard keeps single-mora 連用形 (し/き/
            // …) from pulling in noun-usage overrides (し→市) that belong to
            // the noun path, matching SplitMecab's verb-branch guard.
            // IsShadowedAuxiliary guard: same reason as SplitMecab — block
            // ない/ます/です from being rewritten to their noun homophones.
            if (m.surface.size() >= 2 && !IsShadowedAuxiliary(m.surface))
            {
                std::wstring pref = modernranking::GetPreferred(m.surface);
                if (!pref.empty()) k = pref;
            }
            mecabTop += k;
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

std::wstring ToKatakanaPublic(const std::wstring& s)
{
    return ToKatakana(s);
}

Bunsetsu MakeBunsetsuFromReading(const std::wstring& reading,
                                 const MecabAnalyzer* analyzer,
                                 const SkkDictionary* skk)
{
    Bunsetsu b;
    b.reading = reading;
    b.selected = 0;
    if (reading.empty()) { b.candidates.push_back(reading); return b; }

    // 1. The reading itself first. Resize operations can land on short
    //    readings like "は" / "が" where SKK lookup returns kanji
    //    homophones (歯 / 葉 / 羽) — those should be available but never
    //    the default, because the user almost certainly wants the kana
    //    they typed. Pushing the reading at the head also guarantees
    //    JoinSelected never has an empty Selected() to concatenate.
    b.candidates.push_back(reading);

    // 2. Katakana version (when the reading is pure hiragana). Gives the
    //    user a one-keystroke katakana spelling without scrolling past
    //    the kanji homophones.
    auto kata = ToKatakana(reading);
    if (kata != reading)
        b.candidates.push_back(std::move(kata));

    // 3. MeCab joined form — handles inflected verbs / phrases SKK doesn't
    //    have as a single key ("みた" → 見た). Only adopt the combined form
    //    when it differs from the raw reading (otherwise we'd just push the
    //    kana again).
    if (analyzer)
    {
        auto morphemes = analyzer->Analyze(reading);
        std::wstring combined;
        for (const auto& m : morphemes)
        {
            const bool isParticle =
                m.pos == L"助詞" || m.pos == L"助動詞" || m.pos == L"記号";
            if (isParticle)
                combined += m.surface;
            else if (m.pos == L"動詞")
                combined += KanjifyByReading(m.surface, m.lemma, m.lemmaReading);
            else
                combined += (m.lemma.empty() ? m.surface : m.lemma);
        }
        if (!combined.empty() && combined != reading &&
            std::find(b.candidates.begin(), b.candidates.end(), combined)
            == b.candidates.end())
        {
            b.candidates.push_back(std::move(combined));
        }
    }

    // 4. SKK whole-reading lookup last — homophones (雨/飴/天 for "あめ",
    //    歯/葉/羽 for "は") are useful alternates but ranked below the
    //    typed kana and the MeCab joined form.
    if (skk && skk->IsLoaded())
    {
        auto hits = skk->Lookup(reading);
        if (analyzer)
            hits = MergeMecabVerbForms(reading, *analyzer, hits);
        for (auto& c : hits)
        {
            if (std::find(b.candidates.begin(), b.candidates.end(), c)
                == b.candidates.end())
            {
                b.candidates.push_back(std::move(c));
            }
        }
    }

    return b;
}

} // namespace bunsetsu
