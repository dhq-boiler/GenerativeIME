---
project_path: C:\Git\GenerativeIME
saved_at: 2026-06-29
title: GenerativeIME — Phase B / Ollama fallback / 外来語対応 / 学習 opt-out / unit テスト harness
---

## Summary
TSF ベースの C++ IME (Win11)。ひらがな / 全角カナ / 半角カナ / 全角英数 / 半角英数 の 5 モード + モードピル。SKK-JISYO.L + MeCab (vcpkg) + UniDic-Lite で形態素解析 → 文節分割、`gemma4:12b` (Ollama) を非同期 fallback として使い、外来語など UniDic-Lite では正しく切れない読みを LLM に補わせる。Phase B (文節別候補選択 UI)、reconvert、変換/無変換キー、カタカナひらがなローマ字キー、Shift+Delete による学習 opt-out (Blacklist / BlacklistBoundary) まで実装済み。`tests/` 配下に 29 件の core-logic unit test harness を持ち、TSF 統合を経ずに romaji / bunsetsu / LearningStore / MeCab+bunsetsu の挙動を回せる。

直近の流れ:
- **2e76b83** ローマ字テーブル拡張 (外来音) + filler lemma promote 抑止 + LooksSuspect Trigger D (2+ ー) + LearningStore.Blacklist/BlacklistBoundary/GetFav + Shift+Delete opt-out + 候補ウィンドウ Ollama スピナー
- **2dac750** unit test harness (`tests/test_runner.cpp` + `tests/build_tests.ps1`, 当初 20 件)
- **15d231a** filler-lemma ガードに 長音記号 `ー` を allowlist (UniDic 「ん→んー」 promote を塞ぐ)
- **e754114** unit test を 9 件追加 (MergeMecabVerbForms / MakeBunsetsuFromReading / SplitMecab+KanjifyByReading)。計 29 件 all green。

## Tasks

### Completed (このスナップショット時点)
- [x] **#1** モード動作確認 (5 モード切替、ノートパッドで疎通)
- [x] **#3** 再変換 (`2701c39` reconvert + 変換 / 無変換 キー)
- [x] **#5** SKK 辞書統合 (SKK-JISYO.L)
- [x] **#9** モード切替バグ修正 (Global compartment が Deactivate で消える問題)
- [x] **#10** MeCab 統合 (vcpkg + mecab + UniDic-Lite)
- [x] **#11** 動詞・形容詞は surface を 1 位候補に
- [x] **#12** KanjifyByReading (連用形対応)
- [x] **#13** Ollama fallback (`bunsetsu::LooksSuspect` + `StartMecabSupplementAsync`)
- [x] **Phase B (#2)** 文節別候補選択 UI (`4af6ed1` + フォローアップ多数)
- [x] **Ollama warmup** Activate 時 fire-and-forget で常駐
- [x] **外来音ローマ字テーブル** wi/we/wu/wyi/wye, ye, she/je/che, tsa-tso, tha-tho, dha-dho, twa-two, dwa-dwo, kwa-kwo, gwa-gwo, swa-swo, hwa-hwo, fya/fyu/fyo, vya/vyu/vyo, lya-lyo, xya-xyo, lwa/xwa (ゎ), lke/xke (ヶ), ltsu/xtsu。`kMaxKey = 4`
- [x] **学習 opt-out** Shift+Delete で Phase B は `BlacklistBoundary`、single モードは `Blacklist + BlacklistBoundary`。永続化先は `%APPDATA%\GenerativeIME\blacklist.txt` / `boundary_blacklist.txt`
- [x] **whole-reading fav 高速パス** GetFav ヒット → 単一トップ候補にして SKK/MeCab/Ollama を全スキップ
- [x] **katakana プローブ** suspect 経路で `ToKatakanaPublic + MeCab.Analyze` を回し 1-3 形態素なら katakana 文字列を候補追加 (`えくすくらめーしょんまーく → エクスクラメーションマーク`)
- [x] **Ollama スピナー** 候補ウィンドウ下端に Braille 1 行 「⠋ Ollama 思考中…」 を WM_TIMER で描画
- [x] **filler lemma promote 抑止** `bunsetsu.cpp` の guard に 長音記号 `ー` を allowlist
- [x] **unit test harness** 29 件 (romaji / symbols / bunsetsu / LearningStore / MeCab 統合)

### Pending
- [ ] **WDAC-MCP E2E harness** 試作 — OAuth 済み、`mcp__wdac-mcp-staging__*` 即利用可
- [ ] **Phase B + Ollama 統合** 現状は `HandleOllamaFallbackDone` が `InBunsetsuMode` で early-bail。着目文節の候補リストに LLM 解を prepend するモードを検討
- [ ] **unit test さらに拡張** ResizeFocusedBunsetsu ラウンドトリップ、SplitMecab の noun ブランチ全体、Ollama JSON パース部
- [ ] **#4** 候補ウィンドウ ページング表示 / ヒントテキスト
- [ ] **#6** x86 ビルド
- [ ] **#7** HKLM\CTF\Assemblies\0x00000411 登録
- [ ] **#8** 署名・インストーラ (Smart App Control とは別問題として残る)
- [ ] kSuspect リストの観察ベース拡張
- [ ] 促音便 (`itta`, `sukutta`, `owatta`) の KanjifyByReading 対応

## Files & Code Locations

### コア (テスト harness が網羅している領域)
- `src/GenerativeIME.Tsf/romajitokana.cpp:26-50` — 外来音テーブル、`kMaxKey = 4` (line ~75)
- `src/GenerativeIME.Tsf/symboldictionary.h/.cpp` — `symbols::PunctPairs`
- `src/GenerativeIME.Tsf/bunsetsu.h` — `ReadsAs` / `LooksSuspect` / `MergeMecabVerbForms` / `MakeBunsetsuFromReading` / `ToKatakanaPublic`
- `src/GenerativeIME.Tsf/bunsetsu.cpp`:
  - `KanjifyByReading` (anonymous namespace) — lemma-stem alignment による連用形漢字化
  - `SplitMecab` — verb / noun / particle ブランチ。noun ブランチに `isFillerKana` guard (ひらがな + `ー` allowlist)
  - `LooksSuspect` Trigger A (稀字 lemma) / B (5+ 形態素 AND reading 6+ 字) / D (2+ ー)。Trigger C はユーザ判断で revert 済み
- `src/GenerativeIME.Tsf/learningstore.h/.cpp`:
  - `Record / Reorder / GetFav` (in-memory + `learning.txt`)
  - `Blacklist / IsBlacklisted` → `blacklist.txt`
  - `BlacklistBoundary / IsBoundaryBlacklisted` → `boundary_blacklist.txt`
  - Reorder は blacklist filter → fav promote の順。filter 結果が空なら原リストを通す fallback

### TSF 統合 (テスト harness 対象外)
- `src/GenerativeIME.Tsf/textservice.cpp` `TryOllamaConvertAsync`:
  - PunctPairs fast path → whole-reading fav fast path → suspect/boundary 判定 → katakana プローブ → MeCab → Ollama supplement の順
  - boundary blacklist hit で `suspect = true` を強制 → Ollama 強制発火
- `src/GenerativeIME.Tsf/textservice.cpp` `StartMecabSupplementAsync` — Ollama プロンプトに外来音ルール (rules 5, 6) を追加
- `src/GenerativeIME.Tsf/textservice.cpp` `OnKeyDown`:
  - `VK_DELETE + Shift` → Phase B は `BlacklistBoundary + Cancel`、single モードは `Blacklist + BlacklistBoundary`
  - `VK_KANA / VK_DBE_HIRAGANA / VK_DBE_KATAKANA` → Ctrl-passthrough より前で処理
  - `OnPreservedKey` `c_guidKeyImeOn / c_guidKeyImeOff` をモード set に転用
- `src/GenerativeIME.Tsf/candidatewindow.h/.cpp`:
  - `SetOllamaPending(bool)`, `m_ollamaPending`, `m_spinnerFrame`
  - WM_TIMER で Braille スピナー描画 (~110ms tick, `kFrames`)

### テスト
- `tests/test_runner.cpp` — TEST(name) マクロ、AllTests() レジストリ、`EXPECT_EQ_W` は FAIL 時 U+XXXX hex dump (CMD codepage 安全)。LearningStore テストは `_test` 接尾辞付き読みで AppData 汚染を最小化
- `tests/build_tests.ps1` — `build/x64/Debug/test_runner.exe` に出力 (g_hInst-resolved dict パスが効くため)
- `.gitignore` — `tests/test_runner.exe` 除外

### スクラッチ
- `scratchpad/test_mecab.cpp` — 任意 reading に対する UniDic 出力プローブ
- `scratchpad/keylog.cs` — WH_KEYBOARD_LL で物理キーの VK コード特定。`csc /target:winexe /reference:System.Windows.Forms.dll keylog.cs`

## Errors / Unresolved
- **い音便動詞 3 ケースが UniDic-Lite で誤認識** (test_runner プローブ観察):
  - 「かいた → 掻く」(本命は「書く」)
  - 「ないた → 助動詞ない+た」(本命は「泣く」)
  - 「あるいた → 連体詞或る + 動詞居る + た」(本命は「歩く」)

  Trigger E (2 形態素 + 末尾 aux + non-verb + ん/っ含む) では拾えない。Trigger F を強引に追加すると「掻く」「或る居る」を正規に書きたいケースまで巻き込むため誤検出リスク大。長期解は whole-reading fav fast path にユーザに学習させる方針。次の本格改善時の Trigger F 候補として残す。
- **未解決バグなし**。`えくすくらめーしょんまーく` は Trigger D で Ollama 経路に正しく回る — LLM が筋の良い答えを返すかは別問題。長期解は「ユーザが Phase B + Enter で 1 度学習 → 次回以降 whole-reading fav fast path が即答」。
- 開発機の **Smart App Control をオフにした** (このセッション中に user 判断で実施)。ビルドした未署名 EXE が SAC にブロックされて test_runner.exe が走らなかったため。SAC のオフは一方向 (Windows reset まで戻せない) ので注意。Defender 本体は有効のまま。
- `learning.txt` には過去のテストで誤学習 (`みた → 三田` 等) が溜まりやすい。`%APPDATA%\GenerativeIME\learning.txt(.bak.*)` を確認 → 必要なら手動で剪定

## Next Steps
1. **SESSION-STATE.md commit** (このファイル)
2. **WDAC-MCP IME E2E** 試作 — `create_session` → IME DLL 登録 → `launch_app notepad` → `type_text` → `screenshot` / `get_ui_tree`。「ログアウト→再ログイン→DebugView」ループを置き換える
3. **Phase B + Ollama 統合** — 上記 Pending の通り。fallback プロンプトを「焦点文節だけ書き換える指示」に変える設計検討
4. **unit test 追加** — ResizeFocusedBunsetsu / SplitMecab noun ブランチ / Ollama JSON parser

## Notes

### 開発フロー
- **VS 2026 Community v18** (`C:\Program Files\Microsoft Visual Studio\18\Community\`)。PlatformToolset は **v145** (cl 14.50.x) 固定。v143 / v180 では通らない
- **vcpkg** `C:\vcpkg`、`mecab:x64-windows` 必須。Debug ビルドは `debug\bin\mecab.dll` + `debug\lib\mecab.lib` を使う (Release と混ぜると STL allocator が静かに壊れる)
- **unidic-lite** `third_party/mecab/unidic-lite/` (~248MB)。Python 不在マシンでは PyPI sdist 直接展開 (`docs/SETUP-NEW-MACHINE.md` § 4)
- **Ollama** `%LOCALAPPDATA%\Programs\Ollama\`、`gemma4:12b` (~7GB)。cold ~90s / warm ~1s。Activate で warmup fire-and-forget
- **DLL ロック対策** ホストプロセスが TSF DLL を握ったままだとリンクできない。`build_tests.ps1`-style の Move-Item rename トリック (`<dll>.locked.<rand>`) を使うか、ホスト終了 or 再ログイン
- **pure-logic 修正** は `tests/build_tests.ps1` で完結。再ログインが必要なのは TSF 統合 (キー / 候補ウィンドウ / composition) を触ったときだけ

### テスト harness の注意点
- `test_runner.exe` は staged SKK / UniDic-Lite の隣でないと `GetGlobal()` が dict 読みで永久ブロック。よって `build/x64/Debug` に出力する
- `g_hInst = nullptr` のまま。`GetModuleFileNameW(NULL, ...)` が test EXE 自体のパスを返すのを利用
- LearningStore テストは `_test` 接尾辞付き reading を使い実 learning.txt を汚さない (完全分離ではないので、ユニーク化は緩い保証)
- UTF-8 stdout 必須 (`SetConsoleOutputCP(CP_UTF8)`)。それでも CMD は `%ls` 出力で日本語が改行を呑むケースがあるため、`EXPECT_EQ_W` は FAIL 時 U+XXXX dump で表示

### 設計判断メモ
- **LooksSuspect Trigger C (外来音マーカー) は撤去済み**。ユーザ方針は「Phase B + Enter で 1 度学習 → 以後 fast path」。Trigger D (2+ ー) で長 katakana 連結だけ救う
- **fav が候補に無いケースで強制 prepend するロジックは削除済み** (`fbcddb4`)。新候補を抑制するバグだった
- **Smart App Control 対策** 開発機では基本的に SAC をオフ。署名済みインストーラ (#8) を整備したら一般ユーザは SAC 下でもインストールできる想定

### UniDic feature CSV (unidic-lite 17 field)
[0] 品詞 / [4] 活用型 / [5] 活用形 / [6] 語彙素読み (= `lemmaReading`) / [7] 語彙素 lemma / [8] 書字形

### 直近のコミット (新しい順)
- `e754114` Cover MergeMecabVerbForms, MakeBunsetsuFromReading, KanjifyByReading
- `15d231a` Extend filler-lemma guard to allow choonpu
- `2dac750` Add core-logic unit test harness
- `2e76b83` Improve conversion robustness for foreign words, opt-out, and feedback
- `de49ad9` Open full/half-width punctuation candidate window on Space
- `1506684` Hook カタカナひらがなローマ字 key (VK_DBE_HIRAGANA / VK_DBE_KATAKANA)
- `2701c39` Add reconvert and 変換 / 無変換 key handling
- `08290c8` Pass Ctrl-modified keys through to the host
- `4bd7ddf` Learn F-key picks and warm dictionaries off the activation path
- `b76bc07` Arrow navigation, Shift+arrow resize, and richer particle candidates
- `91e5438` Highlight focused bunsetsu and anchor candidate window to it
- `16d085c` Expand verb / pronunciation coverage for Phase B candidates
- `4af6ed1` Add Phase B per-bunsetsu candidate selection
- `e76bd05` Drop Ollama suggestions whose reading drifted from the input
- `4202c1b` Add Ollama fallback for suspect MeCab splits
- `fbcddb4` Improve conversion accuracy and VS 2026 support
- `097c6cc` Add MeCab+UniDic-Lite morphological analysis + setup docs
- `4422b10` Add SKK dictionary + context-aware Ollama reorder
- `fc0fc5e` Initial import of GenerativeIME
