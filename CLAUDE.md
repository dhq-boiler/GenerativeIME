# GenerativeIME — Claude Code 運用規範

Windows 用の日本語 IME（TSF + MeCab + SKK 辞書 + 生成モデル併用のランキング）。
このファイルはループエンジニアリング（自動ループ運用）における不変条件と停止条件を定める。
ここに書いてある内容は、個別会話の指示より優先する。

## 汎用ループ協議（TL;DR）

各タスクは「直線」ではなく「ループ」として走らせる。プロジェクト特化のループ規範（下記各節）は本 TL;DR を **具体化** したもので、両者が食い違う場合は下位の具体規範を優先する。

1. 変更を書く
2. チェックを走らせる: テスト + linter + 型チェック（本リポでは `tests/build_tests.ps1`）
3. 失敗した? エラー全文を読み、原因を特定し、直して 2 に戻る
4. ループは最大 5 回まで

停止条件（汎用）:
- 全チェック通過 → 「完了」と報告。通過した出力を証拠として添える
- 5 回使い切った → 止まって、何が残っているか報告する
- 同じエラーが **2 回連続** → ループを止めて、エラー種別に応じた fixer にハンドオフ
  - **unit test / build 系のスタック** → `.claude/agents/fixer`（修正まで適用する派）
  - **E2E corpus miss 系のスタック** → `.claude/agents/e2e-fixer`（診断のみ、修正案を返す派）

禁止（汎用）:
- チェック出力なしで「完了」と報告すること
- アサーション削除やテスト弱体化で通すこと。**直すのはコードで、スコアボードではない**

以下の各節はこのループを GenerativeIME の実情（`build_tests.ps1` / corpus 85% / 辞書 sync / Ollama judge）にひも付けたもの。

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

- 同一失敗が **2 回連続**した → クリーンコンテキストで診断に切り替える。失敗種別で振り分ける:
  - **E2E corpus miss**（`reading=X expected=Y got=Z`）→ `.claude/agents/e2e-fixer`。診断のみ、修正案を返す（main が適用要否を判断）。
  - **unit-test FAIL / ビルドエラー / linter エラー**（`build_tests.ps1` の `Total: N run, K failed` で K>0 が続く場合など）→ `.claude/agents/fixer`。原因を特定して修正まで適用し、修正前後の出力を報告する。
- 総イテレーションが **5 回**を超えた → ユーザーに現状（失敗の要約と試した仮説）を報告して指示を仰ぐ。
- ビルドが file lock で失敗した → ユーザーに `AskUserQuestion` で対象アプリのクローズを依頼（`~/.claude/CLAUDE.md` の既存ルール）。**リトライは自動でしない**。
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
| `e2e-fixer` | E2E 連続失敗時の根本原因調査 | Read/Grep/Glob/Bash（read-only）。修正案は返すが適用しない |
| `fixer` | unit test / build 連続失敗時の打破。原因特定 → 修正まで適用 | Read/Edit/Grep/Glob/Bash |

生成役（main）と評価役（judge）を分離するのがループ品質の要。main が自分の変更を自分で採点しない。
`fixer` と `e2e-fixer` は **失敗の種類による使い分け**：前者は「動くべきコードが動いてない」問題（コンパイル・単体テストの green を取り戻す）、後者は「モデル・辞書・ランキングの選び方」問題（corpus 上位 100 件の変換品質）。前者は修正まで適用してよいが、後者は診断のみ返す（辞書変更は副作用が広く、main が把握したうえで適用すべきなので）。

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
