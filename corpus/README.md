# Corpus for Dictionary Expansion

## Purpose

Build a golden regression test that measures GenerativeIME's top-candidate
accuracy on **modern general-purpose Japanese**, since the ultimate goal is
MS-IME replacement for regular users (not developers only, not literary
scholars).

## Structure

- `aozora/raw/pairs-raw.tsv` — raw (reading, kanji) pairs extracted from
  Aozora Bunko ruby annotations. **Note**: Aozora is pre-1946 public-
  domain literature, so the vocabulary is heavily biased toward literary
  / archaic register (面皰, 邪智暴虐, 饑死, …). Retained as a *minority*
  corpus for archaic-register coverage, not the primary source.
- `aozora/raw/` — one raw dump per mining run
- `goldens/*.tsv` — aggregated top-N pairs by frequency across sources,
  the actual regression fixtures the tests read

## Sources & their trade-offs

| Source | modern-ness | ruby quality | fetch difficulty | license |
|---|---|---|---|---|
| Aozora Bunko | poor (literary) | excellent (hand-ruby) | easy | PD / CC-BY |
| Wikipedia article body | excellent | none (need MeCab) | easy (dump / API) | CC-BY-SA |
| Wikipedia article titles | good (proper nouns) | in-article reading | easy | CC-BY-SA |
| NHK News | excellent | none (need MeCab) | medium (scrape) | copyrighted |
| Twitter/X | excellent (colloquial) | none | hard (API paywall) | copyrighted |
| BCCWJ | excellent | good | hard (paid contract) | commercial |

## Current status

**Phase 0-1 (Aozora)**: pipeline verified with 218 pairs from
「走れメロス」 and 「羅生門」. The mining script (`scripts/mine/mine_aozora.ps1`)
handles UTF-8 with BOM (PS 5.1 constraint), cp932 decoding of downloaded
text, and both explicit-base `｜漢字《かんじ》` and bare `漢字《かんじ》`
ruby annotation forms. Fetch URL for Aozora is
`https://www.aozora.gr.jp/cards/<cardId>/files/<bookId>_ruby_<rubyRev>.zip`
— the `rubyRev` number can go stale (3 of 5 curated works 404'd on first
try). Regenerate against the current Aozora index if scaling up.

**Phase 0-2 (Wikipedia)**: pending — requires a `mecab_tokenize.exe`
helper (mecab.exe is not shipped by vcpkg, only the DLL). Design:

1. Compile `scripts/mine/mecab_tokenize.cpp` linking against vcpkg
   mecab + UniDic-Lite, reads UTF-8 stdin and outputs one
   `surface\treading\tpos` per token line.
2. `mine_wikipedia.ps1` calls the MediaWiki API
   (`https://ja.wikipedia.org/w/api.php?action=query&prop=extracts&explaintext=1&titles=…`)
   for ~100 seed articles, pipes bodies through `mecab_tokenize.exe`,
   filters `surface` for kanji content, aggregates.

**Phase 0-3 (Ollama gate)**: pending — script that iterates suspicious
SKK entries (heuristic: whole-hiragana readings whose top candidate is
rare kanji or okuri-ari-synthesized) and asks Ollama "in general modern
Japanese usage, what is the most common written form of `<reading>`?".
Output goes to `corpus/goldens/skk-rerank-candidates.tsv` for manual
review before applying.
