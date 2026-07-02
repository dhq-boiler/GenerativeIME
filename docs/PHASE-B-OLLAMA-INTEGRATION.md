# Phase B + Ollama Integration — 設計メモ

ステータス: ドラフト (2026-06-29)
スコープ: Pending タスク「Phase B + Ollama 統合」の実装方針案。実装は別 PR。

## 問題定義

現状 Phase B (per-bunsetsu UI) と Ollama fallback は**排他**。`HandleOllamaFallbackDone` の `textservice.cpp:1462` が `InBunsetsuMode()` で early-bail するため、ユーザが Tab で文節フォーカスを動かし始めた瞬間に LLM 補完の経路が死ぬ。

```cpp
// textservice.cpp:1462
if (InBunsetsuMode())
{
    OutputDebugStringW(L"[GenerativeIME] Ollama fallback: skipping in Phase B mode\n");
    delete pending;
    return;
}
```

これは初期実装の妥協 — Ollama に「全体 reading の代替変換 3 つ」を要求しているため、文節を 1 個ずつ調整している UI に whole-reading の結果を流し込む綺麗な落としどころがなかった。

## 現状の流れ

```
ユーザ入力 (composition)
   ↓
TryOllamaConvertAsync (textservice.cpp)
   ├── PunctPairs fast path
   ├── whole-reading fav fast path
   ├── SKK ヒット → MergeMecabVerbForms → 候補確定
   └── MeCab fallback
        ├── bunsetsu::LooksSuspect(reading) == false
        │     → 候補確定 (Ollama 不発火)
        └── LooksSuspect == true
              → StartMecabSupplementAsync
                   ↓ async worker (~1s)
                   ↓ Ollama に「全 reading の代替 3 つ」要求
                   ↓ WM_OLLAMA_FALLBACK_DONE post
                   ↓
              HandleOllamaFallbackDone
                  ├── stale check (seq / window / reading)
                  ├── ★ InBunsetsuMode → drop ★
                  ├── ReadsAs filter
                  └── 候補リスト先頭に prepend
```

★ が本ドキュメントで取り組む箇所。

## 使用シナリオ (User Story)

1. ユーザが「えくすくらめーしょんまーく」と入力
2. LooksSuspect Trigger D で Ollama 発火、結果「エクスクラメーションマーク」が候補先頭
3. ユーザは結果に満足せず Tab で Phase B に入り、「えくす / くらめ / ーしょん / まーく」のように分けて 1 文節目だけ修正したい
4. **今**: Phase B モードに入った時点で次の Ollama call (例: 文節 resize 後) は全部 drop される
5. **欲しい挙動**: 着目文節「えくす」 reading に対して Ollama に「これの自然な変換 3 つ」を聞き、その文節の候補リストに prepend する

## 設計選択肢

### A. focused-bunsetsu 単独 Ollama (推奨)

- **入力**: 着目文節の reading のみ
- **出力**: その reading に対する変換候補 3 つ
- **反映**: 着目文節の `candidates` 先頭に prepend
- **トリガー**: 着目文節の reading に対して `LooksSuspect` が true、かつ既にその文節に対する fallback が走ってない時
- **利点**: 既存 Phase B UX を一切壊さない。実装が一番浅い (新規関数 1 個 + early-bail を条件分岐に置き換え)
- **欠点**: 文脈情報を失う (隣接文節の reading を見ない)。「えくす」だけ渡されても LLM が「エクス」「縁故」「衛工」など複数解釈で揺れる

### B. context-aware focused-bunsetsu

- **入力**: 着目文節の reading + 前後文節の selected 表記
- **出力**: 着目文節の代替候補 3 つ
- **反映**: A と同じ
- **トリガー**: A と同じ
- **利点**: 文脈考慮で「えくす」 + 後ろ「クラメーションマーク」 → 「エクス」を優先的に返す
- **欠点**: プロンプトが長くなり cold-load レイテンシ増。LLM のプロンプト遵守が悪化する場合あり (gemma4 で確認要)

### C. 再分割提案 (re-split)

- **入力**: 全 reading + 現在の境界配列
- **出力**: 改善された境界 + 各文節の変換
- **反映**: `m_bunsetsuList` を再構築 (focus 位置は維持)
- **利点**: 「えくすくらめーしょんまーく」を「エクスクラメーション + マーク」に切り直すような根本改善
- **欠点**: ユーザが既に手で調整した境界を壊す可能性。ユーザの「触った文節は保持」を保証する余分なロジックが必要。LLM の境界提案が安定するか未検証

### D. non-blocking marginalia (別領域表示)

- **入力**: 全 reading or 着目文節 reading
- **出力**: 候補
- **反映**: 候補ウィンドウに「Ollama 提案」セクションを追加、現候補リストとは別領域で表示
- **利点**: 既存挙動を一切壊さず純粋に情報追加
- **欠点**: 候補ウィンドウ UI 拡張が必要 (描画 / 高さ / キー操作)。実装コスト最大

## 推奨アプローチ

**まず A、効果を見て B**。理由:

- A は 1 日で実装可能、UX 退行リスクほぼゼロ
- A だけでも user story の主要部分 (文節調整 + Ollama 補完の併存) を解決
- 「えくす」だけでは弱い問題は、whole-reading fav fast path にユーザが学習させる別経路で先に救われる
- B への拡張は A のコードを差し替える形で実装可能、PR 分離容易
- C / D は要件が膨らんでから検討

## 実装スケッチ (Option A)

### 新規 (textservice.cpp / .h)

```cpp
// 着目文節に対する Ollama 補完。bunsetsu mode で呼び出される。
void StartFocusedBunsetsuOllamaAsync(ITfContext* pContext,
                                     size_t bunsetsuIndex,
                                     const std::wstring& reading);

// 別 worker 用構造体 (PendingOllamaFallbackRequest と分離)
struct PendingFocusedBunsetsuRequest
{
    size_t                    bunsetsuIndex;  // 完了時にここに反映
    std::wstring              reading;        // stale check 用
    std::vector<std::wstring> candidates;     // worker から戻り
    LONG                      seq;            // m_reorderSeq snapshot
    HRESULT                   hr;
};
```

新規 WM メッセージ: `WM_OLLAMA_BUNSETSU_DONE = WM_USER + 6`

### 変更箇所

1. **HandleOllamaFallbackDone (textservice.cpp:1462)**
   - `InBunsetsuMode()` の early-bail を**削除**しない (whole-reading 用の path は維持)。代わりに既存メッセージは whole-reading 経路専用と明示
   - WM_OLLAMA_BUNSETSU_DONE 用の新規ハンドラを別追加

2. **StartFocusedBunsetsuOllamaAsync の呼び出し点**
   - Phase B モード中の **resize 完了時** (`ResizeFocusedBunsetsu` の末尾) と **focus 変更時** に着目文節の reading を見て LooksSuspect ならキック
   - 同一文節への重複発火は seq + bunsetsuIndex で抑止

3. **HandleFocusedBunsetsuOllamaDone (新規)**
   - stale check: `pending->seq != m_reorderSeq` で drop
   - mode check: `InBunsetsuMode() == false` で drop (Phase B を抜けてたら無効)
   - bounds check: `pending->bunsetsuIndex >= m_bunsetsuList.size()` で drop
   - reading check: `m_bunsetsuList[index].reading != pending->reading` で drop
   - ReadsAs filter で reading drift 除去
   - filter 後の各候補を `m_bunsetsuList[index].candidates` の **先頭** に prepend (dedup)
   - 候補ウィンドウを再描画

4. **SetOllamaPending の使い回し**
   - 既存スピナーで両モードを表現可能
   - Phase B 中は焦点文節の枠だけスピン、というのは将来の polish

### プロンプト (Option A 用)

既存 `StartMecabSupplementAsync` のプロンプトは whole-reading 用に「形態素解析の答えと違うもっと自然な変換 3 つ」を要求している。focused-bunsetsu 用は短く:

```
入力: 「<reading>」
これは IME の入力読みです。自然な日本語変換候補を 3 つ JSON 配列で返してください。
読みは変えないでください。例:
入力「えくす」 → ["エクス", "縁故", "X"]
```

`think=false`、`keep_alive='30m'`、`stream=false`。

## エッジケース

1. **focus 変更中の race**
   - ユーザが Tab で focus を進めた直後に前 focus の結果が到着
   - 対策: `pending->bunsetsuIndex` と現 `m_focusedBunsetsu` を比較、不一致なら drop (※ ただし「前 focus の結果でも勝手に裏で更新する」のもアリ。UX 議論)

2. **resize 中の race**
   - resize で reading が変わると `m_reorderSeq` がインクリメントされる前提
   - resize ロジックに seq インクリメントが入ってない場合は要追加

3. **Phase B 抜けてから到着**
   - mode check で drop。完成済み composition は触らない

4. **同一文節への複数 trigger**
   - 1 回目が in-flight 中に 2 回目を発火しない (`m_focusedBunsetsuOllamaPending` フラグ)
   - 1 回目が stale になった場合 (seq mismatch) は 2 回目は新しい seq でキック可

5. **whole-reading Ollama と並行**
   - 全体補完が走ってる最中に Tab で Phase B に入った場合
   - 全体補完は完了時に InBunsetsuMode で drop (現状維持)
   - 別途 focused-bunsetsu 補完がキックされる

## テスト戦略

### unit (test_runner)

- `StartFocusedBunsetsuOllamaAsync` は async + winhttp 依存で unit テストには不向き
- 代わりに pure-logic 分離可能な部分を切り出してテスト:
  - **プロンプト生成関数** `MakeFocusedBunsetsuPrompt(reading)` を testable に
  - **filter** は既存 `ReadsAs` を使い回し、追加テスト不要

### E2E (WDAC-MCP)

WDAC-MCP の VS remote test harness で:
1. IME DLL 登録 / notepad 起動
2. `えくすくらめーしょんまーく` を type_text で入力
3. Space で候補ウィンドウ表示
4. Tab で Phase B 入り
5. 着目文節を「えくす」になるまで Shift+← で resize
6. ~1 秒待機 → screenshot で候補ウィンドウに「エクス」が含まれることを確認
7. ↓ で「エクス」を選び Enter → 着目文節が「エクス」で確定

WDAC-MCP の `screenshot_diff` を使えば「Ollama 補完前 / 後」の候補ウィンドウの差分を pixel-level でロックできる。

## 未決定事項

1. **trigger タイミング**: resize 完了時のみ vs focus 変更時にも (前者だけだと一度 focus した文節への 2 度目 trigger が来ない)
2. **重複発火抑止の粒度**: 文節ごと vs 全 IME インスタンス共通
3. **プロンプトの context inclusion**: A で始めるが、B に上げる閾値は何か (実装後 UX 評価で決める)
4. **focus 変更中の race 対応**: 「前 focus 結果も裏で適用」する方が、ユーザが Tab で行ったり来たりした時に再 query 不要で速い。ただし「触ってない文節が勝手に変わる」感はある
5. **PendingOllamaFallbackRequest と PendingFocusedBunsetsuRequest を統合するか**: 共通フィールド多いので template / variant 化可

## 次のステップ (実装着手時)

1. ResizeFocusedBunsetsu の末尾に seq increment が入っているか確認 (なければ追加)
2. `MakeFocusedBunsetsuPrompt` を bunsetsu.cpp に切り出して unit test 1 件
3. `StartFocusedBunsetsuOllamaAsync` + `HandleFocusedBunsetsuOllamaDone` を textservice.cpp に追加
4. WM_OLLAMA_BUNSETSU_DONE を StaticWndProc に dispatch
5. E2E で挙動確認
