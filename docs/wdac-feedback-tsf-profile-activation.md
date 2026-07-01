# WDAC チームへ — カスタム TSF TIP を GUID で有効化する手段の要望

**差出人**: GenerativeIME チーム (dhq_boiler)
**日付**: 2026-07-01
**対象**: WDAC (WinDesktopAppOnCloud) ControlPlane / SandboxAgent / MCP 開発チーム
**深刻度**: High（**カスタム TSF IME の E2E が「最後の1手」で完了不能**。IME ベンダー向け WDAC の中核ユースケース）
**前提**: 先の sha256 ギャップ対応（`prepare_large_installer` / `confirm_uploaded_installer`）で大容量 msi の注入は完全に動くようになりました 🙏。今回はその次に当たった壁です。

---

## TL;DR

- 当 IME（GenerativeIME, TSF テキストサービス）の **install / 登録は完全成功**（`install_ime` exitCode 0、regsvr32→DllRegisterServer で COM サーバ・TSF プロファイル・カテゴリ登録 OK）。
- しかし **当 TIP を「いま開いているアプリで有効な入力方式」にする手段が無い**。`activate_keyboard_layout("00000411")` は ja-JP の **HKL(KLID)** を載せるだけで、その言語の**既定 TIP（プリインストールの MS-IME）**が選ばれたまま。GUID 指定で特定の TSF TIP を選ぶ術がない。
- 結果、`ime_type` で打っても **MS-IME が変換**してしまい、当 IME の挙動（Trigger 群・候補ウィンドウ・学習など）を E2E で検証できない。
- **要望**: セッション内で TSF プロファイルを CLSID/プロファイル GUID 指定で有効化する MCP ツール `activate_tsf_profile(...)`（+ 補助で `list_tsf_profiles` / `enable_tsf_profile`）。

---

## 1. 確証（なぜ「当 TIP が非アクティブ」と断定できるか）

当 IME はアクティブ化時に大量の `OutputDebugStringW(L"[GenerativeIME] ...")` を出します（例: `Activate`, `CandidateWindow.Create`, `dict warmup complete`）。

```
1. install_ime(installerId) → get_install_status: completed, exitCode 0   ← 登録成功
2. launch_app(notepad) → activate_keyboard_layout("00000411") → set_ime_state(true)
3. ime_type("toukyou") → send_key("SPACE") → get_ui_tree: valueCurrent="東京"  ← 変換は出来る
4. ただし list_windows に当 IME の候補ウィンドウ class "GenerativeIME_CandWnd_v2" が出ない
   （候補は TextInputHost=MS-IME 側に描画）
5. get_debug_output(pid=<notepad>) → []   ← 当 IME DLL が notepad に一切ロードされていない
```

→ 4 と 5 から、手順 3 の「東京」は **MS-IME による変換**。当 IME は登録済みだが **notepad の入力方式として有効化されていない**。

## 2. 試したが効かなかった回避策

- `activate_keyboard_layout("00000411")`: HKL を載せるだけ。既定 TIP（MS-IME）のまま。
- `send_key_combo(win+SPACE)` を複数回: 入力方式の循環を試すも、`get_debug_output(pid=notepad)` は終始 空 → 当 TIP には一度も切り替わらず（そもそもユーザーの入力方式リストに入っていない疑い）。
- インストーラ内 `ITfInputProcessorProfiles::SetDefaultLanguageProfile(0x0411, clsid, profileGuid)` を追加（**機械既定**の変更）: 既存ユーザー（sandbox-runner）の**ユーザー別入力方式リスト/既定**は上書きされず、有効化されなかった。
  - 根本原因: TSF の有効 TIP は **per-user**（`HKCU\...\CTF\Assemblies`, 入力方式リスト）。SYSTEM 実行の msiexec カスタムアクションからは、既存ユーザーの HKCU を正しく書けない。`activate_keyboard_layout` も KLID 前提で TSF TIP を指せない。

## 3. 要望（IME ベンダー E2E の中核機能）

セッション内（sandbox-runner のユーザーコンテキスト）で、TSF プロファイルを GUID 指定で**有効化**する MCP ツール:

```
activate_tsf_profile(
    sessionId,
    clsid:       "{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}",   # TextService CLSID
    profileGuid: "{F267F064-7917-4631-BB73-567C314F43BE}",   # AddLanguageProfile の profile GUID
    langid:      "0x0411"                                     # ja-JP
) -> { ok, activatedClsid, activatedProfileGuid }
```

実装の方向性（DesktopHelper 内、sandbox-runner セッションで実行）:
- `ITfInputProcessorProfileMgr::ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, langid, clsid, profileGuid, NULL, TF_IPPMF_FORSESSION)`、または
- `ITfInputProcessorProfiles::ChangeCurrentLanguage(langid)` + `ActivateLanguageProfile(clsid, langid, profileGuid)`。
- 必要なら事前に `EnableLanguageProfile(clsid, langid, profileGuid, TRUE)` でユーザーの入力方式リストに追加。

補助ツールがあると便利:
- `list_tsf_profiles(sessionId, langid?)` → 登録済み TIP 一覧（clsid, profileGuid, description, isActive）。当 TIP が登録されているか・現在どれが active かを検証できる。
- `enable_tsf_profile(sessionId, clsid, profileGuid, langid)` → ユーザーの入力方式リストへ追加のみ。

これは **WDAC が IME ベンダー向けに存在する以上、最重要級の機能**です（onboarding doc は「お客様の IME も activate_keyboard_layout(<your KLID>) で活性化」と書いていますが、**TSF テキストサービスは KLID を持たない**ため、現状その記述では当 IME を有効化できません）。doc の §5 もあわせて修正をご検討ください。

## 4. 補足（副次・別件で可）

- onboarding doc §5: 「お客様の IME も activate_keyboard_layout(<your KLID>) で活性化できます」は **KLID ベースの IMM32 IME 前提**。TSF TIP（CLSID+プロファイル GUID、KLID なし）には当てはまらないので、TSF 用の活性化手順（上記ツール）を追記してほしい。

## 5. こちらで検証済み（WDAC の install 系は良好）

- `prepare_large_installer` → `aws s3 cp`(39MB) → `confirm_uploaded_installer`(real sha256) → `install_ime` → completed/exitCode 0。**sha256 対応は完璧に機能**。
- 当 IME の登録（COM/TSF プロファイル/カテゴリ）も regsvr32 経由で成功。
- **`activate_tsf_profile` さえ入れば、当 IME での完全自動 E2E（toukyou→東京 の主体が当 IME であることを [GenerativeIME] デバッグログで裏取り、さらに Trigger 群の実機検証）まで即進めます。**

## 6. 添付情報（実値）

| 項目 | 値 |
|---|---|
| TextService CLSID | `{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}` |
| Profile GUID | `{F267F064-7917-4631-BB73-567C314F43BE}` |
| LANGID | `0x0411`（ja-JP） |
| 表示名 (description) | `GenerativeIME` |
| 候補ウィンドウ class | `GenerativeIME_CandWnd_v2` |
| デバッグ出力プレフィックス | `[GenerativeIME]`（`get_debug_output` で識別可能） |
| installerId | `8ff21210-62e5-4f94-b5bb-3676750adf61`（登録・確定済み） |

以上です。引き続きよろしくお願いします 🙏
