# WDAC で IME 自動テスト — IME ベンダー向け onboarding

このドキュメントは **IME (TSF / IMM32) を開発しているチーム** が
WDAC (WinDesktopAppCloud) を E2E テスト基盤として使い始めるためのガイドです。

**最終更新**: 2026-07-01 (MVP-2 完成版)
**対象読者**: IME 開発エンジニア / QA エンジニア / DevOps

---

## 1. WDAC ができること (3 行で)

- お客様の IME (.msi installer) を **クラウド上の Windows session に注入** できます
- session 内で **ja-JP / ko-KR / zh-CN の MS-IME** がすぐ使える状態で立ち上がります
- **Notepad / WPF / WinForms / その他 Win32 アプリ** を起動し、**漢字変換結果を ValuePattern で取得** して assert できます

---

## 2. 動作確認済みのテストフロー (Quick Start)

```
1. 顧客アカウント作成 (Web UI)
2. お持ちの IME msi を upload (1 回だけ)
3. workspace に「session 起動時に install」と紐付け (1 回だけ)
4. session を作る
   → 自動で IME 注入 + logoff/logon
   → Running になったら接続
5. テストアプリ起動 → activate_keyboard_layout → ime_type → 結果 assert
6. 終わったら session を terminate (EC2 が消えて課金停止)
```

> **所要時間**: 1 セッション作成 〜 install 完了まで **〜10 分**
> (うち IME install + logoff/logon は実測 28 秒)

---

## 3. はじめに必要なもの

| 項目 | 詳細 |
|---|---|
| WDAC アカウント | https://wdac-staging-cp.vmr7k8839ytz4.ap-northeast-1.cs.amazonlightsail.com/ で sign up (staging) |
| IME インストーラ | **.msi 形式** (MVP は msi のみ)。`msiexec /i pkg.msi /qn` で silent install できること |
| Claude Code CLI | WDAC は MCP 経由で動かす。Claude Code に `wdac-mcp-staging` MCP server を登録 |
| テスト対象 app | Notepad (sandbox 同梱) / 自社の WinForms or WPF app (clone & build flow あり) |

### 制約 (MVP scope)

- ❌ **exe / zip + script / dll register 形式の installer は未対応** (v1 で追加予定)
- ❌ **reboot 強制 installer は未対応** — msiexec args に `REBOOT=ReallySuppress` 必須
- ❌ **install 時の license key (env vars) は未対応** (v1 で `install_env` 追加予定)
- ❌ **複数 IME を同一 session に同居** は customer 責任 (TSF 衝突は debug 困難)

---

## 4. MCP CLI からの使い方 (5 分で 1 回走らせる)

### 4-1. login

```
> /mcp
(wdac-mcp-staging を選択 → ブラウザ approve → 8 桁 code を CLI に貼る)
```

### 4-2. installer を upload (initial 1 回だけ)

```
> upload_installer(
    filename: "MyIme-1.0.0.msi",
    fileBase64: "<base64 string>",
    defaultArgs: "REBOOT=ReallySuppress"
  )
→ { installerId: "abc...", sha256: "...", uploadedAt: "..." }
```

> **🚨 注意 (現状の制約)**: 上記 `fileBase64` は MCP tool input に渡る都合上、
> **1 MB 程度が現実的上限** (LLM の output token 制限と context 圧迫のため)。
> 1 MB 超の msi は **aws CLI で直接 S3 にあげる回避 path** を使ってください:
>
> ```bash
> aws s3 cp MyIme.msi s3://wdac-staging-customer-installers-<acctId>/<customerId>/<installerId>/MyIme.msi
> ```
>
> v1 で multipart upload 中継を追加予定。

### 4-3. workspace に「session 起動時 install」を設定 (initial 1 回だけ)

```
> set_workspace_pre_install_packages(
    projectId: "<your-project-id>",
    workspaceId: "<your-workspace-id>",
    packages: [
      { installerId: "abc...", overrideArgs: "REBOOT=ReallySuppress", requireLogoff: true, continueOnFailure: false }
    ]
  )
→ 200 OK
```

### 4-4. session を作って自動 install を確認

```
> create_session(projectId, workspaceId)
> wait_for_session(sessionId)
  # 進捗:
  #  20% SandboxAgent 起動待ち
  #  75% Pre-install 実行中   ← 注入中
  #  80% Enclave artifact 配置済
  # 100% 完了                  ← Running 到達
```

### 4-5. テストを書く (前提: Notepad で「toukyou → 東京」)

```python
connect_session(sessionId)
launch_app("C:\\Windows\\System32\\notepad.exe")
activate_keyboard_layout("00000411")    # ja-JP MS-IME (or your IME's KLID)
set_ime_state(sessionId, enabled=True)
ime_type(sessionId, "toukyou")
send_key(sessionId, "SPACE")            # 変換
send_key(sessionId, "ENTER")            # 確定
tree = get_ui_tree(sessionId, controlTypes="Edit")
assert tree.children[0].valueCurrent == "東京"
```

### 4-6. terminate (課金停止)

```
> terminate_session(sessionId)
```

---

## 5. 各言語の IME 入力リファレンス

WDAC AMI には **ja-JP / ko-KR / zh-CN の MS-IME が pre-install** 済 (お客様の IME と独立)。
お客様の IME をテストする際の比較対象として使えます。

| KLID | 言語 | 既存 IME | 入力例 | 期待出力 |
|---|---|---|---|---|
| `00000411` | ja-JP | MS-IME 日本語 | `ime_type("toukyou")` + `send_key("SPACE")` + `send_key("ENTER")` | `東京` |
| `00000412` | ko-KR | MS-IME 한국어 (2-Beolsik) | `ime_type("dks")` + `send_key("ENTER")` | `안` |
| `00000804` | zh-CN | MS Pinyin | `ime_type("nihao")` + `send_key("SPACE")` | `你好` |
| `00000409` | en-US | (IME なし、layout のみ) | 通常の `type_text` 使用 | — |

お客様の IME も `activate_keyboard_layout(<your KLID>)` で同様に活性化できます。

---

## 6. CI 連携 (GitHub Actions 想定)

**現状の MVP には専用の GitHub Actions custom action はありません** (v2 で予定)。
ただし `Claude Code CLI` を CI runner に install すれば次のような flow が組めます:

```yaml
# .github/workflows/wdac-ime-test.yml (例)
jobs:
  ime-e2e:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: |
          # IME を build (custom)
          ./build.sh -o dist/MyIme.msi
      - run: |
          # WDAC に upload (size 1MB 以下なら base64 経由、超えるなら aws s3 cp)
          aws s3 cp dist/MyIme.msi s3://wdac-staging-customer-installers-<acct>/${{ secrets.WDAC_CUSTOMER_ID }}/${{ github.run_id }}/MyIme.msi
      - run: |
          # Claude Code で MCP CLI 起動 + テスト scenario 実行
          # (現状は ad-hoc script、v1 で wdac-test runner が出る予定)
          ...
```

> **v1 では** `wdac-test` CLI + JUnit-XML reporter を予定しています。
> それまでは ad-hoc な shell script で polling + assertion を書く必要があります。

---

## 7. テスト書き方ベストプラクティス

### ✅ Do

- **session を毎テストで作り直す** — 状態リーク 0、再現性最高。session 作成は ~10 分かかるので、shared workspace と pre-install で固定 setup を吸収
- **`get_ui_tree` で `valueCurrent` を assert** — screenshot 比較より速い、stable
- **`session terminate` を必ず finally で呼ぶ** — EC2 が残ると課金続行
- **IME E2E は `ime_type` で composition 駆動、変換確定は `send_key("SPACE")` + `send_key("ENTER")`** (内部的に keybd_event で IME hook を通る、PR-`b2e68d1` で修正済)
- **session 起動完了は `wait_for_session` で polling** (25 sec cap、内部 retry)、`provisioning_step="Pre-install 実行中"` で install 進行が見える

### ❌ Don't

- **`type_text` で IME composition を駆動しない** — `WM_CHAR` で IME を bypass してしまう、必ず `ime_type` を使う
- **同じ Notepad を複数テストで使い回さない** — text 累積でテスト間 dependency が出る (Known issue #3、test pattern で対処予定)
- **multi-IME を同 session に install しない** — TSF text service が衝突、debug 困難。 1 session 1 IME を keep
- **`requireLogoff: false` を default にしない** — 多くの TSF text service は logoff/logon しないと有効化されない。デフォルト `true` を信頼

---

## 8. アーキテクチャ概要 (debug 用)

```
[お客様 CLI]
   ↓ MCP tool call (HTTPS)
[ControlPlane (Lightsail container)]
   ↓ presigned URL 発行 + dispatch
   ├─ [S3: wdac-staging-customer-installers] (msi 保管、90 day expire)
   └─ [SandboxAgent (EC2 Session 0)]  ← MagicOnion / HTTP
        ↓ proxy
      [DesktopHelper (EC2 Session 1, sandbox-runner)]
        ↓ Process.Start
      [msiexec / 7-Zip / Notepad / TSF text service / ...]
```

**重要な分離**:
- **Session 0** (SYSTEM, NSSM): SandboxAgent、外部通信、SSM agent
- **Session 1** (sandbox-runner): DesktopHelper、IME (ctfmon.exe)、テスト対象 app
- IME の TSF text service は **HKLM\Software\Microsoft\CTF\TIP** に register されるが、ctfmon.exe は per-user で読み込む → install 後 **logoff/logon が必須**

---

## 9. 既知の制約 / トラブルシューティング

| 症状 | 原因 | 対処 |
|---|---|---|
| `upload_installer` で `fileBase64 is not valid base64` | LLM が long base64 を literal で書ききれてない、または改行混入 | 1MB 超なら aws CLI で直接 S3 upload (前掲) |
| `install_ime` で 404 "installer not found" | installer の owner が違う / soft delete された / S3 上に未 upload | `list_installers` で owner と uploadedAt を確認 |
| session が `BuildFailed` で止まる、`ProvisioningStep="ビルド失敗"` | pre_install_packages のいずれかが install fail (msi exit !=0/1641/3010)、`continueOnFailure: false` で打ち切り | session.BuildOutput / BuildError を確認、msi args を再検討、必要なら `continueOnFailure: true` |
| install 後に session が unreachable になる | logoff/logon 中、DH 復活待ち (~20 sec) | 30 sec 後 `connect_session` を retry |
| `activate_keyboard_layout` で `hkl=0x0` | 言語パック未 install (お客様の IME の KLID が AMI に登録されてない) | お客様の IME の install 自体が完了してることを `Test-Path` で確認、KLID は HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layouts で見える |
| `ime_type` 後 IME に何も入らない (literal 文字が Edit に落ちる) | TSF chain に接続されてない → logoff/logon してない、または HKCU の user language list に未登録 | `set_workspace_pre_install_packages` で `requireLogoff: true` を確認 |
| `send_key("SPACE")` で空白が 2 個入る | (旧 bug、2026-06-30 commit `09cb2d1` で修正済) | DH が最新 binary か確認 (`ami-090f53561f6da3617` 以降) |

---

## 10. お問い合わせ / フィードバック

- WDAC リポジトリ (private): https://github.com/dhq_boiler/WinDesktopAppOnCloud
- バグ報告: 該当 session の `session.Id` / install の `installId` / `state.json` の中身を添えて issue 化
- 機能要望: v1 ロードマップ (下記) と照らし合わせて issue 化

---

## 11. ロードマップ (現状 MVP-2 → v1)

| Phase | 内容 | 状態 |
|---|---|---|
| **MVP-2** | msi installer 注入 + auto dispatch + MCP install_ime tool | ✅ **2026-07-01 完成** |
| MVP-3 | scenario runner CLI + JUnit-XML reporter | 未着手 (次フェーズ) |
| v1-1 | assertion tool 群 (`assert_text` / `assert_screenshot` 等) | 未着手 |
| v1-2 | exe installer / dll register / reboot installer 対応 | 未着手 |
| v1-3 | 失敗時 artifact 自動収集 (screenshot + DH log + UI snapshot) | 未着手 |
| v1-4 | multipart upload (1MB 超 msi の MCP 経由 upload) | 未着手 |
| v1-5 | workspace `install_env` (KMS encrypted) で license key 渡し | 未着手 |
| v2 | GitHub Actions custom action、Slack 通知、Tournament テスト | future |

---

## 付録: 一発で動かすサンプル script (PowerShell)

```powershell
# 前提:
#   - .\\MyIme.msi が手元にある (size <= 1MB)
#   - wdac-mcp-staging に login 済
#   - $projectId, $workspaceId が決まってる

# 1. msi を base64 化 (1MB 以下なら MCP 経由 OK)
$bytes = [IO.File]::ReadAllBytes(".\\MyIme.msi")
$base64 = [Convert]::ToBase64String($bytes)

# 2. upload (Claude Code MCP CLI 経由、対話的)
#    > upload_installer(filename: "MyIme.msi", fileBase64: "<貼る>")
#    → installerId をコピー

# 3. workspace 設定
#    > set_workspace_pre_install_packages(
#        projectId, workspaceId,
#        packages: [{ installerId: "...", overrideArgs: "REBOOT=ReallySuppress", requireLogoff: true, continueOnFailure: false }]
#      )

# 4. テスト run (Claude Code が assert 含めて auto)
#    > create_session(projectId, workspaceId)
#    > wait_for_session(sessionId)
#    > connect_session(sessionId)
#    > launch_app("C:\\Windows\\System32\\notepad.exe")
#    > activate_keyboard_layout("<YourKLID>")
#    > set_ime_state(sessionId, enabled: true)
#    > ime_type(sessionId, "<your romaji>")
#    > send_key(sessionId, "SPACE")
#    > send_key(sessionId, "ENTER")
#    > get_ui_tree(sessionId, controlTypes: "Edit")  → valueCurrent を assert
#    > terminate_session(sessionId)
```

---

**Happy IME testing っす 🚀**
