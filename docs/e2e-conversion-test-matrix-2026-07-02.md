# GenerativeIME E2E 変換テストマトリクス (2026-07-02)

WDAC クラウドサンドボックス上で、MSI v0.1.3 で注入した `GenerativeIME.Tsf.dll` に対し notepad から入力を打ち、期待変換を観測する。1 セッション内で全カテゴリを一巡する前提。

## 環境の制約 (実行時に留意)

- **Ollama 不到達**: `localhost:11434` は無い。LooksSuspect Trigger A/B/C/E ではフォールバック要求が発火して spinner が 120s タイムアウトまで回る。各ケースは「seed[0] (MeCab 結合) と katakana probe [seed[1]] を観測 → ESC」で切り上げる。実際の LLM 補正候補（例: 死んだ）は観測不能。
- **学習ストア**: `%APPDATA%\GenerativeIME\{learning,blacklist,boundary_blacklist}.txt`。セッションローカル。前セッションの学習は残らない。
- **DBWIN 出力不可**: `OutputDebugStringW` は捕捉できない。`list_process_modules` で DLL ロードだけ確認。
- **`list_tsf_profiles.isActive`** は嘘。`activate_tsf_profile` 後は `list_process_modules(pid=notepad, filter="GenerativeIME|mecab")` でロード確認する。

## Trigger ラベルの実装との対応 (混乱防止)

保存済メモリでは "Trigger D/E/F" と呼んでいた項目とコード実装 (`bunsetsu.cpp`) がズレていた。以下が正:

| 通称 | コード内ラベル | 発火条件 | 実装 |
|---|---|---|---|
| kanji-in-lemma | Trigger A | lemma に `顎為居出御様等処時故沢殆凡矢兎宛何嘗只迄謂勿論尤所如唯` 等 | ✓ |
| many-short-morphemes | Trigger B | 短形態素 5+ / 6+ 文字 | ✓ |
| 2+ 長音 ー | **Trigger C** (旧記憶は "D") | reading に `ー` が 2+ | ✓ |
| 撥音便/促音便 misanalysis (ん/っ) | Trigger E | 2-morpheme で ん/っ | ✓ |
| い音便 misanalysis | (無) | **未実装**. `kaita/naita/saita/haita/kiita/oita/oyoida` は現状 wrong 変換 | ✗ |

このセッションの Regression baseline としてい音便パターンも実測する（現状の wrong 出力を記録）。

---

## Cat A: Romaji→Kana テーブル (composition 表示)

Space を押さず、composition 表示に出ている kana だけを assertion。各ケース末尾で ESC してクリア。

### A-1: 基本五十音
| Input | 期待 composition |
|---|---|
| `konnichiwa` | こんにちは |
| `sakura` | さくら |
| `tabemasu` | たべます |
| `zyoshi` | ぞし (`zy` は無いので `zi`+`y`… 実測要) |

### A-2: 促音 (自動 sokuon)
| Input | 期待 |
|---|---|
| `motto` | もっと |
| `hakkou` | はっこう |
| `sassoku` | さっそく |
| `kittai` | きったい |
| `nppa` | んっぱ (n → ん + っぱ) |

### A-3: 促音 (明示)
| Input | 期待 |
|---|---|
| `xtsu` | っ |
| `ltsu` | っ |
| `ltuki` | っき |

### A-4: 拗音
| Input | 期待 |
|---|---|
| `kyou` | きょう |
| `syuppatsu` | しゅっぱつ |
| `chotto` | ちょっと |
| `nya` `nyu` `nyo` | にゃ にゅ にょ |

### A-5: 長音
| Input | 期待 |
|---|---|
| `me-ru` | めーる |
| `ko-hi-` | こーひー |
| `su-tsu` | すーつ |

### A-6: Small kana / 小書き
| Input | 期待 |
|---|---|
| `la` `li` `lu` `le` `lo` | ぁ ぃ ぅ ぇ ぉ |
| `xa` `xi` `xu` `xe` `xo` | ぁ ぃ ぅ ぇ ぉ |
| `lya` `lyu` `lyo` | ゃ ゅ ょ |
| `lwa` / `xwa` | ゎ ゎ |
| `lke` / `xke` | ヶ ヶ (katakana) |

### A-7: n-handling
| Input | 期待 |
|---|---|
| `nn` | ん |
| `nka` | んか (n + k → ん) |
| `nya` | にゃ (n + y なので table 経由) |
| `na` | な |
| `n'a` | ん’あ (apostrophe は punct 経由なので ’) |
| 単独 `n` を末尾に残す | 未確定 (composition では pending) |

### A-8: 外来音行
| Input | 期待 |
|---|---|
| `wi` `we` `wu` | うぃ うぇ う |
| `wyi` `wye` | ゐ ゑ |
| `fa` `fi` `fe` `fo` | ふぁ ふぃ ふぇ ふぉ |
| `fya` `fyu` `fyo` | ふゃ ふゅ ふょ |
| `va` `vi` `vu` `ve` `vo` | ヴぁ ヴぃ ヴ ヴぇ ヴぉ |
| `vya` `vyu` `vyo` | ヴゃ ヴゅ ヴょ |
| `ye` `she` `je` `che` | いぇ しぇ じぇ ちぇ |
| `tsa` `tsi` `tse` `tso` | つぁ つぃ つぇ つぉ |
| `tha` `thi` `thu` `the` `tho` | てゃ てぃ てゅ てぇ てょ |
| `dha` `dhi` `dhu` `dhe` `dho` | でゃ でぃ でゅ でぇ でょ |
| `twa` `twi` `twu` `twe` `two` | とぁ とぃ とぅ とぇ とぉ |
| `dwa` `dwi` `dwu` `dwe` `dwo` | どぁ どぃ どぅ どぇ どぉ |
| `kwa` `kwi` `kwe` `kwo` | くぁ くぃ くぇ くぉ (kwu 無し) |
| `gwa` `gwi` `gwe` `gwo` | ぐぁ ぐぃ ぐぇ ぐぉ (gwu 無し) |
| `swa` `swi` `swe` `swo` | すぁ すぃ すぇ すぉ (swu 無し) |
| `hwa` `hwi` `hwe` `hwo` | ふぁ ふぃ ふぇ ふぉ (fa 列と衝突) |

### A-9: 句読点/記号 (composition 表示のみ)
| Input | 期待 |
|---|---|
| `,` | 、 |
| `.` | 。 |
| `!` | ！ |
| `?` | ？ |
| `/` | ・ |
| `[` `]` | 「 」 |
| `(` `)` | （ ） |
| `@` | ＠ |
| `~` | 〜 |
| `\` | ￥ |

---

## Cat B: SKK 全読み直接ヒット

`space` → 候補窓 → 最上位/最下位を観測。

| Input | 期待 (SKK ヒット) |
|---|---|
| `arigatou` | 有難う / 有り難う |
| `ame` | 雨 / 飴 / 天 (順序は SKK ファイル順) |
| `mita` | 見た が MergeMecabVerbForms で先頭 |
| `okuru` | 送る / 贈る (okuri-ari 復元) |
| `atarashii` | 新しい (i-adj 合成) |

---

## Cat C: シンボル辞書 (SKK 前段)

| Input | 期待 |
|---|---|
| `yajirushi` | → ← ↑ ↓ ⇒ ⇐ ⇑ ⇓ … |
| `mirimeetoru` | ㎜ mm |
| `hoshi` | 星 と ★ が混在するか観測 |

---

## Cat D: MeCab + KanjifyByReading

| Input | 期待 |
|---|---|
| `hashiru` | 走る |
| `taberu` | 食べる |
| `noritai` | 乗りたい (uncertain — 実測) |

---

## Cat E: Phase B 文節分割 UI

複数文節。Tab, Shift+Tab, Shift+←, Shift+→, Enter を追走。

| Input | 期待 |
|---|---|
| `watashihagakusei` | 私 / は / 学生 の 3 文節 |
| `kyouhakaigi` | 今日 / は / 会議 |
| `atarashiihon` | 新しい / 本 |
| `haruhaakaruikisetsudesu` | 春/は/明るい/季節/です |

操作: Tab で focus 移動 → 候補が切替わる。Shift+→ で文節境界拡張。Enter で全文節コミット + 各文節の学習記録。

---

## Cat F: LooksSuspect Triggers (Ollama 依存)

Space 直後、seed[0]/seed[1] を観測 → ESC。Ollama 補正候補は捕捉できないため absence を確認するだけ。

### F-1: Trigger A (kanji-in-lemma)
| Input | seed 予想 |
|---|---|
| `seide` | 「所為で」 (Ollama 補正無しで seed のまま) |
| `ojisama` | 「御じ様」相当 (実測) |

### F-2: Trigger B (many short morphemes)
| Input | seed 予想 |
|---|---|
| `chuugakusei` | seed = MeCab 断片結合 |
| `chuugakuseigagakkouni` | seed = 中/学/生/が/学/校/に 相当 |

### F-3: Trigger C (2+ 長音 ー)
| Input | seed 予想 |
|---|---|
| `ko-hi-` | seed[0] = mecab結合, seed[1] = コーヒー (katakana probe) |
| `su-pa-ma-ketto` | seed[1] = スーパーマーケット 予想 |
| `ekusukuramesyonmaaku` | seed[1] = エクスクラメーションマーク 予想 |
| `konpyu-ta-` | seed[1] = コンピューター |

### F-4: Trigger E (2-morpheme 撥音便/促音便)
| Input | seed 予想 (Ollama 無しで sandbox) | 本来欲しい (参考) |
|---|---|---|
| `shinda` | シンだ / 芯だ | 死んだ |
| `funda` | それだ相当 | 踏んだ |
| `kanda` | 彼だ | 噛んだ |
| `monda` | 物だ | 揉んだ |
| `anda` | 餡だ | 編んだ |
| `unda` | うんだ | 産んだ |
| `shinde` | 芯で | 死んで |
| **Negative controls** | | |
| `nonda` | 飲んだ (MeCab 正常) — suspect にならない | 飲んだ |
| `yonda` | 呼んだ | 呼んだ |
| `panda` | パンダ | パンダ |

---

## Cat G: い音便 (実装ギャップ確認 = regression baseline)

現状 wrong を記録。次実装時の比較用。

| Input | 現状予想 (wrong) | 本来欲しい |
|---|---|---|
| `kaita` | 貝田 / カイタ / 貝多 | 書いた |
| `naita` | 内田 相当 | 泣いた |
| `saita` | 斎田 | 咲いた |
| `haita` | 拝田 | 履いた / 掃いた |
| `kiita` | 木田 | 聞いた |
| `oita` | 老田 / 大分 | 置いた |
| `oyoida` | 御依田 | 泳いだ |

---

## Cat H: Filler-guard (`isFillerKana`)

単独 kana + Space。lemma が 長音記号 `ー` で伸ばされている場合 (`ん→んー` 等) を suppress していることを確認。

| Input | 期待 |
|---|---|
| `nn` | ん (単独, "んー" が候補に無い) |
| `u` | う (拡張 "うう" が候補に無い) |
| `e` | え |
| `a` | あ |
| **Negative** `ame` | lemma "雨" が promote されて先頭 (filler じゃない) |

---

## Cat I: Katakana probe (Trigger C 内で発火)

Cat F-3 と重複するので省略。probe が seed[1] に付いていることを get_ui_tree で確認。

---

## Cat J: 句読点 (単独 Space → PunctPairs)

Space すると単独文字候補が 2 件 (全角/半角) 出る。

| Input | Space 後の候補 |
|---|---|
| `,` | 、 / , |
| `.` | 。 / . |
| `!` | ！ / ! |
| `?` | ？ / ? |
| `[` | 「 / " |
| `(` | （ / ( |
| `@` | ＠ / @ |
| **Negative (pair 表に無い)** | |
| `/` | ・のまま or 別ルート — 実測 |
| `~` | 〜のまま — 実測 |

---

## Cat K: 学習

各サブケースは実行順に依存する（学習状態を積んでいく）。実行順ロック:

1. **K-1 fav 記録**: `ame` Space → 候補窓 → ↓ で「飴」を選ぶ → Enter コミット。→ 学習に (あめ,飴) 記録。
2. **K-2 fav fast path**: 再度 `ame` Space → 候補窓は「飴」単独 (fav fast path)。
3. **K-3 blacklist**: 直接 `ame` を Space → 候補窓が飴単独になっている場合は Shift+Delete で飴 blacklist → composition キャンセル。想定: 再度 `ame` → 候補窓に飴が含まれない (雨/天/...)。
4. **K-4 boundary blacklist (Phase B)**: `watashihagakusei` Space → Phase B → 文節焦点で Shift+Delete → 境界形状 blacklist → キャンセル。再度 `watashihagakusei` Space → 同じ境界が使われず suspect path 強制 (Ollama fallback spinner).
5. **K-5 F-key learning**: `arigatou` Space **せずに** F7 → アリガトウ 直接コミット準備 → Enter。→ (ありがとう,アリガトウ) 学習。再度 `arigatou` Space → fav fast path で アリガトウ。

---

## Cat L: 変換/無変換 キー

### L-1: 変換 (VK_CONVERT)
- composition 中: Space と同じ。
- 選択ありで composition 無し: TryReconvertFromSelection 発動。手順: notepad に "見た" を type_text で直接書く → その "見た" を選択 → send_key(VK_CONVERT) → composition が "みた" になり Space 挙動と同じ。

### L-2: 無変換 (VK_NONCONVERT)
- 候補窓中: fav 化キャンセル ("あめ" 状態に戻して learn マーク)
- composition のみ (candidate 無し): CycleNonconvertForm — 4状態を順に:
  0: `abc` → あbc? (要実測 — 未確定 romaji は落ちる)
  1: 全角カナ
  2: 半角カナ
  3: ローマ字

---

## Cat M: モードトグルキー

| キー | 期待 |
|---|---|
| VK_KANJI (半角/全角) | IME on/off トグル |
| VK_DBE_HIRAGANA (0xF2) | Hiragana モードで on |
| Ctrl + VK_DBE_HIRAGANA | IME off |
| VK_DBE_KATAKANA (0xF1) | FullKatakana モードで on |
| ESC | 現 composition キャンセル |

---

## Cat N: F6-F10 変換

`arigatou` を打った状態 (composition = ありがとう) で:

| キー | 期待 |
|---|---|
| F6 | ありがとう (ひらがな, 変わらず) |
| F7 | アリガトウ (全角カタカナ) |
| F8 | ｱﾘｶﾞﾄｳ (半角カタカナ) |
| F9 | ａｒｉｇａｔｏｕ (全角英数) |
| F10 | arigatou (半角英数) |

各後に Enter でコミット + `(hira, converted)` 学習。

---

## Cat O: 候補窓ナビゲーション

`ame` Space で候補窓を出した後:
- ↓ / ↑ で選択移動
- Tab で次候補 (単一文節時)
- PgDn / PgUp でページ切替 (候補が >9 の場合のみ)
- 1-9 で直接ページ内相対位置選択 + コミット
- ESC で候補窓と composition 両方閉じる

---

## 実行計画（順序）

- **Phase 1 (0-30 min)**: セッション起動 → v0.1.3 install_ime → activate_tsf_profile → list_process_modules で DLL ロード確認。
- **Phase 2 (30-60 min)**: Cat A (composition 表示 sanity, ESC で完結) を全部一気に。40 ケース超。
- **Phase 3 (60-90 min)**: Cat B/C/D (SKK/シンボル/MeCab) 15 ケース程度。
- **Phase 4 (90-120 min)**: Cat E (Phase B 文節分割) 4 ケース。Tab/Shift+←→ 込み。
- **Phase 5 (120-150 min)**: Cat F/G (LooksSuspect + い音便 regression) 20 ケース。suspect 系は spinner 待たずに ESC 切り。
- **Phase 6 (150-180 min)**: Cat H/J (filler-guard + 句読点) 15 ケース。
- **Phase 7 (180-210 min)**: Cat K (学習) 5 サブケース。順序ロック。
- **Phase 8 (210-240 min)**: Cat L/M/N/O (キー動作 + F-key + navigation)。
- **Phase 9**: terminate_session + `docs/e2e-conversion-test-report-2026-07-02.md` に fail/pass を集約。

各 phase 間で必ず notepad の内容を Ctrl+A + Delete でクリアし、composition 状態も ESC で解除。

## 判定基準

- **PASS**: 期待値 (composition 表示 or 候補窓先頭 or コミット後 valueCurrent) と実測が完全一致。
- **KNOWN_FAIL**: い音便 (Cat G) 全項目、Ollama 依存の LooksSuspect 補正部分。
- **BUG**: それ以外の期待外れ。詳細を再現手順と共に記録。
