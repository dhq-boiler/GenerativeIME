# WDAC 要望: `launch_app` executablePath whitelist に Edge 追加 (or 拡張手段)

**依頼者**: GenerativeIME 側 (dhq_boiler)
**依頼日**: 2026-07-12
**優先度**: Middle — Chromium contenteditable ホストの caret / composition 系
E2E テストがこれ待ち。ロールアウトの遅延ブロッカーというより、テスト
カバレッジを埋めるための機能追加要望です。
**関連過去 doc**: `docs/wdac-exe-installer-spec.md`, `docs/wdac-sandboxagent-401-report.md`

---

## 1. サマリ

WDAC staging の `launch_app` は現在、`executablePath` が
**`[C:\Work, C:\Git, C:\WdocBuild]`** 配下の `.exe` に限定されています。
これは意図した hardening (2026-07-11 の EXE installer 対応リリース以降で
観察) と理解していますが、GenerativeIME 側で以下 2 種類の E2E が
ブロックされているのでご相談したいです:

- **caret / composition の Chromium 系ホスト検証**: `docs/e2e/caret-test.html`
  (github.com/dhq-boiler/GenerativeIME に置いた自作ページ、GitHub Pages で
  配信) を Edge で開き、TSF の compositionstart/update/end + selection
  変化を UI Automation 経由で観測したい (matrix Cat P-5)
- **notepad ベースの入力比較**: 一部のケース (`「」` 括弧内キャレット、
  ホスト差分) を native WPF `WpfDemo.exe` と notepad の両方で見たい

いずれも `msedge.exe` / `notepad.exe` が現行 whitelist の外なので現状不可能
です。以下、選択肢を並べます — 実装コストとハザードで一番良い落とし所を
選んでいただきたく。

---

## 2. 現状のエラー

```
launch_app(sessionId=..., executablePath="C:\\Windows\\System32\\notepad.exe")
→ {"success":false,
   "error":"session/init failed (500): executablePath is not allowed
            (must be a .exe under [C:\\Work, C:\\Git, C:\\WdocBuild]):
            C:\\Windows\\System32\\notepad.exe",
   "statusCode":500,"retryable":false}
```

同じことを Edge の絶対パスでも試しますが、結果は同じで却下されます。

---

## 3. 過去の状況とのギャップ

memory 上の `wdac-tsf-activation-gap` (2026-07-01) の時点では、
notepad は絶対パスで launch できていました。以下は当時の実績記録:

> `launch_app(notepad 絶対パス)` → 正常起動 → `list_process_modules(pid=notepad,
> filter="GenerativeIME|mecab")` → GenerativeIME.Tsf.dll 実ロード確認 ★決定的証拠★

2026-07-11 の改修 (EXE installer 対応 or 別 hardening) で whitelist が
入ったのだと理解していますが、GenerativeIME 側の E2E は notepad が実質
「参考実装」だったので、突然閉じられると Chromium 系ホストの検証手段が
ゼロになります。

---

## 4. 要望 (3 案)

以下のうち **B 案が最良のバランス**と考えています。ただし実装難易度は WDAC 側
判断でお願いします。

### 案 A: whitelist に `C:\Program Files\Microsoft\Edge\Application\` と
`C:\Windows\System32\` を追加

- 最小工数
- ハザード: System32 全開放は "hardening を後退させる" 印象
- なので Edge だけの追加が実際的
- 具体パス:
  - `C:\Program Files\Microsoft\Edge\Application\msedge.exe` (default install)
  - `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe` (32-bit path)

### 案 B (推奨): `launch_app` に `allowSystemExe: bool = false` オプション追加

- default 現状維持 (whitelist 内のみ)
- `allowSystemExe: true` で `C:\Program Files\Microsoft\Edge\Application\` +
  `C:\Windows\System32\` (notepad / cmd 程度) 配下を許可
- 呼び出し側 (MCP client / 我々) が明示的にオプトインする形なので、
  hardening ポリシーは維持したまま、E2E 用途のフラグとして機能
- `WinDesktopAppOnCloud` 側で easy to gate (フラグ名で意図が読める)

### 案 C: `launch_url` の新 tool

- `launch_url(sessionId, url)` を新設 → 内部で Edge を起動して URL を
  navigate
- Edge の絶対パスを client 側で意識する必要なし
- GitHub Pages / S3 に置いた HTML を渡すだけで E2E できる
- 副次的にブラウザテストが全て `mcp__wdac-mcp-staging__*` 経由で完結
- 実装工数はやや大きい (URL の validation, Edge の CLI args, ラック状態
  管理)

---

## 5. caret-test.html を巡る背景 (参考)

`docs/e2e/caret-test.html` は既に本リポジトリに配置し、GitHub Pages で
公開済み:

- URL: `https://dhq-boiler.github.io/GenerativeIME/e2e/caret-test.html`
- 内容: `<input>` / `<textarea>` / `<div contenteditable>` の 3 面 +
  compositionstart/update/end 及び selectionchange の live 監視
  (`<output>` 要素に selection index を吐き出すので UIA `get_ui_tree` から
  読める設計)
- E2E マトリクスでの位置づけ: Cat P-5 (`docs/e2e-conversion-test-matrix-2026-07-02.md`)
- 直近の実バグ (matrix Cat P-4 記載): 「Chromium contenteditable では
  括弧ペア確定後のキャレットが 「｜」 の内側でなく閉じ括弧の外側に
  飛ぶ」 → 再現・修正が Edge 起動を待っている状態

---

## 6. 検収項目 (実装後こちらで確認)

案 B / C のどちらの経路でも、以下が通ることを検証します:

1. `launch_app(msedge.exe, ...)` or `launch_url(https://.../caret-test.html)` が
   `status: initialized` で返る
2. `list_windows` に Edge の main window が出る
3. `get_ui_tree(controlTypes="Edit,Text")` で HTML の `<input>` / `<output>`
   が UIA nodes として取れる
4. `ime_type("[") + send_key("SPACE") + send_key("ENTER")` の後、`<output>`
   の `valueCurrent` が期待した caret index (=1) を報告する
5. UI からブラウザを navigate 中の他タブに切り替えるのに `focus_window` /
   `send_key(CTRL+TAB)` が刺さる

---

## 7. あれば嬉しい (nice-to-have)

- Edge が起動しない environment (AMI 側 Edge 未インストール等) の
  検出 → 明示的なエラーメッセージ (今の 500 だと whitelist 外か Edge 未
  インストールかが判別できない)
- 案 C の場合、`launch_url` に `browser: "edge" | "chrome" | "firefox"`
  引数追加 (今後 Chrome テストが必要になるかも)

---

## 8. 対応いただけない場合の代替案 (こちら側の workaround)

もし whitelist 開放が難しい場合は、以下のいずれかで凌ぐことは可能です。
実装コストと引き換えなので参考まで:

- **A**: WpfDemo に `WebView2` を仕込んで caret-test.html を app 内で表示。
  ただし現行 `WpfDemo` のビルドを我々側からいじれないので、ビルドチームに
  依頼が必要
- **B**: caret-test.html と等価な機能を WPF ネイティブ TextBox / RichTextBox
  で再実装した専用 sample app を `C:\Work` に配置し launch_app で起動。
  Chromium quirk は再現できないので "notepad 相当ホスト" のみカバー
- **C**: 諦めて caret-test は主開発機の Edge で手動テスト (E2E ハーネス外)。
  regression 検知の自動化はできない

---

## 参考

- 過去 spec: `docs/wdac-exe-installer-spec.md` (EXE installer 対応要望)
- 障害報告: `docs/wdac-sandboxagent-401-report.md` (H-12 shared-secret regression)
- E2E マトリクス: `docs/e2e-conversion-test-matrix-2026-07-02.md` の Cat P-4/P-5
- caret-test.html: `docs/e2e/caret-test.html` (このリポジトリ)
- 公開 URL: https://dhq-boiler.github.io/GenerativeIME/e2e/caret-test.html
