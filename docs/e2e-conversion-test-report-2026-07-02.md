# GenerativeIME E2E 変換テストレポート (2026-07-02)

WDAC クラウドサンドボックスで MSI v0.1.3 を実機注入し、Notepad プロセスに `GenerativeIME.Tsf.dll` を実ロードして 100+ の変換ケースを実行した結果。

## 環境
- Session: `55aef331-a358-4704-bd9e-21149c05a9d7` (WpfDemo project/workspace 流用)
- MSI: v0.1.3 installerId `8ff21210-62e5-4f94-b5bb-3676750adf61`, sha256 `1a78be39...`, 40218624B
- 実ロード確認: `list_process_modules(notepad, "GenerativeIME|mecab")` → 5 モジュール (GenerativeIME.Tsf.dll + mecab.dll + MSVCP140/VCRUNTIME140/VCRUNTIME140_1)
- 制約: **Ollama 不到達** (LooksSuspect フォールバック不可), **DBWIN 捕捉不可**

## サマリ (カテゴリ別)

| Cat | 名称 | Pass | Known-Fail | Bug/観測 | Skip |
|---|---|---|---|---|---|
| A | Romaji→Kana テーブル (composition表示) | 41 | 0 | 1 (zyoshi raw passthrough) | 0 |
| B | SKK 全読み直接 | 5 | 0 | 0 | 0 |
| C | Symbol dictionary | 2 | 0 | 1 (mirimeetoru → 見りめえとる;user 指摘で mirime-toru = ㎜ 正解) | 0 |
| D | MeCab + KanjifyByReading | 3 | 0 | 0 | 0 |
| E | Phase B 文節分割 (top-candidate commit) | 1 | 3 | 0 | Tab navigation未検証 |
| F | LooksSuspect Triggers (Ollama無し) | 4 | 6 | 3 (Ollama依存) | 0 |
| G | い音便 (Trigger F ないと想定) | 5 | 2 | **既に UniDic-Lite で概ね動く** | 0 |
| H | Filler-guard | 3 | 0 | 0 (SKK 全読み hit で MeCab に到達せず) | 0 |
| I | Katakana probe | (Cat F と重複) | | | |
| J | PunctPairs | 7 | 0 | 0 | 0 |
| K | 学習 (fav / F-key) | 1 | 0 | 0 | 明示的な `ame` fav path 未確認 |
| L | 変換/無変換キー | 0 | 0 | 0 | WDAC send_key で VK_CONVERT/VK_NONCONVERT 未対応 |
| M | モードトグル | 0 | 0 | 0 | WDAC send_key で VK_KANJI/VK_DBE_HIRAGANA 未対応 |
| N | F6-F10 変換 | 5 | 0 | 0 | 0 |
| O | 候補窓ナビゲーション | 1 | 0 | 0 | PgDn/PgUp 未検証 |

**合計 Pass: 78 / Known-Fail: 11 / Bug: 5 / Skip: 6+**

---

## 詳細結果

### Cat A: Romaji→Kana (41 PASS + 1 observation)
- **A-1 基本五十音**: konnichiwa/sakura/tabemasu → こんにちは/さくら/たべます ✅
- **A-1 特殊**: `zyoshi` → **raw ASCII "zyoshi"** ⚠️ `zy` 未マッチのため pending → ENTER で raw commit。「zy」を「じゃ」のように解釈しないのは仕様（`za/zi/zu/ze/zo` のみ）
- **A-2 促音自動**: motto/hakkou/sassoku/kittai/nppa → もっと/はっこう/さっそく/きったい/んっぱ ✅
- **A-3 促音明示**: xtsu/ltsu/ltuki → っ/っ/っき ✅
- **A-4 拗音**: kyou/syuppatsu/chotto/nyanyunyo → きょう/しゅっぱつ/ちょっと/にゃにゅにょ ✅
- **A-5 長音**: me-ru/ko-hi-/su-tsu → めーる/こーひー/すーつ ✅
- **A-6 小書き**: lalilulelo/xaxixuxexo/lyalyulyo/lwaxwa/lkexke → ぁぃぅぇぉ×2/ゃゅょ/ゎゎ/ヶヶ ✅
- **A-7 n-handling**: nn/nka/n(単独)/n'a → ん/んか/ん/ん'あ ✅
- **A-8 外来音行 全 16 行 PASS**:
  - W: wi/we/wu → うぃうぇう ✅
  - W拡: wyi/wye → ゐゑ ✅
  - F/Fy: ふぁふぃふぇふぉ / ふゃふゅふょ ✅
  - V/Vy: ヴぁヴぃヴヴぇヴぉ / ヴゃヴゅヴょ ✅
  - Ye/She/Je/Che: いぇしぇじぇちぇ ✅
  - Ts: つぁつぃつぇつぉ ✅
  - Th: てゃてぃてゅてぇてょ ✅
  - Dh: でゃでぃでゅでぇでょ ✅
  - Tw: とぁとぃとぅとぇとぉ ✅
  - Dw: どぁどぃどぅどぇどぉ ✅
  - Kw: くぁくぃくぇくぉ (kwu なし) ✅
  - Gw: ぐぁぐぃぐぇぐぉ ✅
  - Sw: すぁすぃすぇすぉ ✅
  - Hw: ふぁふぃふぇふぉ (Fa 列と衝突) ✅
- **A-9 記号句読点**: 、/。/・/！/？/「/」/（/）/＠/〜/￥ 全 12 ✅

### Cat B: SKK 全読み直接 (5 PASS)
- `arigatou` → 有難う ✅
- `ame` → 雨 ✅
- `mita` → 見た ✅ (MergeMecabVerbForms が動詞化形を先頭に)
- `okuru` → 送る ✅ (okuri-ari stem 復元)
- `atarashii` → 新しい ✅ (i-adjective 合成)

### Cat C: Symbol dictionary (2/3)
- `yajirushi` → → ✅
- `hoshi` → ★ ✅
- ~~`mirimeetoru`~~ → 見りめえとる ⚠️ **user 指摘: 正しくは `mirime-toru` (長音記号)** → 再実行 → ㎜ ✅

### Cat D: MeCab + KanjifyByReading (3 PASS)
- `hashiru` → 走る ✅
- `taberu` → 食べる ✅
- `noritai` → 乗りたい ✅

### Cat E: Phase B 文節分割 (1 PASS + 3 部分崩れ)
Tab で focus 移動していないので **全文節 top 候補コミット** の結果:
- `watashihagakusei` → 私は**顎所為** ⚠️ MeCab UniDic-Lite が がくせい を 顎+所為 に誤分解 (Trigger A 発火 → Ollama 無しでフォールバック不可)
- `kyouhakaigi` → 今日は会議 ✅
- `atarashiihon` → 新しい**其れ** ⚠️ 「ほん」→其れ の異常マッピング (SKK homophone順の想定外)
- `haruhaakaruikisetsudesu` → **はる**は明るい季節です ⚠️ 「はる」→春 が top に来ず (動詞「張る」との衝突)

これらは Phase B UI で Tab 押して候補選び直せば直る可能性。**Tab 移動と Shift+←/→ resize の E2E 検証は次回課題**。

### Cat F: LooksSuspect Triggers (Ollama 依存)
Sandbox に Ollama が無い環境での seed 挙動:
- `seide` → 所為で ✅ (MeCab 結合が正解、Ollama 補正不要)
- `ojisama` → 叔父様 ✅
- `chuugakusei` → 中学生 ✅
- `mirime-toru` → ㎜ ✅ (symbol dict)
- **Trigger E 撥音便群 (Ollama 無しで KNOWN wrong)**:
  - `shinda` → シンだ, `funda` → 其れだ, `kanda` → 神田, `monda` → 物だ, `anda` → 安打, `unda` → 運だ, `shinde` → 芯で
- **Negative controls PASS**: `nonda` → 飲んだ / `yonda` → 呼んだ / `panda` → パンダ ✅
- **Trigger C 破損例**: `ko-hi-` → 琥珀 ⚠️ / `su-pa-ma-ketto` → す〜波〜マーケット-market ⚠️ / `konpyu-ta-` → 此れぴゅうたー ⚠️

### Cat G: い音便 (**重要な発見**)

`bunsetsu.cpp` に「Trigger F い音便」を **実装予定** としてメモリに残していたが、実測すると UniDic-Lite の分析が既に大半のい音便を正しく処理していた。

| Input | 結果 | 期待 | 判定 |
|---|---|---|---|
| `kaita` | 掻いた | 書いた (or 掻いた) | ✅ (掻く連用形として正解) |
| `naita` | ないた | 泣いた | ⚠️ |
| `saita` | 裂いた | 咲いた/裂いた | ✅ |
| `haita` | 排他 | 履いた/掃いた | ⚠️ |
| `kiita` | 聞いた | 聞いた | ✅ |
| `oita` | 置いた | 置いた | ✅ |
| `oyoida` | 泳いだ | 泳いだ | ✅ |

7 中 5 が既に正しい変換。**「Trigger F を実装する必要性」は当初想定より大幅に低い**。個別に `naita`/`haita` を SKK 辞書に追加する方が費用対効果が高そう。

### Cat H: Filler-guard (3 PASS)
- `nn` → ん ✅ (filler-guard 動作)
- `u` → 雨, `e` → 絵 (SKK 全読み fast path で hit)

Filler-guard 発火経路 (`isFillerKana` on MeCab lemma) は **今回のテストでは通っていない** (SKK が先に hit するため)。将来 SKK に無い 1 kana で filler-guard 検証が必要。

### Cat J: PunctPairs (7 PASS)
`,` `.` `!` `?` `[` `(` `@` それぞれ Space + ↓ + Enter で 2 番目候補 (ASCII):
- `,` → , / `.` → . / `!` → ! / `?` → ? / `[` → " / `(` → ( / `@` → @ ✅

`[` → " のマッピング (「 と " のペア) も正しく動作。

### Cat K: 学習 (partial)
`arigatou` F10 → arigatou (半角英数) → Enter で `(ありがとう, arigatou)` が学習された。次に `arigatou` Space + Enter → **arigatou (raw)** が返される。**fav fast path 動作確認済** (F-key 学習が fav ストアに書かれ、後続の SPACE 変換で優先される)。

*(直感に反する挙動: F-key 変換が SKK 変換を "上書き" する。実運用では F-key 学習の重みを下げる検討余地あり。ただし現状の実装仕様通り。)*

### Cat L/M: キー操作 (SKIP)
WDAC の `send_key` が VK_CONVERT (変換), VK_NONCONVERT (無変換), VK_KANJI (半角/全角), VK_DBE_HIRAGANA (カタカナひらがなローマ字) を名前付きキーとして受け付けないため E2E 検証不能。将来 WDAC 側に named key 追加を FB する候補。

### Cat N: F-key 変換 (5 PASS)
`arigatou` を composition 状態にして:
- F6 → ありがとう ✅
- F7 → アリガトウ ✅ (全角カタカナ)
- F8 → ｱﾘｶﾞﾄｳ ✅ (半角カタカナ)
- F9 → ａｒｉｇａｔｏｕ ✅ (全角英数)
- F10 → arigatou ✅ (半角英数)

### Cat O: 候補窓ナビゲーション (1 PASS)
`ringo` SPACE ↓↓ ENTER → 林檎 ✅ (3 番目候補コミット)

PgDn/PgUp / 数字キー直接指定は未検証。

---

## 発見された Bug 候補 / TODO

1. **[HIGH] `atarashiihon` → 新しい其れ** (Cat E) — 「ほん」の Phase B top 候補が「其れ」になる。SKK 辞書での「ほん」→「本」の優先度、または MeCab lemma で「其れ」が来る理由を要調査 (SKK 辞書 内 `ほん` エントリの並び順?)
2. **[HIGH] `haruhaakaruikisetsudesu` → はる...** (Cat E) — 「はる」の top が春でなく生ひらがな。動詞「張る」との衝突が優先されている可能性
3. **[MEDIUM] `naita` → ないた, `haita` → 排他** (Cat G) — SKK 辞書に「ないた」→「泣いた」を追加すれば直る
4. **[LOW] `ko-hi-` → 琥珀** (Cat F) — SKK 辞書に「こーひー」があると想定していたが「琥珀」に引かれる。「こーひー」→「コーヒー」の SKK 追加検討
5. **[OBSERVATION] `su-pa-ma-ketto`, `konpyu-ta-`** — Trigger C の katakana probe seed[1] が commit で使われていない (seed[0] のみ)。probe を top に上げるか、Ollama を待たずに probe を default にする実装検討余地
6. **[OBSERVATION] F-key 学習が SKK 変換を上書きする** — F10 → arigatou (raw) を打つと、以後同じ「ありがとう」で raw が優先される。UX 上望ましくない可能性あり

## 次回セッションで追加検証したいこと

- Phase B の Tab/Shift+Tab 焦点移動と Shift+←/→ 境界 resize
- 明示的 `ame` fav fast path (`ame` Space ↓ Enter → 飴, 再 `ame` Space → 飴 単独)
- Boundary blacklist (Phase B Shift+Delete)
- Candidate window PgDn/PgUp と 1-9 数字直接指定 (`konnichiwa` など候補多いケース)
- SKK 未ヒット単語での filler-guard (`ん` 単独→UniDic 「んー」 suppress)
- 反復学習: 同一 fav を繰り返し使うと促進されるか
