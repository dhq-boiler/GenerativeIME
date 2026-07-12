# scripts/loop/misconversion — 誤変換ログ定期スキャンループ

`%APPDATA%\GenerativeIME\misconversions.log` (ユーザーが Ctrl+F5 で記録する
実誤変換) を定期的にスキャン・判定・整理する。corpus 回帰ループとは別系統。

## 何が違うか

- corpus 回帰: 事前定義の (reading → expected) を top 候補と突き合わせる **クローズド判定**
- misconversion: 「間違って確定された文字列」から「本来入力したかったもの」を **オープンエンド推測**

そのため e4b の 2 択判定は向かず、Haiku 単独で JSON 返答させる。

## 3-Phase パイプライン

```
scan.ps1  → dedupe → misconversion_scan.json
              ↓
judge.ps1 → Haiku で intended/category/confidence → misconversion_judge.json
              ↓
run.ps1   → 高信頼のみ action plan 化 → misconversion_action_plan.md
```

## 使い方

### 差分スキャン + judge + plan
```powershell
.\scripts\loop\misconversion\run.ps1
```
- `misconversion_scan.json` の `last_processed_timestamp` より新しい entry のみ処理
- 新規ゼロなら 0 コスト即終了

### 全件再処理（既存判定を全部やり直す）
```powershell
.\scripts\loop\misconversion\run.ps1 -All
```

### スキャンだけ（Haiku コスト回避）
```powershell
.\scripts\loop\misconversion\run.ps1 -SkipJudge
```

## 出力

`.claude/state/misconversion_action_plan.md` に:

- **高信頼 (high confidence)**: 具体的な追加コード / 辞書エントリを提示
- **中/低信頼**: 人間 review 用に理由と併記
- **判定失敗**: JSON parse 不能なもの

**自動でファイルは書き換えない**。plan を人が読み、承認したものだけ手で反映。

## 判定カテゴリ

| category | 対応 |
|---|---|
| `reading_top` | `modernranking.cpp` の `kOverrideTable[]` 追加候補（corpus ループと同じ枠） |
| `user_dict` | `%APPDATA%\GenerativeIME\dict\user_additions.utf8` に SKK 行追加 |
| `bunsetsu` | 読みを丸ごとユーザー辞書登録、または `bunsetsu.cpp` head-priority override |
| `complex` | 人間判断要（deferred） |

## 定期実行 (Windows Task Scheduler)

例: 毎日 09:00 に自動スキャン。

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument '-NoProfile -File "C:\Git\GenerativeIME\scripts\loop\misconversion\run.ps1"'
$trigger = New-ScheduledTaskTrigger -Daily -At '09:00'
Register-ScheduledTask -TaskName 'GenerativeIME-MisconversionScan' `
    -Action $action -Trigger $trigger -Description 'Scan and judge new misconversion log entries'
```

登録後の状態は `Get-ScheduledTask GenerativeIME-MisconversionScan` で確認、
手動実行は `Start-ScheduledTask -TaskName GenerativeIME-MisconversionScan`。

## 判定精度メモ（2026-07-12 実測）

5 件の初回実測:

- **候補窓の `candidates` フィールドを prompt に含めると顕著に精度向上**。
  `唖ました` → `オシマシタ` の隣接候補ヒントから `押しました` に到達できるようになった。
- **`context` は末尾 40 字だけ渡す**。Ctrl+F5 連打でノイズ蓄積するため。
- Haiku は自信が無いとき素直に `confidence: low` を返す。この閾値で
  自動アクションから除外する運用が実用的（低信頼 = 人間確認）。
- コスト実測: 5 件で $0.16、`chinggis` の逆読み特定など難ケースは失敗するが
  失敗を low confidence でフラグできるので実害小さい。

## 何をしないか

- **自動でファイル書き換え・辞書更新をしない**。plan を出すだけ。
- **corpus 回帰ループとは干渉しない**。両者は独立して回せる。
- **重複エントリは自動 dedupe** (`lastCommitted` group)。8 回押された
  `付いか下実装` は 1 件として扱う。
