# WDAC 改修要望仕様: `install_ime` の EXE インストーラ対応

**Author**: GenerativeIME 側 (dhq_boiler)
**Date**: 2026-07-11
**Target repo**: `C:\Git\WinDesktopAppOnCloud` (github.com/dhq-boiler/WinDesktopAppOnCloud)
**Priority**: E2E 自動化ブロッカー

## 1. 背景

WDAC staging は既に `install_ime` + `msiexec /i pkg.msi /qn` 経路で customer-IME を
サンドボックスに投入し、E2E テスト（`activate_tsf_profile` → `ime_type` → `get_ui_tree` で
変換結果を assert）を回せる。GenerativeIME もこの経路で v0.1.3 まで動作確認済み
（既存 installerId=`8ff21210-…`, `56c44728-…`）。

しかし 2026-07 に GenerativeIME の配布形式が **WiX MSI から自作 WPF 単一ファイル EXE
インストーラ**へ切り替わった（commit `d985d5f` "Installer phase 4: CI publishes .exe,
WiX MSI removed"）。以後、以下が発生している:

- ローカルビルドは `installer/build/GenerativeImeInstaller.exe` (123 MB) のみを生成。
- WDAC の `install_ime` は `msiexec /i` を前提としているため EXE を受け付けない。
- 既存 MSI (`56c44728`, 2026-07-02) を使い続けると HEAD から 91 コミット遅れとなり、
  最近の重要修正（`85452ac` でない/1かげつ・`b44c1ea` Backspace・`c4a8186` 何度mお
  regression 等）が E2E 対象から漏れる。

MSI 配布に戻す設計逆行は避けたいので、**WDAC 側で EXE インストーラも受け付ける**改修を
依頼したい。

## 2. スコープ

### 2.1 変更が必要な MCP tool

| Tool | 現状 | 要望 |
|---|---|---|
| `upload_installer` | filename が `.msi` で終わる必要 | `.msi \| .exe` を許容 |
| `prepare_large_installer` | 同上 | 同上 |
| `confirm_uploaded_installer` | 変更なし | 変更なし（sha256 検証は共通） |
| `install_ime` | ダウンロード後 `msiexec /i pkg.msi /qn /norestart {args}` を実行 | 拡張子ベースでディスパッチ（下記 2.2） |
| `list_installers` | `type: "msi"` を返す | 追加 `type: "exe"` を許容 |

### 2.2 `install_ime` の実行分岐

| 拡張子 | DesktopHelper が実行するコマンド |
|---|---|
| `.msi` | 現行通り `msiexec /i "<downloadPath>" /qn /norestart {defaultArgs or overrideArgs}` |
| `.exe` | `Process.Start(new ProcessStartInfo { FileName = "<downloadPath>", Arguments = "{defaultArgs or overrideArgs}", UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, WorkingDirectory = Path.GetDirectoryName(downloadPath) })` |

- どちらも `WaitForExit()` して exit code を回収。
- exit code 0 のみを成功と判定（現行と同一）。
- `requireLogoff` 挙動は EXE でも維持: exit code 0 のあと `shutdown /l /f` → auto-logon。

### 2.3 GenerativeIME 側の silent mode 契約

GenerativeIME はこの改修に合わせて `GenerativeImeInstaller.exe` に `--silent` を実装する
（本リポジトリの並行タスク）。契約は以下:

- コマンドライン: `GenerativeImeInstaller.exe --silent [--aggressive]`
- 動作: WPF ウィザードを表示せず、既定インストール先（`C:\Program Files\GenerativeIME`）に
  埋め込みペイロードを展開・TSF 登録・HKCU seed・ctfmon 再起動まで一括実施。
- 標準出力/標準エラーに進捗ログを吐き、`WaitForExit` 後に stdout/stderr を回収可能。
- 終了コード:
  - `0` = 成功
  - `1` = 一般的失敗（例外・ペイロード欠落）
  - `2` = ファイルロック（`Safe` モードで既存プロセスがファイルを掴んでいた）
  - `3` = 権限不足（UAC 昇格失敗）
- `install_ime(overrideArgs: "--silent --aggressive")` で E2E からロック無視の
  強制インストールを指示する想定。

### 2.4 期待する E2E フロー (改修後)

```
# 前提: upload_installer("GenerativeImeInstaller.exe", base64) → installerId

confirm_uploaded_installer(installerId, sha256)   # 変更なし
→ create_session(WpfDemo) → wait_for_session(Running)
→ connect_session → launch_app(notepad)
→ install_ime(installerId, overrideArgs="--silent --aggressive")
   # DesktopHelper 内部で GenerativeImeInstaller.exe --silent --aggressive を起動
   # exit 0 → logoff/logon → 完了
→ get_install_status → completed (exitCode 0)
→ (再接続) → launch_app(notepad)
→ list_tsf_profiles(0x0411) → GenerativeIME + MS-IME 2件確認
→ activate_tsf_profile(clsid, profileGuid, 0x0411) → hrEnable=0, hrActivate=0
→ activate_keyboard_layout("00000411") → set_ime_state(true)
→ list_process_modules(pid=notepad, filter="GenerativeIME|mecab")
   → GenerativeIME.Tsf.dll 実ロード確認 ★決定的証拠★
→ ime_type("toukyou") → send_key(SPACE) → send_key(ENTER)
→ get_ui_tree(controlTypes="Edit,Document") → valueCurrent=="東京" ✅
```

## 3. 非機能要件

- **サイズ**: `GenerativeImeInstaller.exe` は 123 MB 前後（framework-dependent 単一 EXE）。
  `prepare_large_installer` + `aws s3 cp` 経路が現行で >1 MB をカバー済みなので、
  そのまま利用可能。
- **セキュリティ**: EXE は署名なし（GenerativeIME は個人 OSS プロジェクト）。既存 MSI と
  同じく DesktopHelper 側で Authenticode 検証を要求しない前提。
- **ログ**: EXE の stdout/stderr を `get_install_status` の response に含めてもらえると
  デバッグが劇的に楽になる（現状 exitCode のみ）。任意で `stdoutTail: string`, `stderrTail: string`
  を追加してほしい（各末尾 8 KB 程度で十分）。

## 4. 実装ヒント（WDAC 側担当エージェント向け）

- DesktopHelper (`WdacControlPlane.SandboxAgent`) の `POST /api/desktop/install` 実装で
  `Path.GetExtension(downloadPath)` を見て `msiexec` パス / 直接起動パスを分岐。
- installer type を DB 列に持つ場合、既存 `type="msi"` を残しつつ `type="exe"` を追加。
  migration は forward-only（過去 MSI の row を触らない）。
- `upload_installer` / `prepare_large_installer` の入力バリデーション（filename 拡張子）は
  `.msi|.exe` の case-insensitive regex に統一。
- ControlPlane 側 `POST /api/installers/confirm` の `s3Key` サフィックスは既に filename から
  引いているので、拡張子変更で追加変更は不要のはず。

## 5. 検収項目

1. `upload_installer(filename="Foo.exe", base64=...)` が installerId を返し、`type="exe"` で
   `list_installers` に表示される。
2. `install_ime(installerId, overrideArgs="--silent")` が exit 0 で completed になる（EXE
   側が正しく実装されている前提）。
3. `install_ime` 直後の `list_process_modules` に `GenerativeIME.Tsf.dll` が現れる。
4. 既存 MSI 経路（`msi` type installerId）が regression なく動く。

## 6. GenerativeIME 側の並行作業

このスペック手渡し後、以下を本リポジトリで進める（並行）:

- [ ] `App.xaml.cs` に `--silent` / `--aggressive` フラグ解析追加
- [ ] 上記フラグ検出時、`InstallerShell` を表示せず `InstallationService.InstallAsync` を
  直接呼び出す silent runner を追加
- [ ] `Program.Main` の返り値を silent 時に `InstallResult.Success` から派生させて exit code
  を正しく返す
- [ ] `installer/build_installer.ps1` の smoke test に `--silent` パスを追加（ローカル検証）

## 7. 参考: 過去 memory

- [[wdac-e2e-harness]] — 現行 E2E フロー・既存 MSI 情報
- [[wdac-tsf-activation-gap]] — v0.1.3 MSI での TSF activation 手順（改修後もこの手順は不変）
- [[wdac-large-installer-sha256-gap]] — 1 MB 超 installer の sha256 検証（既に解決済み）
