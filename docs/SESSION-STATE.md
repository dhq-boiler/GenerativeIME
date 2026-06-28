---
project_path: C:\Git\GenerativeIME
saved_at: 2026-06-29
title: GenerativeIME — SKK+MeCab merge / KanjifyByReading / #13 Ollama fallback + warmup 動作確認済
---

## Summary
TSF ベースの C++ IME。Win11 のモードピル + 5 モード（ひらがな / 全角カナ / 半角カナ / 全角英数 / 半角英数）を備え、Ollama (`gemma4:12b`) を非同期 backend に持ち、文脈考慮の reorder も実装済。SKK-JISYO.L (4.5MB) と MeCab + UniDic-Lite (vcpkg) で形態素解析 → 文節分割を行うハイブリッド構成。

直近の作業（コミット `fbcddb4` + 次コミット）:
- **fbcddb4** (committed): 変換精度改善
  - SKK 完全マッチに MeCab 動詞活用形を merge (`bunsetsu::MergeMecabVerbForms`)
  - KanjifyByReading: UniDic field 6 (語彙素読み) を使った連用形対応 → `みた→見た` / `もえた→燃えた` / `おりた→下りた` を解決
  - LearningStore::Reorder の `else` ブランチ削除（fav が候補に含まれない場合の強制 prepend が新候補を抑制するバグ）
  - VS 2026 Community v18 用に PlatformToolset を v145 に変更
  - SETUP-NEW-MACHINE.md に Python 不在マシン向けの unidic-lite 取得手順（PyPI sdist + tar）+ v145 注記
- **次コミット**: #13 Ollama fallback + Activate warmup
  - `bunsetsu::LooksSuspect` (Trigger A: 稀字 lemma / Trigger B: 5+ 形態素 AND reading 6+ 文字)
  - `CTextService::StartMecabSupplementAsync` + `HandleOllamaFallbackDone` + `WM_OLLAMA_FALLBACK_DONE`
  - timeout 120s (gemma4:12b cold-load ~88s 実測)
  - Activate 時に warmup を非同期 fire-and-forget → 初回入力時はモデル warm 状態

## Tasks
### Completed
- [x] #1 再ログイン後 ノートパッドで 5 モード動作確認
- [x] #5 SKK 辞書統合 (SKK-JISYO.L)
- [x] #9 モード切替バグ修正 (Global compartment が Deactivate で消える問題)
- [x] #10 MeCab 統合 (vcpkg + mecab + UniDic-Lite + argv 配列 createModel)
- [x] #11 動詞・形容詞は surface を 1 位候補に
- [x] #12 動詞活用形の漢字化 (KanjifyVerbSurface)
- [x] **SKK vs MeCab 優先順位**: MergeMecabVerbForms で SKK ヒット時に MeCab 動詞 form を prepend
- [x] **#13 Ollama fallback**: LooksSuspect + StartMecabSupplementAsync 実装、ビルド通過
- [x] **KanjifyByReading**: 連用形対応で みた/もえた/おりた のケースが正しく漢字化
- [x] **LearningStore fix**: ghost fav prepend 削除 + 学習データ退避
- [x] **動作確認** (6/6): `みた→見た` / `もえた→燃えた` / `おりた→下りた` / `せいで→せいで` (Ollama fallback) / `あめ→雨` / `あした→明日` 全て期待通り
- [x] **Ollama インストール + warmup**: OllamaSetup → `ollama pull gemma4:12b` 完了、Activate warmup で常駐

### Pending
- [ ] #2 文節分割 Phase B: 文節別候補選択 UI (Space / Tab で文節フォーカス、各文節内で候補サイクル)
- [ ] #3 再変換（確定済み文字列の再選択）
- [ ] #4 候補ウィンドウのページング表示数増加 / ヒントテキスト
- [ ] #6 x86 ビルド対応
- [ ] #7 HKLM\CTF\Assemblies\0x00000411 登録
- [ ] #8 署名・インストーラ
- [ ] kSuspect 稀字リストの拡張 (動作確認しながら追加)
- [ ] 促音便 (`itta`, `sukutta`, `owatta`) の KanjifyVerbSurface 対応

## Files & Code Locations

### このセッションで変更
- `src/GenerativeIME.Tsf/mecabanalyzer.h`:
  - `MecabMorpheme.lemmaReading` (UniDic field 6、ひらがな化) を追加
- `src/GenerativeIME.Tsf/mecabanalyzer.cpp`:
  - field 6 を取得して katakana→hiragana 変換 (0x30A1〜0x30F6 を -0x60)
  - debug log に reading=... を追加
- `src/GenerativeIME.Tsf/bunsetsu.h`:
  - `MergeMecabVerbForms(reading, analyzer, skkCandidates) → vector<wstring>` を追加
  - `LooksSuspect(reading, analyzer) → bool` を追加
- `src/GenerativeIME.Tsf/bunsetsu.cpp`:
  - `KanjifyByReading(surface, lemma, lemmaReading)` 新規。lemmaReading 空時は旧 KanjifyVerbSurface fallback を保持
  - `MergeMecabVerbForms` 実装: MeCab.Analyze で coverage 確認 → 動詞/形容詞含むときに mecabTop 生成 → skkCandidates 先頭 prepend (dedup)
  - `LooksSuspect` 実装: Trigger A (lemma に kSuspect 漢字) OR Trigger B (5+ 形態素 AND reading 6+ 文字)
  - kSuspect = 顎/為/居/出/御/様/等/処/時/故/沢/殆/凡/矢/兎/宛/何/嘗/只/迄/謂/勿/論/尤/所/如
  - SplitMecab の verb branch も KanjifyByReading に置換
- `src/GenerativeIME.Tsf/learningstore.cpp`:
  - Reorder の `else` ブランチ (fav が候補にない場合の強制 prepend) を削除し `return candidates;` に
- `src/GenerativeIME.Tsf/textservice.h`:
  - `PendingOllamaFallbackRequest` 前方宣言
  - `StartMecabSupplementAsync`, `HandleOllamaFallbackDone` 宣言
- `src/GenerativeIME.Tsf/textservice.cpp`:
  - `WM_OLLAMA_FALLBACK_DONE = WM_USER + 5` 追加
  - `PendingOllamaFallbackRequest` 構造体追加 (reading / recentContext / mecabTop / candidates / seq / hr)
  - `TryOllamaConvertAsync` SKK ヒットパスで `MergeMecabVerbForms` 呼び出し + ログ
  - `TryOllamaConvertAsync` MeCab パスで `LooksSuspect` → `StartMecabSupplementAsync` 起動
  - `StartMecabSupplementAsync` 実装: Ollama に「形態素解析の答えと違うもっと自然な変換 3 つ」を JSON で要求
  - `HandleOllamaFallbackDone` 実装: seq stale / 候補ウィンドウ状態 / 選択中インデックス チェック → 候補リスト先頭 prepend
  - `StaticWndProc` に WM_OLLAMA_FALLBACK_DONE ハンドラ追加
- `src/GenerativeIME.Tsf/GenerativeIME.Tsf.vcxproj`:
  - `<PlatformToolset>v143</PlatformToolset>` → `v145` (VS 2026 v18 用)

### 新規追加 (このマシンの環境)
- `C:\vcpkg\` (clone + bootstrap + mecab:x64-windows install)
- `third_party/mecab/unidic-lite/` (PyPI sdist から手動展開、約 248MB)
  - SETUP-NEW-MACHINE.md は Python 経由を想定しているが、Python 不在マシンでは tar.gz 直接 DL で代替

## Errors / Unresolved
- **動作確認未実施** — コードは通って DLL が生成されたが、regsvr32 と再ログイン、ノートパッドでの入力テストはまだ
- 促音便動詞の漢字化（KanjifyVerbSurface の制約）は引き続き未対応
- 複合語 (`chuugakusei`, `konpyuutaa`) の UniDic-Lite 語彙不足は #13 のフォールバックで救えるか実証待ち
- kSuspect 稀字リストは初期推定。実環境で誤検出 / 取りこぼしを観察して拡張する

## Next Steps
1. **動作確認**: ユーザーに regsvr32 と再ログインを依頼し、`みた` / `もえた` / `せいで` / `あめ` を一通り入力
2. 問題なければコミット (SKK+MeCab merge と #13 を 1〜2 コミットに整理)
3. **Phase B (#2)** に着手: 文節別候補選択 UI

直近の判断保留:
- kSuspect リストの粒度（広くするか厳しくするか）
- Ollama fallback 発火時に candidate window 上で「待機中」インジケーターを出すべきか

## Notes

### MeCab + UniDic-Lite アーキテクチャ
- vcpkg: `C:\vcpkg`、`mecab:x64-windows`
- 辞書: `third_party/mecab/unidic-lite/` (sys.dic 179MB、matrix.bin 68MB、合計 248MB)
- ロード: `MecabAnalyzer::GetGlobal()` で 1 度だけ、Activate 時にウォームアップ
- UTF-8 + forward-slash パスで `MeCab::createModel(argc, argv)` の **argv 配列版** に渡す
- Debug ビルド時は **Debug 版 mecab.dll** をリンク・配置（Release 版とは ABI 違う）

### SKK+MeCab merge ロジック (MergeMecabVerbForms)
- MeCab で reading 全体を分析、surface 結合が reading と一致しない (= 部分カバレッジ) なら何もしない
- 形態素のいずれかが 動詞 / 形容詞 でなければ何もしない (純粋名詞は SKK が強い: 雨/飴/天)
- 各形態素から「mecabTop」を生成:
  - 動詞: `KanjifyVerbSurface(surface, lemma)`
  - 助詞/助動詞/記号: surface そのまま
  - その他: lemma (空なら surface)
- mecabTop != reading なら skkCandidates の先頭に prepend (重複排除)

### #13 Ollama fallback アーキテクチャ
- 発火条件: `bunsetsu::LooksSuspect(reading, analyzer)`
  - MeCab analysis size >= 3
  - 任意の形態素の lemma 中に kSuspect の漢字 1 字以上
- 非同期 worker → `gemma4:12b` に「MeCab の答えと違う、より自然な変換を 3 つ」プロンプト
- WM_OLLAMA_FALLBACK_DONE で IME スレッドに戻して候補リスト先頭 prepend
- stale チェック: m_reorderSeq, candidate window visible, m_lastReading 一致, GetSelectedIndex() == 0

### UniDic feature CSV layout (unidic-lite 17-field)
- [0] 品詞 / [4] 活用型 / [5] 活用形 / [6] 語彙素読み / [7] 語彙素 lemma / [8] 書字形

### 動詞活用形漢字化アルゴリズム (KanjifyVerbSurface, bunsetsu.cpp)
- lemma を「漢字 prefix + ひらがな suffix」に分解 (例: `食べる` → `食` + `べる`)
- surface の末尾と hira suffix の頭で最長一致探索
- 一致前を漢字 prefix で置換
- 促音便など末尾一致しないものは surface のまま

### SKK okuri-ari の扱い
- 末尾 ASCII 1 字を除いた stem を reading として保存
- okuri code `i` は `<stem>い` の先頭に「<漢字 stem>い」を合成挿入
- それ以外は deferredOkuri に貯めて末尾 append (姓 etc の okuri-nashi より下げる)

### 開発フロー（このマシン）
- ビルド: `& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' 'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\GenerativeIME.Tsf.vcxproj' /p:Configuration=Debug /p:Platform=x64`
- DLL ロック対策: `Move-Item -Path $dll -Destination "$dll.locked.$(Get-Random)" -Force`
- カテゴリ追加なし → 再ログインのみで反映
- Visual Studio 2026 Community v18 (`C:\Program Files\Microsoft Visual Studio\18\Community\`)
  - **PlatformToolset は v145** (cl 14.50.x)、v143 / v180 ではビルド不可
- Windows SDK: `C:\Windows Kits\10\Include\10.0.28000.0\um\`

### Ollama
- モデル: `gemma4:12b`、warmup 後 600–1000ms、cold start 30–60s
- warmup: `Invoke-RestMethod -Uri http://127.0.0.1:11434/api/generate -Method Post -ContentType 'application/json' -Body (@{model='gemma4:12b';prompt='warmup';stream=$false;keep_alive='30m';think=$false} | ConvertTo-Json)`
- think=false 必須

### 既存のコミット
- `097c6cc` Add MeCab+UniDic-Lite morphological analysis + setup docs
- `4422b10` Add SKK dictionary + context-aware Ollama reorder
- `fc0fc5e` Initial import of GenerativeIME (TSF IME + Core + Sandbox)
