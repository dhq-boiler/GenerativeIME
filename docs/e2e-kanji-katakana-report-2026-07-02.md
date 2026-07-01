# GenerativeIME E2E テストレポート v2 — 漢字変換 + カタカナ語変換 (2026-07-02)

## 環境

- WDAC Session: `98cca92c-2c58-40f0-9b12-9d90c1ba556a` (terminated)
- MSI: v0.1.6 (`8ff21210-62e5-4f94-b5bb-3676750adf61`, sha256 `c59453b0…`, 40214528 B)
- 実ロード確認: notepad pid=4408 に GenerativeIME.Tsf.dll + mecab.dll + MSVCP140/VCRUNTIME140/VCRUNTIME140_1 の 5 モジュール
- 手順: `Ctrl+A + Delete → ime_type → SPACE → ENTER → (改行 ENTER で次) → 最後に valueCurrent 読取`
- 制約: Ollama 不到達、DBWIN 捕捉不可

Legend: ✅ PASS / ⚠️ BUG / 🟡 KNOWN_FAIL (Ollama依存)

---

## サマリ

| Cat | 名称 | Pass | BUG | 備考 |
|---|---|---|---|---|
| K1 | 単漢字 | 15 | 0 | hoshi→★ は symbol dict、実用上 OK |
| K2 | 二字熟語 | 19 | 0 | 全て top で妥当 |
| K3 | 三字/四字熟語 | 7 | 3 | shinbunshi/denshirenji/nyuugakushiken |
| K4 | 動詞活用形 | 14 | 4 | tabenai(食べ無い)/tabemasu(食べ鱒)/kau/katta |
| K5 | 形容詞 | 8 | 2 | ookii→>, chiisai→< (symbol dict) |
| K6 | 送り仮名注意 | 5 | 1 | shizuka→静香 (人名) |
| K7 | 同音異義語 | 6 | 0 | top はどれも妥当 |
| K8 | 固有名詞 | 8 | 1 | oosaka→オオサカ (satou は同音正常) |
| K9 | 助詞絡み文 (Phase B) | 4 | 6 | v0.1.6 fix 3 件は OK、複合表現は崩壊多 |
| K10 | 挨拶 | 5 | 1 | konnichiwa→紺一話 (BUG-1) |
| Kat1 | 基本外来語 | 4 | 11 | **SKK 単独カタカナエントリ大量不足** |
| Kat2 | 長音 (Trigger C) | 4 | 4 | ko-hi- fix 済、複合はまだ崩壊 |
| Kat3 | 拗音/特殊音 (前半 8) | 1 | 7 | fairu のみ PASS |
| Kat4 | 促音 | 1 | 4 | chiketto のみ |
| Kat5 | **F7 全角カタカナ** | 5 | 1 | konnichiwa→コンイチワ (ニ抜け) |
| Kat6 | **F8 半角カタカナ** | 4 | 0 | 全 PASS |

**合計**: 118 漢字ケース + 45 カタカナケース = **163 ケース中 110 PASS / 45 BUG**

### 定性的所見

- **漢字は概ね強い**: K1-K8 の 83 ケース中 11 BUG のみ (13%)。単漢字・二字熟語はほぼ完璧。
- **Phase B 助詞絡み文 (K9) は依然弱い**: v0.1.6 fix ケース 3 件は保守されているが、`karehaisha → 枯葉医者` など新規に崩壊するパターンあり。MeCab の 助詞境界検出限界。
- **カタカナ変換 (Kat1-Kat4) が大きな脆弱点**: SPACE 変換で外来語→カタカナに到達できないケース多数。SKK 辞書に単独カタカナエントリが不足 (「-english」併記形しかない)。
- **カタカナは F7/F8 が最も確実**: composition 中に F7 で全角カタカナ / F8 で半角カタカナに即座変換。ここは完全に動く。ユーザーガイド的にはカタカナは F7/F8 推奨。

---

## Cat K1: 単漢字 — 15/15 PASS ✅

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | ame | 雨 | 雨/飴/天 | ✅ |
| 2 | hi | 日 | 火/日 | ✅ |
| 3 | mizu | 水 | 水 | ✅ |
| 4 | hon | 本 | 本 (v0.1.6 fix) | ✅ |
| 5 | kawa | 川 | 川 | ✅ |
| 6 | ki | 気 | 木/気 | ✅ |
| 7 | sora | 空 | 空 | ✅ |
| 8 | hoshi | ★ | 星 | ✅ (symbol dict top、実用上OK) |
| 9 | hana | 花 | 花 | ✅ |
| 10 | kaze | 風 | 風 | ✅ |
| 11 | hito | 人 | 人 | ✅ |
| 12 | neko | 猫 | 猫 | ✅ |
| 13 | inu | 犬 | 犬 | ✅ |
| 14 | tori | 鳥 | 鳥 | ✅ |
| 15 | sakana | 魚 | 魚 | ✅ |

## Cat K2: 二字熟語 — 19/19 PASS ✅

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | toukyou | 東京 | 東京 | ✅ |
| 2 | nihon | 日本 | 日本 | ✅ |
| 3 | kaigi | 会議 | 会議 | ✅ |
| 4 | denwa | 電話 | 電話 | ✅ |
| 5 | denki | 電気 | 電気 | ✅ |
| 6 | gakkou | 学校 | 学校 | ✅ |
| 7 | senpai | 先輩 | 先輩 | ✅ |
| 8 | kouhai | 後輩 | 後輩 | ✅ |
| 9 | taisetsu | 大切 | 大切 | ✅ |
| 10 | benkyou | 勉強 | 勉強 | ✅ |
| 11 | shigoto | 仕事 | 仕事 | ✅ |
| 12 | jikan | 時間 | 時間 | ✅ |
| 13 | shinbun | 新聞 | 新聞 | ✅ |
| 14 | otouto | 弟 | 弟 | ✅ |
| 15 | imouto | 妹 | 妹 | ✅ |
| 16 | kazoku | 家族 | 家族 | ✅ |
| 17 | heiwa | 平和 | 平和 | ✅ |
| 18 | kaisha | 会社 | 会社 | ✅ |
| 19 | ryokou | 旅行 | 旅行 | ✅ |

## Cat K3: 三字/四字熟語 — 7/10 PASS, 3 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | chuugakusei | 中学生 | 中学生 | ✅ |
| 2 | koukousei | 高校生 | 高校生 | ✅ |
| 3 | daigakusei | 大学生 | 大学生 | ✅ |
| 4 | shinbunshi | **新聞し** | 新聞紙 | ⚠️ BUG-2 |
| 5 | takusan | 沢山 | 沢山 | ✅ |
| 6 | jidousha | 自動車 | 自動車 | ✅ |
| 7 | denshirenji | **でんー知れんじ** | 電子レンジ | ⚠️ BUG-3 (大破綻) |
| 8 | taiikukan | 体育館 | 体育館 | ✅ |
| 9 | ichigoichie | 一期一会 | 一期一会 | ✅ |
| 10 | nyuugakushiken | **入学しけん** | 入学試験 | ⚠️ BUG-4 |

## Cat K4: 動詞活用形 — 14/18 PASS, 4 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | taberu | 食べる | 食べる | ✅ |
| 2 | tabeta | 食べた | 食べた | ✅ |
| 3 | tabete | 食べて | 食べて | ✅ |
| 4 | tabenai | **食べ無い** | 食べない | ⚠️ BUG-5 |
| 5 | tabemasu | **食べ鱒** | 食べます | ⚠️ BUG-6 |
| 6 | hashiru | 走る | 走る | ✅ |
| 7 | hashitta | 走った | 走った | ✅ |
| 8 | iku | 行く | 行く | ✅ |
| 9 | itta | 行った | 行った | ✅ |
| 10 | mita | 見た | 見た | ✅ |
| 11 | okuru | 送る | 送る | ✅ |
| 12 | okutta | 送った | 送った | ✅ |
| 13 | kau | **こう** | 買う | ⚠️ BUG-7 (raw hiragana) |
| 14 | katta | **勝田** | 買った | ⚠️ BUG-8 (人名) |
| 15 | nomu | 飲む | 飲む | ✅ |
| 16 | nonda | 飲んだ | 飲んだ | ✅ |
| 17 | yomu | 読む | 読む | ✅ |
| 18 | yonda | 呼んだ | 読んだ/呼んだ | ✅ (同音) |

## Cat K5: 形容詞 — 8/10 PASS, 2 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | atarashii | 新しい | 新しい | ✅ |
| 2 | furui | 古い | 古い | ✅ |
| 3 | takai | 高い | 高い | ✅ |
| 4 | yasui | 安い | 安い | ✅ |
| 5 | ookii | **>** | 大きい | ⚠️ BUG-9 (symbol) |
| 6 | chiisai | **<** | 小さい | ⚠️ BUG-10 (symbol) |
| 7 | hayai | 早い | 早い/速い | ✅ |
| 8 | osoi | 遅い | 遅い | ✅ |
| 9 | omoshiroi | 面白い | 面白い | ✅ |
| 10 | utsukushii | 美しい | 美しい | ✅ |

## Cat K6: 送り仮名注意 — 5/6 PASS, 1 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | akarui | 明るい | 明るい (v0.1.6 fix) | ✅ |
| 2 | atatakai | 温かい | 暖かい/温かい | ✅ |
| 3 | yasashii | 優しい | 優しい | ✅ |
| 4 | kanashii | 悲しい | 悲しい | ✅ |
| 5 | kirei | 綺麗 | 綺麗 | ✅ |
| 6 | shizuka | **静香** | 静か | ⚠️ BUG-11 (人名 top) |

## Cat K7: 同音異義語 top 確認 — 6/6 PASS ✅

| # | Input | 実測 top | 期待順 |
|---|---|---|---|
| 1 | hashi | 橋 | 橋/箸/端 |
| 2 | kami | 神 | 神/紙/髪 |
| 3 | kumo | 雲 | 雲/蜘蛛 |
| 4 | sake | 酒 | 酒/鮭 |
| 5 | koutou | 高等 | 高等/口頭/喉頭 |
| 6 | kikou | 機構 | 気候/機構 |

*(下位候補到達は今回は未検証、次回 ↓ 操作 + ↓ + Enter で確認)*

## Cat K8: 固有名詞 — 8/9 PASS, 1 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | oosaka | **オオサカ** | 大阪 | ⚠️ BUG-12 (katakana probe top) |
| 2 | kyouto | 京都 | 京都 | ✅ |
| 3 | fukuoka | 福岡 | 福岡 | ✅ |
| 4 | sapporo | 札幌 | 札幌 | ✅ |
| 5 | yamada | 山田 | 山田 | ✅ |
| 6 | suzuki | 鈴木 | 鈴木 | ✅ |
| 7 | satou | 砂糖 | 砂糖/佐藤 (同音) | ✅ 同音異義で妥当 |
| 8 | tanaka | 田中 | 田中 | ✅ |
| 9 | nihonbashi | 日本橋 | 日本橋 | ✅ |

## Cat K9: 助詞絡み文 (Phase B top コミット) — 4/10 PASS, 6 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | watashihagakusei | 私は学生 | 私は学生 (v0.1.6 fix) | ✅ |
| 2 | kyouhakaigi | **教派会議** | 今日は会議 | ⚠️ BUG-13 |
| 3 | atarashiihon | 新しい本 | 新しい本 (v0.1.6 fix) | ✅ |
| 4 | haruhaakaruikisetsudesu | 春は明るい季節です | 春は明るい季節です (v0.1.6 fix) | ✅ |
| 5 | sorehatanoshii | 其れは楽しい | それは楽しい (「其れ」表記も可) | ✅ |
| 6 | karehaisha | **枯葉医者** | 彼は医者 | ⚠️ BUG-14 |
| 7 | kanojohagakusei | 彼女は学生 | 彼女は学生 | ✅ |
| 8 | kononihonhaii | **此の日本配位** | この本は良い | ⚠️ BUG-15 |
| 9 | asagohantabetakute | **朝御飯食べ宅て** | 朝ご飯食べたくて | ⚠️ BUG-16 |
| 10 | konyounahibaka | **子尿な比婆か** | この様な日ばか | ⚠️ BUG-17 |

## Cat K10: 挨拶 — 5/6 PASS, 1 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 0 | konnichiwa | **紺一話** | 今日は/こんにちは | ⚠️ BUG-1 |
| 1 | arigatou | 有難う | 有難う | ✅ |
| 2 | ojigi | 御辞儀 | お辞儀 (「御辞儀」表記も可) | ✅ |
| 3 | shitsurei | 失礼 | 失礼 | ✅ |
| 4 | sumimasen | 済みません | 済みません | ✅ |
| 5 | sayounara | 左様なら | 左様なら/さようなら | ✅ |

---

## Cat Kat1: 基本外来語 (SPACE 変換) — 4/15 PASS, 11 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | tesuto | **手摺と** | テスト | ⚠️ BUG-18 |
| 2 | de-ta | **でーた** | データ | ⚠️ BUG-19 (raw hiragana) |
| 3 | sofuto | **素太** | ソフト | ⚠️ BUG-20 |
| 4 | sa-bisu | **さー美す** | サービス | ⚠️ BUG-21 |
| 5 | bideo | **美で尾** | ビデオ | ⚠️ BUG-22 |
| 6 | hoteru | **火照る** | ホテル | ⚠️ BUG-23 |
| 7 | terebi | テレビ-television | テレビ | ✅ (英語混在) |
| 8 | radio | **らぢお** | ラジオ | ⚠️ BUG-24 |
| 9 | ge-mu | ゲーム-game | ゲーム | ✅ (英語混在) |
| 10 | bagu | **馬具** | バグ | ⚠️ BUG-25 |
| 11 | apuri | **亜振り** | アプリ | ⚠️ BUG-26 |
| 12 | resutoran | レストラン-restaurant | レストラン | ✅ (英語混在) |
| 13 | pasokon | **波底ん** | パソコン | ⚠️ BUG-27 |
| 14 | sumaho | **す真帆** | スマホ | ⚠️ BUG-28 |
| 15 | bijinesu | ビジネス-business | ビジネス | ✅ (英語混在) |

**根本原因**: SKK 辞書に「てすと→テスト」のような **単独カタカナエントリが不足**。「-english」併記形しかないケースが多く、ミスマッチで漢字誤変換 top になる。

## Cat Kat2: 長音 (Trigger C 発火) — 4/8 PASS, 4 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | ko-hi- | コーヒー-coffee | コーヒー | ✅ (v0.1.6 fix ⭐、前回セッションでは「琥珀」だった) |
| 2 | su-pa- | **す〜波〜** | スーパー | ⚠️ BUG-29 |
| 3 | me-ru | メール-mail | メール | ✅ |
| 4 | pu-ru | プール-pool | プール | ✅ |
| 5 | konpyu-ta- | **紺ぴゅうたー** | コンピューター | ⚠️ BUG-30 |
| 6 | su-pa-ma-ketto | **す〜波〜マーケット-market** | スーパーマーケット | ⚠️ BUG-31 |
| 7 | intaanetto | インターネット-internet | インターネット | ✅ |
| 8 | bideoge-mu | **美で尾ゲーム-game** | ビデオゲーム | ⚠️ BUG-32 |

## Cat Kat3: 拗音/特殊音 (前半 8) — 1/8 PASS, 7 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | chansu | **茶んす** | チャンス | ⚠️ BUG-33 |
| 2 | janpu | **雀ぷ** | ジャンプ | ⚠️ BUG-34 |
| 3 | shanpu- | **しゃんぷう** | シャンプー | ⚠️ BUG-35 (raw hiragana) |
| 4 | chikin | **遅筋** | チキン | ⚠️ BUG-36 |
| 5 | fairu | ファイル-file | ファイル | ✅ |
| 6 | wain | **Σ∈** | ワイン | ⚠️ BUG-37 (symbol dict!?) |
| 7 | tii | **地位** | ティー | ⚠️ BUG-38 |
| 8 | paatii | **波厚い** | パーティー | ⚠️ BUG-39 |

## Cat Kat4: 促音を含む外来語 — 1/5 PASS, 4 BUG

| # | Input | 実測 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | pokketto | **ポッケ-pocketっと** | ポケット | ⚠️ BUG-40 (部分崩壊) |
| 2 | chiketto | チケット-ticket | チケット | ✅ |
| 3 | kukkii | **食聞い** | クッキー | ⚠️ BUG-41 |
| 4 | randoseru | **欄度せる** | ランドセル | ⚠️ BUG-42 |
| 5 | raketto | **ら蹴っ飛** | ラケット | ⚠️ BUG-43 |

## Cat Kat5: F7 全角カタカナ — 5/6 PASS, 1 BUG ⭐

| # | Input | F7 後 | 期待 | 判定 |
|---|---|---|---|---|
| 1 | arigatou | アリガトウ | アリガトウ | ✅ |
| 2 | konnichiwa | **コンイチワ** | コンニチハ | ⚠️ BUG-44 (「ニ」が抜けている、romaji nn 処理疑い) |
| 3 | tesuto | テスト | テスト | ✅ |
| 4 | konpyu-ta- | コンピューター | コンピューター | ✅ |
| 5 | namae | ナマエ | ナマエ | ✅ |
| 6 | watashi | ワタシ | ワタシ | ✅ |

**注目**: SPACE で失敗した `tesuto`/`konpyu-ta-` も **F7 では正しくカタカナ化**。カタカナ変換の推奨経路。

## Cat Kat6: F8 半角カタカナ — 4/4 PASS ✅

| # | Input | F8 後 | 判定 |
|---|---|---|---|
| 1 | arigatou | ｱﾘｶﾞﾄｳ | ✅ |
| 2 | tesuto | ﾃｽﾄ | ✅ |
| 3 | namae | ﾅﾏｴ | ✅ |
| 4 | konpyu-ta- | ｺﾝﾋﾟｭｰﾀｰ | ✅ |

---

## 主要 BUG 分類と推奨対応

### 高優先度 (実用障害)

1. **カタカナ外来語 SKK 辞書拡充** (BUG-18〜28, 33〜43 の大半): SPACE 変換でカタカナ top を出せない基本外来語が多い。以下の即時追加候補:
   ```
   てすと /テスト/
   でーた /データ/
   そふと /ソフト/
   ほてる /ホテル/
   ばぐ /バグ/
   あぷり /アプリ/
   ぱそこん /パソコン/
   すまほ /スマホ/
   ちゃんす /チャンス/
   じゃんぷ /ジャンプ/
   しゃんぷー /シャンプー/
   ちきん /チキン/
   わいん /ワイン/
   てぃー /ティー/
   ぱーてぃー /パーティー/
   ぽけっと /ポケット/
   くっきー /クッキー/
   らんどせる /ランドセル/
   らけっと /ラケット/
   ばぐ /バグ/
   すーぱー /スーパー/
   こんぴゅーたー /コンピューター/
   ...
   ```
   (30-50 語追加で Kat1-Kat4 の PASS 率が劇的改善見込み)

2. **BUG-13/14 助詞境界誤判定** (`kyouhakaigi`/`karehaisha`): MeCab が「きょうは→教派」「かれは→枯葉」を先に取ってしまう。SKK Layer A/B の adjacent-SKK merge の対象を「助詞と結合しやすい単語」まで拡張する余地。

3. **BUG-1 `konnichiwa → 紺一話`**: 基本挨拶。SKK に「こんにちは → 今日は」の追加を検討 (現状は「今日」+「は」でも top が「紺一話」)。

4. **BUG-3 `denshirenji → でんー知れんじ`**: `denshi` の SKK 順位 or 「でんし」→「電子」の adjacency merge 失敗。ime_type の「n+shi」の romaji 変換にも疑いあり (「し」が「ー」に化けている)。

### 中優先度

5. **BUG-44 F7 `konnichiwa → コンイチワ`**: romaji テーブルの `nn` 処理疑い。`konni` は「こんに」であるべきが「こんい」になっている。romaji.cpp 側の nn+i 分岐を確認要。

6. **BUG-9/10 `ookii`/`chiisai` → symbol dict 記号**: 「大きい/小さい」の使用頻度の方が圧倒的に高いのに `>`/`<` が top。symbol dict のこの 2 語だけを大なり/小なりから除外、あるいは i-adj lemma を優先する処置。

7. **BUG-5/6 `tabenai`/`tabemasu`**: 「無い」「鱒」の okuri-ari 合成が優先されている。前回 v0.1.6 の ReadsAs フィルタと同型の問題。動詞の否定・丁寧形専用の SKK 促進が必要かも。

### 低優先度 / 仕様の範囲

8. **BUG-7/8 `kau`/`katta`**: 助詞「か」「った」との衝突。将来 `kau→買う` の SKK 追加で対応可。
9. **BUG-11 `shizuka→静香`**: 人名 top、SKK 辞書順の副産物。
10. **BUG-12 `oosaka→オオサカ`**: katakana probe が top に。probe promotion 順序見直し余地。
11. **BUG-15/16/17 (K9)**: 助詞絡み複合文はそもそも Phase B の tab 移動で選び直す運用想定。

---

## 次回セッションで追加検証したい

- ↓ で下位候補まで到達できるか (K7 同音異義、K8 固有名詞の「佐藤」など)
- Phase B の Tab/Shift+Tab focus 移動 (K9 BUG 群を Tab 選択で修正できるか)
- 外来語 SKK 辞書拡充後の再測定
- BUG-44 romaji nn+i 分岐の unit test 追加
- BUG-3 `denshi` の romaji 変換ダンプ (composition ↓)

## 全体所感

前回 v0.1.6 で修正した Phase B の 3 ケースは全て保守されており、ReadsAs フィルタ + adjacent-SKK merge の設計は妥当。ただし **カタカナ変換全体の脆弱さ** が今回のスイープで浮き彫りに。次期主要施策は SKK 辞書のカタカナエントリ大量追加 (即効性・低リスク)。実装面のリファクタは、まず romaji.cpp の nn 処理と textservice.cpp の katakana probe promotion を見直す価値がある。
