# WDAC 追加報告: `launch_app` で Edge を起動すると session bind に失敗

**依頼者**: GenerativeIME 側 (dhq_boiler)
**依頼日**: 2026-07-12
**発生 status**: v83 + 新 AMI 反映後 (`allowSystemExe=true` は正常動作)
**関連 doc**: `docs/wdac-launch-app-whitelist-request.md`,
`docs/wdac-exe-installer-spec.md`

---

## サマリ

`allowSystemExe=true` の追加ありがとうございました。**whitelist 通過は
確認済み** (notepad は launch_app で正常に session bind し、
click / send_key / find_element すべて OK)。

一方 **msedge.exe は launch_app が 500 を返して session が bind されず**、
以後の click / send_key / focus_window がすべて 500 で反射されます。
Edge プロセス自体は起動しているが、SandboxAgent が Process handle を
掴めていない状態です。

再現手順、原因推定、望ましい修正案を共有します。

---

## 再現手順

```
create_session → wait_for_session(Running) → connect_session
list_windows → 起動中の子windowは Edge / notepad なし (fresh sandbox)

launch_app(
  sessionId=<sid>,
  executablePath="C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  allowSystemExe=true
)
→ {
     "success": false,
     "error": "session/init failed (500): {\"error\":\"No process is associated with this object.\"}",
     "statusCode": 500,
     "retryable": false
   }

# Edge プロセスは実際は起動している
list_windows
→ msedge の window が 5 個返る (pid 5900 で共通)
   - "New tab - Profile 1 - Microsoft Edge" x 4
   - "Amazon DCV WebAuthn Redirection Extension added to Microsoft Edge" (modal)

# しかし focus / click は全滅
focus_window(windowTitle="New tab")
→ 500 "window/focus failed"

click(x=100, y=100)
→ 500 "api/desktop/input/click failed"

# 同セッションで notepad は問題なく bind できる
launch_app(
  executablePath="C:\\Windows\\System32\\notepad.exe",
  allowSystemExe=true
)
→ { "status": "initialized" }  ✅

# 以後 click / send_key / find_element 全て OK
```

## x64 path (`C:\Program Files\Microsoft\Edge\Application\msedge.exe`)

このパスは 404 で「Executable not found」でした:

```
launch_app(
  executablePath="C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
  allowSystemExe=true
)
→ 404 ARTIFACT_NOT_READY, actualPaths=[]
```

こちらの AMI では x86 launcher パスにしか Edge が居ないようです。x64 の
`C:\Program Files\...` にはインストールされていない模様。allowlist に x64
パスを載せていても実際に無いので、`allowSystemExe=true` + 存在チェックで
明示的な「Edge 未インストール」エラーになるとありがたいです (whitelist 通過は
していますが `File.Exists` 失敗パスに落ちている雰囲気)。

---

## 原因推定

`msedge.exe` は「launcher stub」で、起動時に子 Edge 本体プロセスを spawn した
直後に**自身は exit** する動作をします。Chrome / Edge の一般的挙動で、
`chrome.exe --single-process` 相当のフラグを渡さない限り常にこうなります。

現状の `launch_app` 実装 (推定) は:
1. `Process.Start(msedge.exe)` を呼ぶ
2. 返ってきた Process 参照を desktop session に紐付ける
3. 以降の click / send_key はその Process の HWND を対象にする

しかし msedge.exe launcher は spawn 直後に exit してしまうので、SandboxAgent が
保持している Process 参照は「既に死んだ launcher」を指しています。
以後の Process.MainWindowHandle アクセスが `InvalidOperationException:
No process is associated with this object` を投げるのが、今回の 500 の中身の
はず。

同じ理由で Chrome / Firefox / Slack のような multi-process app 全般が同じ
症状になる可能性が高いです。

---

## 修正案 (WDAC 側で選択いただく)

### 案 A (推奨): child process への rebind

`Process.Start` で得た Process が `WaitForExit(0)` で即座に終了しているのを
検出したら、`Process.GetProcessesByName("msedge")` などで生きている子
プロセスを探して bind し直す。

- 実装: launch_app の end of pipeline に `if (!process.HasExited) rebind` の
  ロジックを追加。exited なら親フォルダの `Path.GetFileNameWithoutExtension`
  でプロセス名を得て `Process.GetProcessesByName(...)` から一番古いものを選ぶ。
- 落とし穴: 既存の同名プロセスが sandbox に残っていた場合、それを掴む可能性
  → sandbox は fresh なので実質問題にならないはず。ゼロにするには
  「launch_app 呼び出し直前に一覧を撮って diff」推奨。
- Chrome / Firefox / Slack / VS Code などにも波及効果あり。

### 案 B: launcher モード追加

`launch_app` に `launcherMode: "attach-child" | "direct"` を追加。
`direct` (default) は現状動作、`attach-child` は案 A のロジックを実行。

- customer 側の意図が明示的になる (Edge / Chrome / Firefox / Slack など
  multi-process 系だけ opt in)
- 実装コストは案 A + 1 パラメータ

### 案 C: 別 tool `launch_browser_with_url`

`launch_browser_with_url(sessionId, url, browser="edge")` を新設。
内部で:
1. Edge を絶対 path で起動
2. rebind ロジックで子プロセスを掴む
3. `--new-window <url>` を渡して指定 URL をロード

- 起動 + navigate まで one shot で完結 → E2E ハーネスが簡潔に
- caret-test.html を **1 tool call で開ける**ようになる
- 案 A / B のロジックはこの tool 内で完結してもよい

---

## E2E 用途としての具体的要望

こちら側の caret-test は「Edge を allowSystemExe で起動 → Ctrl+L で URL bar
→ type_text で URL → Enter → HTML rendered → get_ui_tree で input-sel/
area-sel/ce-sel を読む」で回したいです。

そのため必要なのは:
1. Edge が起動できる (✅ プロセスは起動している)
2. Edge の window に click / send_key / send_key_combo が届く (❌ 今ここ)
3. `get_ui_tree(controlTypes="Edit,Text")` が Edge の DOM を UIA node として
   返す (未確認、2. が通ればテスト可能)

上記案 A / B / C いずれでも 1-2 は解決します。

案 C だと 3 の前に URL 直接 navigate ができる (Ctrl+L 経由不要) ので、
`arguments` / `--new-window <url>` のような文字列渡しがあれば E2E コードが
劇的に短くなります。優先順位は `A ≥ C > B` くらいで考えています。

---

## 検収項目 (実装後の期待挙動)

- [ ] `launch_app(msedge.exe, allowSystemExe=true)` が `status: initialized`
       で返り、以降の click / focus_window / send_key が Edge 対象で通る
- [ ] Edge 起動後の `find_element(controlType="Edit")` が address bar と
       page内 `<input>` の両方を UIA node として返す
- [ ] `send_key(CTRL+L) → type_text(<url>) → send_key(ENTER)` で URL
       navigate 成功
- [ ] (案 C の場合) `launch_browser_with_url(url=...)` で navigate まで完結
- [ ] notepad path (v83 で確認済み) の regression なし

---

## nice-to-have (継続要望)

- Edge が未インストールの AMI で `launch_app(msedge.exe)` が
  `File.Exists` 失敗で明示的なエラーコード (`EXE_NOT_INSTALLED` 等) を
  返してくれると、"whitelist 外" vs "未インストール" の切り分けが早い
- 現状 x64 パスは 404 + `ARTIFACT_NOT_READY`, x86 パスは 500 + `No process is
  associated` と、Edge が「あるのに掴めない」か「そもそもない」かの判別が
  エラー本体を読む必要あり

---

## 対応いただけない場合の代替案 (こちら側の workaround)

- **A**: `WpfDemo` に WebView2 コントロールを埋めて caret-test.html を app 内で
  表示 → Chromium quirk は再現するが、customer 側で app を build し直す必要
- **B**: caret-test.html と等価な機能を WPF ネイティブ TextBox で再実装した
  sample app を `C:\Work` 配下に配置 → OS-native ホストのみカバー、
  Chromium quirk は再現不可

---

## 参考

- 過去 spec: `docs/wdac-exe-installer-spec.md`
- 過去要望書: `docs/wdac-launch-app-whitelist-request.md`
- 過去障害報告: `docs/wdac-sandboxagent-401-report.md`
- caret-test.html: https://dhq-boiler.github.io/GenerativeIME/e2e/caret-test.html
- 再現セッション ID: `75a794a9-7d17-40f8-bc0b-064610abe4aa` (2026-07-12,
  すでに terminate 済み)
