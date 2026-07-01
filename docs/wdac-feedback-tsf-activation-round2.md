# WDAC チームへ — TSF 有効化 Round 2: MSI 側修正済 + activate_tsf_profile / list_tsf_profiles の実装バグ

**差出人**: GenerativeIME チーム (dhq_boiler)
**日付**: 2026-07-01
**対象**: WDAC (WinDesktopAppCloud) ControlPlane / DesktopHelper / MCP 開発チーム
**関連**:
- 前回頂いた `message-to-generativeime-team-2026-07-01.md`（KLID + ActiveSetup 追加を提案）
- 前回 FB `docs/wdac-feedback-tsf-profile-activation.md`

---

## TL;DR

- MSI に **KLID 登録**（HKLM Keyboard Layouts\E0210411）と **ActiveSetup**（StubPath で SeedHkcu.ps1）を追加した **v0.1.2** をビルドし直し、install も成功（installId 完了、exitCode 0）。
- しかし **当 IME は依然として notepad にロードされない**（`get_debug_output(pid=notepad)` 空 = 決定的証拠）。
- 追加で判明した **WDAC 側の実装バグ 2 件**:
  1. **`list_tsf_profiles`** が `langid=0x0411` を指定するとエラーで落ちる。langid 省略時も **ja-JP プロファイルを 1 件も返さない**（langid=0 のシステム 3 件のみ）。→ 内部で `EnumProfiles(langid)` を呼び分けていない疑い。
  2. **`activate_tsf_profile`** が `E_NOINTERFACE` で失敗:
     ```
     Unable to cast COM object ... to interface type 'DesktopHelper.Services.ITfInputProcessorProfileMgr'.
     QueryInterface for {71C6E74F-0F28-46FA-B86C-15A02FE0EEEF} failed: 0x80004002 (E_NOINTERFACE).
     ```
     → `ITfInputProcessorProfiles` を経由してから QI しても `ITfInputProcessorProfileMgr` は取れません（同じ CLSID から別途 `CoCreateInstance(IID_ITfInputProcessorProfileMgr)` を呼ぶ必要あり）。

---

## 1. MSI 側修正の詳細（実装済み・v0.1.2）

### 追加した registry（HKLM）

```
[HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layouts\E0210411]
  "Layout Text"         = "GenerativeIME"
  "Layout File"         = "kbdjpn.dll"
  "Layout Display Name" = "GenerativeIME"
  "IME File"            = "GenerativeIME.Tsf.dll"

[HKLM\Software\Microsoft\Active Setup\Installed Components\{5F3E9A21-4C8B-4D2E-9F1A-7B6C5D4E3F20}]
  (default) = "GenerativeIME per-user TSF seed"
  "Version"  = "1,0,2"
  "StubPath" = powershell.exe -ExecutionPolicy Bypass -WindowStyle Hidden
               -NonInteractive -File "[INSTALLFOLDER]SeedHkcu.ps1"
```

### 追加した SeedHkcu.ps1（payload に同梱、ActiveSetup が呼ぶ）

```powershell
$catId = '{34745C63-B2F0-4784-8B67-5E12C8701A31}'  # GUID_TFCAT_TIP_KEYBOARD
$clsid = '{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}'
$prof  = '{F267F064-7917-4631-BB73-567C314F43BE}'
$klid  = 'E0210411'

# HKCU\CTF\Assemblies\0x00000411\{catId}
$asm = "HKCU:\Software\Microsoft\CTF\Assemblies\0x00000411\$catId"
New-Item -Force -Path $asm | Out-Null
Set-ItemProperty $asm 'Default'        $prof
Set-ItemProperty $asm 'Profile'        $clsid
Set-ItemProperty $asm 'KeyboardLayout' 0x04110411 -Type DWord

# HKCU\Keyboard Layout\Preload に substitute KLID を append
# HKCU\CTF\Substitutes 04110411 -> E0210411
```

（新 sha256: `4c98d9d32b378ff42be8fd4b98c1c47c9a4aa36432ad16e1d1dc14e06835763f`, size: 40173568。既に `confirm_uploaded_installer` で S3 と DB を再確定済み）

## 2. 症状（v0.1.2 install 後）

```
install_ime → completed, exitCode=0                     ← MSI install 成功
list_tsf_profiles(langid=0x0411) → 500 error            ← WDAC 実装バグ #1
list_tsf_profiles (langid 省略) → [Touch Input Correction ほか langid=0 の 3 件のみ]
                                                         ← ja-JP プロファイル 0 件（当 IME も MS-IME も見えない）
activate_tsf_profile(clsid, profGuid, 0x0411) → 500 E_NOINTERFACE  ← WDAC 実装バグ #2
                                                         (QI IID = ITfInputProcessorProfileMgr)
ime_type("toukyou") → SPACE → 変換自体は動く（東京）      ← ただし MS-IME による
get_debug_output(pid=notepad) → []                       ← [GenerativeIME] ログ 0 件
                                                         = 当 IME DLL は notepad に非ロード
```

**決定的**: `[GenerativeIME]` プレフィックスの `OutputDebugString` が 1 件も出ない = 当 IME は Activate されていない。

## 3. WDAC 側実装バグ 2 件（優先度: 高）

### バグ 2-1: `list_tsf_profiles(langid=<非0>)` が 500

- 現在: langid 引数を受けても内部で使っていない or `EnumProfiles(0, ...)` 固定
- 期待: `ITfInputProcessorProfiles::EnumLanguageProfiles(langid, ppEnum)` を呼び、その enumerator を回して返す
- 補足: langid を指定しない場合の結果に langid=0 のみ 3 件、というのは `IEnumTfLanguageProfiles` ではなく別 API（`EnumProfiles`）を叩いている可能性

### バグ 2-2: `activate_tsf_profile` の QI で E_NOINTERFACE

- 現状のメッセージから、`CLSID_TF_InputProcessorProfiles` を CoCreateInstance で得たオブジェクトから `ITfInputProcessorProfileMgr` を QI しているように読める。
- Windows SDK 上、`CLSID_TF_InputProcessorProfiles` は **`ITfInputProcessorProfileMgr` を実装する**（少なくとも Win10+）はずなので、この失敗は「.NET 側 RCW の QI で C++ の CO_E_INTF_NOT_SUPPORTED を触っている」もしくは「引数の IID GUID の綴りミス」を疑います。commit `a7f3b60` の実装を、以下のいずれかで確認してほしいです:
  - 直接 `CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, out mgr)` で受け取れているか（分けて CoCreate してから QI ではなく、直接その IID で受ける）
  - .NET なら `Marshal.GetTypeFromCLSID` + `Activator.CreateInstance` 経由ではなく、Ole32 の `CoCreateInstance` P/Invoke で直接 IID を渡す
  - あるいは Windows SDK の `Msctf.h` の `IID_ITfInputProcessorProfileMgr` の値を再確認（`71C6E74F-0F28-46FA-B86C-15A02FE0EEEF` は正しいはず）

これらが直れば、少なくとも `activate_tsf_profile` が「成功と返す」ところまでは前進します。ただし前回の Round 1 での Step 4 の指摘（成功と返しても実 activation が起きない）は残るので、以下 §4 の観察が更に重要になります。

## 4. MSI 側で更に何が足りない可能性（相談）

WDAC チームの前回報告で、Step 4 が「API 成功でも DLL ロードなし」だった実験結果を頂いています。それを踏まえて、私の MSI 側の Active Setup が **本当に走ったのか / 走った結果 HKCU seed が正しく入ったのか** を、以下のいずれかで確認する方法があると助かります:

- (a) **install 後の任意タイミングで、`reg query HKCU\Software\Microsoft\CTF\Assemblies\0x00000411 /s` 相当を返す `run_powershell(sessionId, script)` 系のツール**（診断用）
- (b) `list_tsf_profiles` バグ 2-1 が直れば、当 TIP が enumerate される時点で HKLM 登録は生きていると確認可
- (c) 前回頂いた「Notepad のプロセス Modules に GenerativeIME.Tsf.dll があるか」を SSM で確認する診断: これを MCP ツール化してほしい（`list_process_modules(sessionId, pid)`）

こちらでも並行して以下を試します:
- `SeedHkcu.ps1` の内容を、install 直後にも `regsvr32` の代わりに `CustomAction`（Deferred, Impersonate=no ではなく Impersonate=yes / no を切り替え）で試す → ActiveSetup を待たずに現ユーザーへ seed 可否
- ActiveSetup の Version を上げる（1,0,3 に）→ 次 logon で再実行を強制
- **launcher exe（代替 B）路線**: `.NET` の小さな init exe で `InstallLayoutOrTip` + `AddLanguageProfile` を叩く

## 5. 添付情報（実値・変更なし）

| 項目 | 値 |
|---|---|
| CLSID | `{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}` |
| Profile GUID | `{F267F064-7917-4631-BB73-567C314F43BE}` |
| LANGID | `0x0411` (ja-JP) |
| Substitute KLID | `E0210411` |
| installerId | `8ff21210-62e5-4f94-b5bb-3676750adf61` |
| v0.1.2 sha256 | `4c98d9d32b378ff42be8fd4b98c1c47c9a4aa36432ad16e1d1dc14e06835763f` |
| v0.1.2 size | 40173568 |
| MSI version | 0.1.2 |

## 6. まとめ / お願い

1. **バグ 2-1 (list_tsf_profiles)** と **バグ 2-2 (activate_tsf_profile QI)** の修正を優先していただけると助かります（少なくとも API 呼び出しが返せる状態になる）。
2. install 直後の HKCU seed / KLID 反映を診断する手段が欲しいです（§4 (a) の `run_powershell` 相当、または (c) の `list_process_modules`）。
3. こちら側は launcher exe 路線 / ActiveSetup Version bump / CustomAction 経由 seed を試して報告します。

引き続きよろしくお願いします 🙏
