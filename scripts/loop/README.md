# scripts/loop — GenerativeIME E2E 回帰ループ

Qiita 記事「ループエンジニアリング」を GenerativeIME に適用したときの
ループの実体。1 サイクル 1 スクリプト実行の設計。

- 停止条件・禁則はプロジェクト直下の `CLAUDE.md` に集約。
- 判定役 (`e2e-judge`) と診断役 (`e2e-fixer`) は `.claude/agents/`。
- ループの状態は `.claude/state/last_e2e_run.json`（gitignore 済）。

## 使い方

### 1 回だけ回す
```powershell
.\scripts\loop\run_e2e_loop.ps1
```
- `tests\build_tests.ps1` を実行し、出力をパースして状態 JSON に保存。
- 「終わるまで一気に回す」動きはしない — 呼び出し側が結果を見て次を決める。

### ビルド済みバイナリを再利用
```powershell
.\scripts\loop\run_e2e_loop.ps1 -SkipBuild
```
- 辞書だけ差し替えて再確認したいとき用。`build_tests.ps1` の辞書 sync も飛ぶので注意。

### 状態を汚さず観察のみ
```powershell
.\scripts\loop\run_e2e_loop.ps1 -DryRun
```

## 出力 JSON の見方

```json
{
  "verdict": "FAIL",
  "consecutive_fail_count": 2,
  "corpus_misses": [
    { "reading": "し", "expected": "市", "got": "氏" }
  ]
}
```

`consecutive_fail_count >= 2` なら `.claude/agents/e2e-fixer` を呼ぶタイミング。
`iteration >= 5` なら人間へエスカレーション。詳細は `../../CLAUDE.md`。

## Ollama judge の呼び方（メイン Claude から）

```
Agent(
  subagent_type: "e2e-judge",
  description: "corpus miss を採点",
  prompt: "`.claude/state/last_e2e_run.json` を読んで misses を判定し JSON を返して"
)
```

judge の出力（`REAL_MISS` の miss リスト）を根拠に、辞書追加 or ranking 調整を行う。
`AMBIGUOUS` 判定の miss は「そもそも直しようがない」ので手を付けない。

## Ollama fixer の呼び方（連続失敗時）

```
Agent(
  subagent_type: "e2e-fixer",
  description: "「し→市」の連続失敗を診断",
  prompt: "reading=「し」 expected=「市」 got=「氏」で連続 2 回失敗。試した仮説: ... 根本原因を診断して"
)
```

fixer は修正案を Markdown で返す。**適用はメイン Claude が判断**する
（fixer 自身は書き込み権限を持たない — コンテキスト汚染防止のため意図的）。

## 何をしないか

- **自動で無限ループしない**。1 実行 = 1 イテレーション。Claude Code の hooks や
  cron から複数回呼ぶことは可能だが、その場合も CLAUDE.md の停止条件（連続失敗 2、iter 5）を
  必ず守ること。
- **judge/fixer を PowerShell から直接呼ばない**。サブエージェントは Claude Code から呼ぶ。
  スクリプトはあくまで「テスト → JSON 化」まで。
- **判定結果で自動的にコード修正しない**。ループエンジニアリングの「判断放棄」防止。
  最終 diff は必ず人間が読む（CLAUDE.md の禁則参照）。
