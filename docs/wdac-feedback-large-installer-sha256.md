# WDAC チームへ — >1MB installer の sha256 検証ギャップ報告 & 機能要望

**差出人**: GenerativeIME チーム (dhq_boiler)
**日付**: 2026-07-01
**対象**: WDAC (WinDesktopAppOnCloud) ControlPlane / MCP server 開発チーム
**深刻度**: High（MVP-2 の「>1MB installer」導線が実質使用不可）
**関連ドキュメント**: `docs/ime-team-onboarding.md` §4-2 の 🚨 注意、§9 トラブルシューティング、§11 ロードマップ v1-4

---

## 1. 要約 (3行)

- `upload_installer` は base64 本体（実質 **1MB 上限**）から sha256 を計算し DB に記録する。
- onboarding doc は「1MB 超は **`aws s3 cp` で S3 直アップロード**」を回避策として案内している。
- しかし `install_ime` はダウンロードした S3 オブジェクトを **DB の sha256 と照合**するため、回避策で差し替えた本物 msi は **必ず `sha256 mismatch` で install 失敗**する。**両者が整合しておらず、>1MB installer を入れる手段が現状存在しない。**

---

## 2. 再現手順（実際に踏んだ手順）

GenerativeIME（TSF IME、msi **39MB**＝unidic-lite 辞書 249MB を cab 圧縮）を WDAC で E2E しようとした際に発覚。

```
1. upload_installer(filename:"GenerativeIME.msi", fileBase64:<25バイトのプレースホルダ>)
   → installerId=8ff21210-62e5-4f94-b5bb-3676750adf61
     sha256(DB)=3ea59a2987ae1f81e54bd713c5585b06e061714fcb0e5391ffc4df8b51c14bf6  ← プレースホルダのハッシュ
     S3 key = 6b342a4ae7514ef585c3cc81632212d5/8ff2121062e54f94b5bb3676750adf61/GenerativeIME.msi

2. aws s3 cp GenerativeIME.msi(本物39MB) s3://wdac-staging-customer-installers-218797010517/<上記key>
   → S3 上は本物 39MB に置き換わる（real sha256=b33bf1b3388abd22b67350866f961cbedef2e2961bdadcb66a80afcd7a574f66）
     ※ DB の sha256 はプレースホルダのまま（更新する術がない）

3. create_session(WpfDemo) → Running → connect_session
4. launch_app(notepad)   ← これを先に呼ばないと install_ime が
                            "DesktopHelper is unreachable (localhost:5197)" になる（後述の副次バグ）
5. install_ime(sessionId, installerId=8ff21210...)
   → get_install_status: status=failed,
     error="download failed after 2 attempts: sha256 mismatch"   ← ここで詰む
```

---

## 3. 根本原因

`upload_installer` が記録する sha256 は **「アップロードした base64 本体」のハッシュ**であり、後から `aws s3 cp` で S3 オブジェクトを差し替えても **DB 側の sha256 は更新されない**。
`install_ime`（DesktopHelper のダウンロード処理）は presigned URL で取得したファイルを **DB の sha256 と照合**して不一致なら 2 回リトライ後に失敗させる。

回避策（aws s3 cp）が前提とする「S3 のオブジェクトを正とする」考え方と、install 側の「DB の sha256 を正とする」検証が**矛盾**している。
DB の sha256 を本物の値へ更新する MCP ツール／エンドポイントが存在しない（`update_installer` 系なし。`upload_installer` の確定ハッシュは本体由来のみ）。

---

## 4. 影響

- **辞書同梱型 IME（本件）や大きめの msi は WDAC に一切載せられない。** GenerativeIME は unidic-lite を同梱するため最小でも数十 MB。
- onboarding doc §4-2 の回避策は**現状機能しない**（ドキュメントと実装の不整合）。
- §11 ロードマップ v1-4「multipart upload（1MB超 msi の MCP 経由 upload）」が来るまで、大容量 IME の E2E は不可能。

---

## 5. 要望（いずれか1つで解決。優先順）

### A. 【最優先・最小工数】既アップロード済みオブジェクトを sha256 指定で登録する導線

`aws s3 cp` 済みの前提で、DB 行を後追い確定／更新できる MCP ツール or ControlPlane API を追加してほしい。例:

```
register_uploaded_installer(
    filename: "GenerativeIME.msi",
    s3Key:    "<customerId>/<installerId>/GenerativeIME.msi",   # or installerId だけでも可
    sha256:   "b33bf1b3388abd22b67350866f961cbedef2e2961bdadcb66a80afcd7a574f66",
    sizeBytes: 40169472,
    defaultArgs: "REBOOT=ReallySuppress"
) -> { installerId }
```

あるいは既存の installerId に対する確定/更新 API:

```
confirm_installer(installerId, sha256, sizeBytes)   # /confirm を顧客が直接叩けるように
```

→ これがあれば「mint(upload_installerで小さく) → aws s3 cp で本物 → confirm_installer(real sha256)」で完結する。

### B. 大容量 upload 経路（multipart / presigned 中継）

`upload_installer` が presigned PUT URL を**返すだけ**のモード、または multipart 中継（v1-4）。顧客が aws CLI で直接 PUT し、サーバが PUT 後に S3 から sha256 を再計算して DB を確定する。**「S3 を正」とするなら install 時の照合もアップロード確定時の S3 実体ハッシュに合わせる**のが筋。

### C. 【非推奨】install 時 sha256 検証のスキップ
セキュリティ的に弱くなるため A/B を推奨。どうしてもなら `install_ime(skipIntegrityCheck:true)` のような明示オプトインに留める。

---

## 6. 副次的に気づいた点（参考・別件で良い）

- **install_ime 前に launch_app が必須**: connect 直後に install_ime を呼ぶと
  `DispatchInstaller failed (BadGateway): DesktopHelper is unreachable ... (localhost:5197)`。
  launch_app で Session 1 のデスクトップを起こしてからだと成功（status=accepted）。
  → install_ime 側で DesktopHelper の起動待ち/自動起動を入れるか、ドキュメントに「install_ime の前に launch_app（または明示の起動API）」を明記してほしい。
- **リポジトリ無し/ビルド無しの workspace は create_session が BuildFailed**
  （"Enclave build did not produce an artifact"）。Notepad だけ使う smoke test 用に
  「ビルドをスキップして起動だけ」する workspace モードがあると、IME install テストの土台が軽くなる。

---

## 7. こちら側で検証済みの事実（WDAC ハーネス自体は良好）

- ハーネス全経路は MS-IME(00000411) で動作確認済み:
  `login → create_session → connect → launch_app(notepad) → activate_keyboard_layout("00000411")
   → set_ime_state(true) → ime_type("toukyou") → send_key("SPACE") → send_key("ENTER")
   → get_ui_tree` で `valueCurrent=="東京"` を assert 成功。
- GenerativeIME 側の準備は完了済み:
  - Release x64 DLL（mecab.lib リンク・辞書ステージを Release 構成に追加して修正）
  - VC++ ランタイム3種（vcruntime140 / vcruntime140_1 / msvcp140）同梱
  - WiX v5 で msi 作成（regsvr32 による TSF 登録カスタムアクション、perMachine、261MB→39MB）
  - installerId 登録済み・S3 正キーへ 39MB 配置済み
- **上記 §5-A の導線さえ用意されれば、こちらは即座に install_ime → 当 IME での E2E（toukyou→東京 等）に進めます。**

---

## 8. 添付情報（再現/対応に必要な実値）

| 項目 | 値 |
|---|---|
| customerId | `6b342a4ae7514ef585c3cc81632212d5` |
| installerId | `8ff21210-62e5-4f94-b5bb-3676750adf61` |
| S3 bucket | `wdac-staging-customer-installers-218797010517` |
| S3 key | `6b342a4ae7514ef585c3cc81632212d5/8ff2121062e54f94b5bb3676750adf61/GenerativeIME.msi` |
| 本物 sha256 | `b33bf1b3388abd22b67350866f961cbedef2e2961bdadcb66a80afcd7a574f66` |
| 本物 sizeBytes | `40169472` |
| DB に記録された誤 sha256 | `3ea59a2987ae1f81e54bd713c5585b06e061714fcb0e5391ffc4df8b51c14bf6`（25バイトのプレースホルダ） |
| 失敗メッセージ | `download failed after 2 attempts: sha256 mismatch` |

以上です。よろしくお願いします 🙏
