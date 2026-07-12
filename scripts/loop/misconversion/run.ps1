# run.ps1 — misconversion 定期ループの 1 イテレーション
#
# scan → judge → 高信頼のみアクションプラン化 → 中信頼以下は人間 review
# へ振り分け。「定期」実行時はこれ 1 本を Task Scheduler / cron から呼ぶ。
#
# 「終わるまで一気に自動修正」しない:
#   - user_dict 追加は %APPDATA% に書き込む破壊的変更なので、apply コマンド
#     を出力するだけで自動実行しない。人が承認してから走らせる。
#   - bunsetsu / reading_top は modernranking.cpp や bunsetsu.cpp を触るので
#     必ずコード diff を人が読む。
#
# 判定失敗の 2 件パターン（context 汚染 or 情報不足）は confidence:medium 以下で
# 出るので、自動アクションから除外される。
#
# Usage:
#   .\scripts\loop\misconversion\run.ps1                # 差分スキャン + judge
#   .\scripts\loop\misconversion\run.ps1 -All           # 全件再スキャン
#   .\scripts\loop\misconversion\run.ps1 -SkipJudge     # スキャンだけ
#   .\scripts\loop\misconversion\run.ps1 -SkipScan      # 既存 scan の judge のみ

param(
    [switch]$All,
    [switch]$SkipScan,
    [switch]$SkipJudge,
    [string]$Model = 'haiku'
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$here      = $PSScriptRoot
$stateDir  = Join-Path $root '.claude\state'
$planFile  = Join-Path $stateDir 'misconversion_action_plan.md'

# ---- 1. Scan --------------------------------------------------------

if (-not $SkipScan) {
    Write-Host '=== Phase 1: Scan ===' -ForegroundColor Cyan
    if ($All) {
        & (Join-Path $here 'scan.ps1') -All
    } else {
        & (Join-Path $here 'scan.ps1')
    }
    if ($LASTEXITCODE -ne 0) { throw 'scan failed' }
}

$scanFile = Join-Path $stateDir 'misconversion_scan.json'
if (-not (Test-Path $scanFile)) {
    Write-Host '[run] no scan output — nothing to do' -ForegroundColor Yellow
    return
}
$scan = Get-Content $scanFile -Raw -Encoding UTF8 | ConvertFrom-Json
if (@($scan.misconversions).Count -eq 0) {
    Write-Host '[run] no unique misconversions' -ForegroundColor Green
    return
}

# ---- 2. Judge -------------------------------------------------------

if (-not $SkipJudge) {
    Write-Host ''
    Write-Host '=== Phase 2: Judge ===' -ForegroundColor Cyan
    & (Join-Path $here 'judge.ps1') -Model $Model
    if ($LASTEXITCODE -ne 0) { throw 'judge failed' }
}

$judgeFile = Join-Path $stateDir 'misconversion_judge.json'
if (-not (Test-Path $judgeFile)) {
    Write-Host '[run] no judge output' -ForegroundColor Yellow
    return
}
$j = Get-Content $judgeFile -Raw -Encoding UTF8 | ConvertFrom-Json

# ---- 3. Action plan (Markdown) --------------------------------------

Write-Host ''
Write-Host '=== Phase 3: Action Plan ===' -ForegroundColor Cyan

$lines = New-Object System.Collections.ArrayList
[void]$lines.Add('# Misconversion Action Plan')
[void]$lines.Add('')
[void]$lines.Add(('Generated: {0}  |  Judge: {1}  |  Cost: ${2}' -f $j.iso_time, $j.model, $j.total_cost_usd))
[void]$lines.Add('')

$auto   = @($j.judged | Where-Object { $_.verdict -eq 'JUDGED' -and $_.confidence -eq 'high' })
$review = @($j.judged | Where-Object { $_.verdict -eq 'JUDGED' -and $_.confidence -in @('medium','low') })
$fails  = @($j.judged | Where-Object { $_.verdict -eq 'UNSURE' })

[void]$lines.Add('## Summary')
[void]$lines.Add('')
[void]$lines.Add(('- 高信頼 (自動アクション候補): {0}' -f $auto.Count))
[void]$lines.Add(('- 中/低信頼 (人間 review 要):   {0}' -f $review.Count))
[void]$lines.Add(('- 判定失敗:                    {0}' -f $fails.Count))
[void]$lines.Add('')

if ($auto.Count -gt 0) {
    [void]$lines.Add('## 高信頼 — 提案アクション')
    [void]$lines.Add('')
    foreach ($it in $auto) {
        [void]$lines.Add(('### {0} → {1}' -f $it.lastCommitted, $it.intended))
        [void]$lines.Add(('- count: {0}  |  category: {1}' -f $it.count, $it.category))
        if ($it.reading_used) {
            [void]$lines.Add(('- reading: `{0}`' -f $it.reading_used))
        }
        [void]$lines.Add(('- rationale: {0}' -f $it.rationale))
        [void]$lines.Add('')
        [void]$lines.Add('推奨アクション:')
        switch ($it.category) {
            'user_dict' {
                $reading = if ($it.reading_used) { $it.reading_used } else { '（読み不明 — 手動入力要）' }
                [void]$lines.Add('- `%APPDATA%\GenerativeIME\dict\user_additions.utf8` に次の行を追加:')
                [void]$lines.Add('  ```')
                [void]$lines.Add(('  {0} /{1}/' -f $reading, $it.intended))
                [void]$lines.Add('  ```')
            }
            'reading_top' {
                [void]$lines.Add('- `modernranking.cpp` の kOverrideTable[] に次を追加:')
                [void]$lines.Add('  ```cpp')
                $tmpl = '  {{L"{0}", L"{1}"}}, // from misconversion log'
                [void]$lines.Add(($tmpl -f $it.reading_used, $it.intended))
                [void]$lines.Add('  ```')
            }
            'bunsetsu' {
                [void]$lines.Add(('- 文節境界の誤り。読み `{0}` を `{1}` と結ぶ最小介入:' -f $it.reading_used, $it.intended))
                [void]$lines.Add('  1. ユーザー辞書 (`%APPDATA%\GenerativeIME\dict\user_additions.utf8`) に読みを丸ごと登録:')
                [void]$lines.Add('     ```')
                [void]$lines.Add(('     {0} /{1}/' -f $it.reading_used, $it.intended))
                [void]$lines.Add('     ```')
                [void]$lines.Add('  2. または `bunsetsu.cpp` の head-priority overrides に追加 (要コード変更)')
            }
            default {
                [void]$lines.Add('- カテゴリ不明。人間判断要')
            }
        }
        [void]$lines.Add('')
    }
}

if ($review.Count -gt 0) {
    [void]$lines.Add('## 中/低信頼 — 人間 review 要')
    [void]$lines.Add('')
    foreach ($it in $review) {
        [void]$lines.Add(('### {0} → {1}' -f $it.lastCommitted, $it.intended))
        [void]$lines.Add(('- confidence: {0}  |  category: {1}' -f $it.confidence, $it.category))
        [void]$lines.Add(('- rationale: {0}' -f $it.rationale))
        [void]$lines.Add('')
    }
}

if ($fails.Count -gt 0) {
    [void]$lines.Add('## 判定失敗')
    [void]$lines.Add('')
    foreach ($it in $fails) {
        [void]$lines.Add(('- {0} (count={1}) — {2}' -f $it.lastCommitted, $it.count, $it.rationale))
    }
    [void]$lines.Add('')
}

($lines -join "`r`n") | Out-File -FilePath $planFile -Encoding utf8

Write-Host ''
Write-Host ('[run] plan written to: {0}' -f $planFile) -ForegroundColor Green
Write-Host ('[run] 高信頼:    {0}' -f $auto.Count)
Write-Host ('[run] review 要: {0}' -f $review.Count)
Write-Host ('[run] 失敗:      {0}' -f $fails.Count)
