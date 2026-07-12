# GenerativeIME — Claude Code 運用規範

Windows 用の日本語 IME（TSF + MeCab + SKK 辞書 + 生成モデル併用のランキング）。
このファイルはループエンジニアリング（自動ループ運用）における不変条件と停止条件を定める。
ここに書いてある内容は、個別会話の指示より優先する。

## 完了条件（Success Criteria）

コード変更を「完了」と宣言してよいのは、次のすべてが満たされた時のみ:

1. `tests/build_tests.ps1` が exit 0 で終わる（`Total: N run, 0 failed`）。
2. `TEST(corpus_top_100plus_pass_rate)` の pass rate が **85% 以上**（`test_runner.cpp:2203` のガード）。
3. 直近の変更が対象にした E2E マトリクス（`docs/e2e/*.md`）で新規失敗ゼロ。
4. 辞書ファイル同期済み（`build_tests.ps1` 内の `Copy-Item` が全成功）。

上記のうち 1 つでも欠けた状態で PR を出したり「完了」と報告してはならない。
テスト側の期待値を変更して通す "ズルパス" は禁止（下の禁則を参照）。

## 停止条件（Stop Conditions）

以下に該当したら、追加の実装ループを回さず作業を止めること。

- 同一テストが **2 回連続で失敗**した → `.claude/agents/e2e-fixer` を呼び、クリーンコンテキストで根本原因診断に切り替える。
- 総イテレーションが **5 回**を超えた → ユーザーに現状（失敗の要約と試した仮説）を報告して指示を仰ぐ。
- ビルドが file lock で失敗した → ユーザーに `AskUserQuestion` で対象アプリのクローズを依頼（`~/.claude/CLAUDE.md` の既存ルール）。
- Ollama 呼び出しが timeout / connection refused を 3 回連続で返した → judge を切り、生の diff だけ提示。

## 禁則（Forbidden Actions）

- **テストの削除・スキップ化**（`TEST(...)` を消す、条件で return する等）。
- **アサーションの緩和**（`EXPECT_EQ_W` の期待値を実測値に合わせる、`EXPECT_TRUE(pass * 100 >= total * 85)` の 85 を下げる等）。
- **`--no-verify` / `--no-gpg-sign` などフックのバイパス**（ユーザーの明示指示なしで）。
- **辞書 sync のスキップ**（`build_tests.ps1` 冒頭の `Copy-Item` を消して速くしようとするなど）。過去に stale SKK-JISYO で偽の regression を踏んだ経緯あり。
- **`SKK-JISYO.L.utf8` などの上流辞書ファイルへの直接書き込み**。追加語は `third_party/skk/SKK-JISYO.godan.utf8` などのコンパニオン辞書、または head-priority overrides（`test_runner.cpp:2837` 近辺のコメント参照）で対応する。

## E2E 回帰ループ（Loop Structure）

`scripts/loop/run_e2e_loop.ps1` がループの器。1 サイクルは次のとおり:

1. **Discovery**: `test_runner.exe` の出力から `[ RUN ]` / `FAIL` / `miss: reading=...` 行を抽出。
2. **Handoff**: 失敗が corpus miss なら `.claude/agents/e2e-judge` に判定させる（REAL_MISS / AMBIGUOUS / UNSURE）。unit-test FAIL は判定不要（それは常に修正すべきバグ）。
3. **Verification**: judge の verdict と過去ラン (`.claude/state/last_e2e_run.json`) を比較。同一失敗が続いていれば連続失敗カウンタ +1。
4. **Persistence**: 今回のラン結果を `.claude/state/last_e2e_run.json` に保存（失敗、連続カウンタ、iter 数）。
5. **Scheduling**: 連続失敗 ≥ 2 → fixer に handoff。iter ≥ 5 → 人間にエスカレーション。

**このループは Claude が自分で回すのではなく**、上記スクリプトが 1 回だけ実行され、結果を JSON に落とし、Claude はそれを読んで次の一手を決める。「終わりまで一気に回す」動きはしない（コスト暴走防止）。

## サブエージェント役割分担

| Agent | 役割 | 権限 |
|-------|------|------|
| main（この Claude） | 実装・辞書追加・head-priority override 追加 | 全ツール |
| `e2e-judge` | corpus miss の妥当性判定（Ollama 呼び出し） | Read/Bash（read-only） |
| `e2e-fixer` | 連続失敗時の根本原因調査 | Read/Grep/Bash（read-only）。修正案は返すが適用しない |

生成役（main）と評価役（judge）を分離するのがループ品質の要。main が自分の変更を自分で採点しない。

## ビルド／テスト権限

`~/.claude/CLAUDE.md` に既に「build/rebuild/clean/test は毎回聞かず自動で実施」の許可あり。
このリポでは上記に加え、`scripts/loop/run_e2e_loop.ps1` の実行も許可済みとみなしてよい。
ただし、**辞書マイニング系スクリプト**（`scripts/mine/*.ps1`）は数十分〜数時間かかるものがあるので、
実行前に `AskUserQuestion` で確認する。

## 参考

- ループエンジニアリングの元記事: https://qiita.com/Syoitu/items/97ed37e7ba9c38dc75d8
- 既存の Ollama judge パターン: `scripts/mine/judge_review_ollama.ps1`
- E2E テストレポートの書式: `docs/e2e-conversion-test-report-2026-07-02.md`
- 記憶ファイル索引: `~/.claude/projects/C--Git-GenerativeIME/memory/MEMORY.md`
