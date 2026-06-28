---
project_path: C:\Git\GenerativeIME
saved_at: 2026-06-28
title: GenerativeIME — MeCab+UniDic 統合まで完了、文節別候補選択 (Phase B) 着手前
---

## Summary
TSF ベースの C++ IME。Win11 のモードピル + 5 モード（ひらがな / 全角カナ / 半角カナ / 全角英数 / 半角英数）を備え、Ollama (`gemma4:12b`) を非同期 backend に持ち、文脈考慮の reorder も実装済。SKK-JISYO.L (4.5MB) と MeCab + UniDic-Lite (vcpkg) で形態素解析 → 文節分割を行うハイブリッド構成。

最新の `4422b10` 以降に追加した未コミット作業：
- **MeCab + UniDic-Lite 統合**（vcpkg / mecab.dll Debug 版 / `unidic-lite` 辞書 ~248MB）
- **bunsetsu Phase A 改良**: MeCab 1 形態素ケースも採用、動詞は `KanjifyVerbSurface` で「漢字 stem + surface 末尾 hira」を 1 位、形容詞は lemma 優先、SKK 候補を補完
- **SKK okuri-ari 遅延マージ**: okuri-ari の stem 候補 (例: `みたt /見立/`) を okuri-nashi (`みた /三田/...`) のあとに末尾 append するよう変更
- 詳細な診断ログ (mecab morpheme / MeCab args / SKK greedy / Ollama reorder)

## Tasks
### Completed
- [x] #1 再ログイン後 ノートパッドで 5 モード動作確認
- [x] #5 SKK 辞書統合 (SKK-JISYO.L)
- [x] #9 モード切替バグ修正 (Global compartment が Deactivate で消える問題)
- [x] #10 MeCab 統合 (vcpkg + mecab + UniDic-Lite + argv 配列 createModel)
- [x] #11 動詞・形容詞は surface を 1 位候補に (中間版、その後 #12 で動詞は kanjify)
- [x] #12 動詞活用形の漢字化 (KanjifyVerbSurface: lemma 漢字 stem + surface 末尾 hira の最長一致)

### In Progress / 直前まで詰めてた問題
**SKK 完全マッチが MeCab より優先される問題**:
- `みた` `もえた` `おりた` を入力すると、SKK が `みた /三田/見田/美田/` などにヒットして MeCab パスをスキップ
- okuri-ari (`みたt`, `もえたt`, `おりたt`) の候補（見立 / 燃え立 / 降り立）が候補に混ざる
- 直前修正: deferredOkuri マップで okuri-ari の merge を okuri-nashi のあとに延期 → 末尾配置 (skkdictionary.cpp)
- 残課題: okuri-nashi がない `もえた` `おりた` の場合は okuri-ari しかないので、依然「燃え立」「降り立」が先頭。**SKK と MeCab の優先順位を入れ替えるか**、 SKK ヒットでも候補が「実用しなさそう」なら MeCab に流すか、の判断保留

### Pending
- [ ] #2 文節分割 Phase B: 文節別候補選択 UI (Space / Tab で文節フォーカス、各文節内で候補サイクル)
- [ ] #3 再変換（確定済み文字列の再選択）
- [ ] #4 候補ウィンドウのページング表示数増加 / ヒントテキスト
- [ ] #6 x86 ビルド対応
- [ ] #7 HKLM\CTF\Assemblies\0x00000411 登録
- [ ] #8 署名・インストーラ
- [ ] #13 Ollama fallback: MeCab 怪しい分解時に LLM 任せ
- [ ] **(直近)** SKK vs MeCab の優先順位再設計

## Files & Code Locations

### 新規追加（このセッション、未コミット）
- `src/GenerativeIME.Tsf/mecabanalyzer.h` / `.cpp` — MeCab ラッパ。global singleton で 1 度だけ Tagger 作成。argv 配列 API で createModel
  - `MecabMorpheme` 構造体: `surface` / `lemma` / `pos`
  - `MecabAnalyzer::Analyze(text)` — UTF-8 経由で UniDic feature CSV から field 0 (pos) と field 7 (lemma) 取得
- `src/GenerativeIME.Tsf/bunsetsu.h` / `.cpp` — 文節分解
  - `SplitGreedy()` — SKK だけ使う初期実装 (現在 Ollama 前 fallback として残ってる)
  - `SplitMecab()` — MeCab 主体 + SKK 候補補完
  - 動詞 / 形容詞 / 名詞で候補先頭ルールを分けている (cpp 内ヘルパ `KanjifyVerbSurface` 含む)
- `src/GenerativeIME.Tsf/skkdictionary.h` / `.cpp` — SKK-JISYO.L UTF-8 読込
  - okuri-ari の i-adjective 合成 (`あかi → あかい /赤い/`) は今もある
  - okuri-ari の其他 stem は `deferredOkuri` に貯めて末尾 append (今回追加)
- `third_party/skk/SKK-JISYO.L`, `.utf8` — git に commit 済
- `third_party/mecab/unidic-lite/` — .gitignore 済。新マシンは `pip install unidic-lite` + setup script で復元

### 変更
- `src/GenerativeIME.Tsf/textservice.cpp`:
  - `TryOllamaConvertAsync` 順序: SymbolDictionary → SKK 完全マッチ + 文脈 reorder → MeCab split (1 morpheme OK) → SKK greedy fallback → Ollama
  - 文脈バッファ `m_recentContext` (60 char rolling)、commit パスで append
  - `StartReorderAsync` / `HandleOllamaReorderDone` で SKK 結果の文脈 reorder
  - 診断ログ多数 (mecab morpheme / SetImeMode / WM_SET_IME_MODE 他)
- `src/GenerativeIME.Tsf/textservice.h`:
  - `m_recentContext`, `m_reorderSeq`, `StartReorderAsync`, `HandleOllamaReorderDone`, `AppendCommittedText`
- `src/GenerativeIME.Tsf/langbaritem.h` / `.cpp`:
  - `m_isImeOn` メンバ削除 (CTextService::IsImeOn() を毎回参照、状態二重持ち回避)
  - `UpdateMode()` 引数なし、早期 return 撤去
- `src/GenerativeIME.Tsf/GenerativeIME.Tsf.vcxproj`:
  - `VcpkgRoot` プロパティ (デフォルト `C:\vcpkg`)
  - Debug は `$(VcpkgInstalled)\debug\lib`、`$(VcpkgInstalled)\debug\bin\mecab.dll` を使う (ABI mismatch 回避)
  - PostBuildEvent で SKK + mecab.dll + UniDic-Lite を OutDir にコピー
- `.gitignore`: `third_party/mecab/unidic-lite/` 追加

## Errors / Unresolved
- `mita` → 「見立」、`moeta` → 「燃え立」、`orita` → 「下り立」: SKK 優先パスで okuri-ari 候補が漏れる問題、最新修正は okuri-nashi がない場合は救えない
- 促音便 (`itta`, `sukutta`, `owatta`) の動詞活用形漢字化は KanjifyVerbSurface でカバー外
- 複合語 (`chuugakusei`, `konpyuutaa`) は UniDic-Lite の語彙不足で誤分解（Ollama fallback 未実装）

## Next Steps
順番 (ユーザー指示で「3 つとも順番に」):
1. **動詞の漢字化** ← 完了 (#12)
2. **Ollama fallback** (#13) ← 次にこれ。MeCab が 3+ 形態素 + lemma に稀字 (顎 / 所為) 含むときに Ollama に投げる
3. **Phase B (#2 文節別候補選択 UI)** ← その次

直近、ユーザーが追加で気にしている分岐:
- SKK 完全マッチ vs MeCab の優先順位設計（今は SKK 先で okuri-ari 汚染が抜けない）

## Notes

### MeCab + UniDic-Lite アーキテクチャ
- vcpkg: `C:\vcpkg`、`mecab:x64-windows`
- 辞書: `third_party/mecab/unidic-lite/` (sys.dic 188MB、matrix.bin 71MB、合計 248MB)
- ロード: `MecabAnalyzer::GetGlobal()` で 1 度だけ、Activate 時にウォームアップ
- UTF-8 + forward-slash パスで `MeCab::createModel(argc, argv)` の **argv 配列版** に渡す（string 版は引用符を残してパス壊す）
- Debug ビルド時は **Debug 版 mecab.dll** をリンク・配置（Release 版とは ABI 違う、stl mismatch で createModel が null になる）

### UniDic feature CSV layout (unidic-lite 17-field)
- [0] 品詞 / [4] 活用型 / [5] 活用形 / [6] 語彙素読み / [7] 語彙素 lemma / [8] 書字形 (連用形ひらがな)

### 動詞活用形漢字化アルゴリズム (KanjifyVerbSurface, bunsetsu.cpp)
- lemma を「漢字 prefix + ひらがな suffix」に分解 (例: `食べる` → `食` + `べる`)
- surface の末尾と hira suffix の頭で最長一致探索
- 一致前を漢字 prefix で置換
- 例: surface=`たべ`, lemma=`食べる` → 末尾 `べ` 一致 → 「食」+「べ」=「食べ」
- 促音便など末尾一致しないものは surface のまま

### SKK okuri-ari の扱い
- 末尾 ASCII 1 字を除いた stem を reading として保存
- okuri code `i` (i-adjective) は `<stem>い` の **先頭** に「<漢字 stem>い」を合成挿入（「あかい /赤い/」を「あかい /赤井/」より優先）
- それ以外 (`t`, `s`, `m`, `k`, `r`, ...) は deferredOkuri に貯めて **末尾** に append (姓 etc の okuri-nashi より下げる)

### 開発フロー（このマシン）
- ビルド: `& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' 'C:\Git\GenerativeIME\src\GenerativeIME.Tsf\GenerativeIME.Tsf.vcxproj' /p:Configuration=Debug /p:Platform=x64`
- DLL ロック対策: `Move-Item -Path $dll -Destination "$dll.locked.$(Get-Random)" -Force`
- カテゴリ追加なし → 再ログインのみで反映
- Visual Studio 2026 Community v18 (`C:\Program Files\Microsoft Visual Studio\18\Community\`)
- Windows SDK: `C:\Windows Kits\10\Include\10.0.28000.0\um\`

### Ollama
- モデル: `gemma4:12b`、warmup 後 600–1000ms、cold start 30–60s
- warmup: `Invoke-RestMethod -Uri http://127.0.0.1:11434/api/generate -Method Post -ContentType 'application/json' -Body (@{model='gemma4:12b';prompt='warmup';stream=$false;keep_alive='30m';think=$false} | ConvertTo-Json)`
- think=false 必須

### 既存のコミット
- `4422b10` Add SKK dictionary + context-aware Ollama reorder
- `fc0fc5e` Initial import of GenerativeIME (TSF IME + Core + Sandbox)
