# WDAC 障害報告: SandboxAgent が全ての desktop 操作を 401 Unauthorized で拒否

**報告者**: GenerativeIME 側 (dhq_boiler)
**発生日時**: 2026-07-11 (JST 22:37 ～ 23:03 頃 / UTC 13:37 ～ 14:03)
**環境**: WDAC staging (ControlPlane 経由 MCP)
**優先度**: High — E2E テストが完全ブロック
**関連改修**: `docs/wdac-exe-installer-spec.md` の EXE installer 対応リリース直後

## 1. サマリ

`install_ime` の EXE 対応が入ったことを確認後、当該改修を含む新 EXE インストーラ (`GenerativeImeInstaller.exe`, 123 MB) を投入して E2E を回そうとしたところ、**ControlPlane 系 API は全て成功する**が、**SandboxAgent 経由の全 desktop 操作が 401 Unauthorized** を返し、`launch_app` すら通らない状態になった。EC2 インスタンスは Running / connect_session は success を返すが、その先の全 API が `session/init failed (401): Unauthorized` (statusCode 401, retryable: false)。

同じ現象が **異なる EC2 インスタンス上の別セッション** で再現したため、EC2 固有の不具合ではなく、認証パス側 (tenant binding / JWT 検証) の問題と推定。

## 2. 影響

- 当 IME (`GenerativeIME.Tsf.dll`) の E2E テストが完全に不可能。
- `install_ime` の EXE 対応の動作検証も不可能 (launch 前段で刺さる)。
- 過去 (2026-07-01) の v0.1.3 MSI で成功していたフロー ([[wdac-tsf-activation-gap]] に記録) は当時 **dhq_boiler** アカウントで動作。今回は **takashi.honma@luxiar.com** アカウントで発生。

## 3. 再現手順 (今回実際に踏んだステップ)

```
# 1. 認証
mcp__wdac-mcp-staging__login(code="SNTQ-ZLKV")
  → success=true, email="takashi.honma@luxiar.com"

# 2. installer 登録 → 成功
prepare_large_installer(filename="GenerativeImeInstaller.exe",
                        defaultArgs="--silent --aggressive")
  → installerId=47ee9263-9bf4-4df4-9b72-e0c9be344427
  → s3Key=6b342a4ae7514ef585c3cc81632212d5/47ee92639bf44df49b72e0c9be344427/GenerativeImeInstaller.exe

aws s3 cp installer/build/GenerativeImeInstaller.exe \
  s3://wdac-staging-customer-installers-218797010517/<s3Key>
  → upload 完了 (117.7 MiB)

confirm_uploaded_installer(installerId=47ee9263-...,
                           sha256=9c49b7855fb6a824bcdff921c84403a895c6427c3db8526f68e47c1b53752419,
                           sizeBytes=123423203)
  → uploadedAt=2026-07-11T13:47:36Z, reconfirmed=false

# 3. セッション作成 → Running
create_session(projectId=92bcb7a6-...(WpfDemo),
               workspaceId=3cd22566-...(WpfDemo))
  → sessionId=77072f38-06a6-4292-aee8-4bca0bff00e0
wait_for_session → Running (数分後)

# 4. connect → 成功
connect_session(sessionId=77072f38-...)
  → sandboxBaseUrl=http://ec2-35-77-199-189.ap-northeast-1.compute.amazonaws.com:5196
  → success=true, "Connected. You can now use launch_app, screenshot, click..."

# 5. launch_app → 401 ★問題★
launch_app(sessionId=77072f38-..., executablePath="C:\\Windows\\System32\\notepad.exe")
  → {"success":false,"tool":"launch_app",
     "error":"session/init failed (401): Unauthorized",
     "statusCode":401,"retryable":false}

# 6. 他の desktop 系 API も同様
list_windows(sessionId=77072f38-...)
  → {"success":false,"tool":"list_windows",
     "error":"windows failed (401): Unauthorized",
     "statusCode":401,"retryable":false}

# 7. 対症: 再 connect, 再 login → いずれも launch_app は 401 のまま
connect_session(...) → success=true (何度呼んでも)
login(code="...") → success=true
launch_app → still 401

# 8. セッション破棄 → 別 EC2 で再作成 → 同じ 401
terminate_session(77072f38-...) → success
create_session → sessionId=94498044-5844-4231-ace8-68e34f909a3c
wait_for_session → Running
connect_session → sandboxBaseUrl=http://ec2-18-183-68-38.ap-northeast-1.compute.amazonaws.com:5196 (別 EC2)
launch_app → 同じく 401
terminate_session(94498044-...) → success
```

## 4. 観察事実

| 事実 | 意味 |
|---|---|
| `login`, `list_projects`, `list_workspaces`, `list_installers` は全て 200 | ControlPlane 側の認証は通っている |
| `prepare_large_installer` が `s3Key` に含める customerId は `6b342a4ae7514ef585c3cc81632212d5` (dhq_boiler の customerId と一致) | takashi.honma と dhq_boiler は同じ customer tenant に属する |
| `create_session`, `wait_for_session`, `get_session`, `terminate_session` は 200 | ControlPlane 側のセッション制御も通っている |
| `connect_session` は 200 かつ `sandboxBaseUrl` を返す | ControlPlane が sandbox 座標を返す層は OK |
| `launch_app`, `list_windows`, etc. の HTTP は 401 | **SandboxAgent の HTTP handler が持つ auth middleware が今回のトークンを拒否している** |
| 別 EC2 でも 401 | EC2 個別ではなく認証パス側の問題 |
| メッセージ `session/init failed (401)` は launch_app 初回。以降の呼び出しは各 API 名で 401 | SandboxAgent が bearer をチェックしてから session/init を走らせる想定 |

## 5. 想定原因 (WDAC 側で確認していただきたい仮説)

### 仮説 A: SandboxAgent の tenant binding が改修で厳しくなり、takashi.honma のトークンを弾いている

- launch_app の docstring に "The agent binds to the first tenant and rejects others." とあり、tenant scope チェックはある。
- 当セッションの ControlPlane 側 tenant は customerId `6b342a4ae7...` (両アカウント共有)。SandboxAgent 側の tenant 判定が customerId ベースなら通るはず、userId ベースなら別 user は弾かれる。
- EXE 対応改修時に SandboxAgent の auth middleware にも手が入っていないか要確認。

### 仮説 B: SandboxAgent が期待する JWT の claim 構造 (aud / scope / roles) が ControlPlane 側の変更と unsync

- MCP クライアントがブラウザ認証で受け取った JWT を SandboxAgent が受け取り、`aud=sandbox` / `scope=desktop:*` などを検証している場合、片側だけ更新されると全リクエストが 401 になる。

### 仮説 C: takashi.honma@luxiar.com には ControlPlane の read/write 権限のみあり、SandboxAgent の実行権限がロールとして未付与

- IAM ロール構成として ControlPlane 権限と SandboxAgent 権限を別 role で持つ設計なら、片方だけ付与のケースは起こりうる。

## 6. 確認 / 対応いただきたいこと

1. **SandboxAgent の 401 応答時ログ**: 対象 EC2 instance (`ec2-35-77-199-189` および `ec2-18-183-68-38`, ap-northeast-1) の 2026-07-11 13:47〜14:03 UTC のログ。特に auth middleware で reject された理由 (JWT expired / audience mismatch / role mismatch / tenant mismatch のいずれか)。
2. **`takashi.honma@luxiar.com` の権限確認**: WpfDemo プロジェクト (`92bcb7a6-e3b6-4422-b778-e72965301cd1`) 配下のセッションで desktop 操作を実行できる権限があるか。
3. **改修差分の確認**: EXE 対応の改修 (recent) で SandboxAgent 側の HTTP handler / auth 周りに変更があったか。あれば diff を共有いただきたい。
4. **`dhq_boiler` (customerId `6b342a4ae7...`) の auth code を新規発行**: この経路で試行して 401 が消えれば、原因はほぼ確実に user-scope。消えなければ SandboxAgent 全体の regression。
5. **回避策**: どちらのアカウントでも通らない場合、staging へ緊急 rollback を検討していただきたい。

## 7. こちら側の状態 (Follow-up 用)

- installer は S3 に配置済、`installerId=47ee9263-9bf4-4df4-9b72-e0c9be344427`, sha256=`9c49b7855fb6a824bcdff921c84403a895c6427c3db8526f68e47c1b53752419`
- 401 が解消次第、以下のフローで E2E を再開する予定:
  1. `create_session` (WpfDemo)
  2. `install_ime(installerId=47ee9263-..., overrideArgs="--silent --aggressive")`
  3. `activate_tsf_profile(clsid=D256C881-..., profileGuid=F267F064-..., langid=0x0411)`
  4. `launch_app` → notepad で長文入力 E2E
  5. Edge で `https://dhq-boiler.github.io/GenerativeIME/e2e/caret-test.html` を開き、括弧ペア確定後の caret 位置を assert (Cat P-1 / P-5)
  6. `terminate_session`

## 8. 参考

- 過去の成功パターン: [[wdac-tsf-activation-gap]] (2026-07-01, dhq_boiler, v0.1.3 MSI)
- EXE 対応仕様書: `docs/wdac-exe-installer-spec.md`
- Caret 観測テストページ: https://dhq-boiler.github.io/GenerativeIME/e2e/caret-test.html
- E2E テストマトリクス: `docs/e2e-conversion-test-matrix-2026-07-02.md`
