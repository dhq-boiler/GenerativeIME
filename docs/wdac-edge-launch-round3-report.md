# WDAC 追加報告: Round 2 修正後も session bind に失敗 (Round 3)

**依頼者**: GenerativeIME 側 (dhq_boiler)
**依頼日時**: 2026-07-12
**発生 status**: Round 2 修正 (commit `9eb5558`) 反映 AMI 後
**関連 doc**: `wdac-edge-launch-session-bind-report.md` (初回),
`reply-generativeime-edge-launcher-round2-2026-07-12.md` (Round 2 回答)

---

## サマリ

Round 2 の 3 件修正 (Bug #1 完全化 / Critical-1 appPid cache /
Critical-2 Chromium rendezvous 回避) 対応ありがとうございました。
**修正の一部は明確に効いています** が、`launch_app` は依然 500 で bind が
確立せず、以降の `focus_window` / `click` / `send_key` が全て 500 になる
状態が続いています。

以下、確認できた変化と残る失敗の証拠を共有します。

---

## Round 2 修正で確認できた **良い変化**

### 1. Chromium single-instance rendezvous 回避 が確実に効いている

`launch_app` 失敗直後の `list_windows` で以下の差分が明確:

**Round 1 (Critical-2 未修正)**:
```
Chrome_WidgetWin_1 x 3 (New tab), 全て bounds=(-8,-8, 1936x1056) = maximized
+ 最初の failed launch から残った orphan Edge windows
```

**Round 2 (Critical-2 修正済)**:
```
Chrome_WidgetWin_1 x 2 (New tab),
  bounds=(20,20, 945x1020)
  bounds=(10,10, 945x1020)
```

- window サイズが 945x1020 の**非 maximized で 2 個並列表示** = `--new-window`
  注入と `--user-data-dir=<per-session>` による独立 profile が効いている証拠
- 2 回目 launch で orphan が居るのに rendezvous していない = 別 profile 経路が動いている

### 2. エラーメッセージが変わり、rebind パス自体は firing

**Round 1**:
```
"No process is associated with this object."  (500)
```

**Round 2**:
```
"Application launched (C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe)
 but main window did not appear within 30s."  (500)
```

- Round 1 は「Process handle が消えた」だが、Round 2 は「main window が来なかった」
- Process には attach できている = **Bug #1 と Critical-1 の appPid cache 化は動作**
- 残る問題は `GetMainWindow(automation, TimeSpan.FromMilliseconds(250))` の
  timeout / rendezvous 側

---

## 残る失敗の再現手順

```
# セッション b79c419b-...(前回 terminate) 後の完全 fresh 状態から

create_session → wait_for_session(Running) → connect_session

launch_app(
  sessionId="fb173ae4-df93-4793-9ea0-dab7344ee9d1",
  executablePath="C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  allowSystemExe=true
)
→ 500 "Application launched but main window did not appear within 30s"

# list_windows で Edge が起動していることは確認できる
list_windows
→ msedge x 2 (pid 4036, "New tab - Profile 1 - Microsoft​ Edge",
             bounds=(20,20, 945x1020) / (10,10, 945x1020))

# しかし session bind が成立してないので以降全滅
focus_window(windowTitle="New tab")
→ 500 "window/focus failed"

# セッション ID: fb173ae4-df93-4793-9ea0-dab7344ee9d1 (terminate 済)
# 前回セッション (Round 1 現象確認用): b79c419b-96c3-4642-9db4-7220b4e5188f
```

---

## 原因推定 (今回)

Round 2 で `GetMainWindow(automation, TimeSpan.FromMilliseconds(250))` の
**250ms timeout** が入りましたが、これが**逆に短すぎる**可能性:

- Edge --new-window で 2 window が起動している = renderer/utility process が
  複数生成される
- 起動直後 300-500ms は main window の UIA node が UI Automation subsystem に
  publish されない (Chromium の `AXPlatformNode` は非同期 register)
- 250ms だと publish 前に GetMainWindow が空返却 → 「main window did not
  appear within 30s」 (この 30s は上位 InitSession のリトライループ側?) に
  伝播

推測される修正:
- `GetMainWindow` の timeout を 250ms → 3000-5000ms 程度に緩める
- あるいは、**GetMainWindow が空を返した場合に `MainWindowHandle` を直接
  polling** する fallback を追加 (UIA subsystem に依存しない Win32 手段)

もしくは:

- Critical-2 で `--new-window` が inject されたことで、**メイン用 first-run
  window (今回の 20,20 の方) と、URL 遷移用 window (10,10 の方) の 2 個** が
  同時 spawn し、FlaUI がどれを "main" とするか判定に迷って空を返している

いずれも上位ループ (30s) が待つ間、UIA 側では Edge の main window は既に
生きて操作可能 = list_windows で見える通り、なのが状況証拠です。

---

## こちら側の状況

- **他の E2E 経路 (WpfDemo / notepad) は完全に動作** (Round 2 修正で
  regression なし、iteration 6 まで通常のバグ修正 loop は続行済み)
- caret-test.html 検証のみが blocking

追加の待ちや調査は問題ないので、優先度は「時間ある時に」で構いません。
急ぎで対応いただかなくても大丈夫です。

---

## 参考

- 過去 spec: `docs/wdac-exe-installer-spec.md`
- 過去要望: `docs/wdac-launch-app-whitelist-request.md`
- 過去障害: `docs/wdac-sandboxagent-401-report.md`, `docs/wdac-edge-launch-session-bind-report.md`
- Round 2 回答: `reply-generativeime-edge-launcher-round2-2026-07-12.md`
- 今回 session ID: `fb173ae4-df93-4793-9ea0-dab7344ee9d1` (terminate 済)
- 参考 window 座標: msedge pid=4036, bounds=(20,20,945,1020) / (10,10,945,1020)
