# scan.ps1 — %APPDATA%\GenerativeIME\misconversions.log をパースして
# 一意な誤変換を JSON 化する。
#
# ログは Ctrl+F5 で 1 エントリ追記される (textservice.cpp:LogMisconversionAttempt)。
# 同じ誤変換を何度も押しがちなので lastCommitted で dedupe。
#
# 差分スキャン: 前回のスキャンで処理した最新 timestamp を状態に持ち、
# それより新しいエントリだけ抽出。
#
# Usage:
#   .\scripts\loop\misconversion\scan.ps1
#   .\scripts\loop\misconversion\scan.ps1 -All  # 差分でなく全件

param(
    [switch]$All,
    [string]$LogPath = "$env:APPDATA\GenerativeIME\misconversions.log"
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$stateDir  = Join-Path $root '.claude\state'
$stateFile = Join-Path $stateDir 'misconversion_scan.json'
if (-not (Test-Path $stateDir)) { New-Item -ItemType Directory -Path $stateDir -Force | Out-Null }

if (-not (Test-Path $LogPath)) {
    Write-Host "[scan] no log at $LogPath" -ForegroundColor Yellow
    return
}

# 前回の位置を読む (存在すれば)
$lastProcessedTs = ''
if (-not $All -and (Test-Path $stateFile)) {
    try {
        $prev = Get-Content $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
        $lastProcessedTs = [string]$prev.last_processed_timestamp
    } catch {}
}

# パース: --- TIMESTAMP --- で始まるブロック単位
$raw = Get-Content $LogPath -Raw -Encoding UTF8
# BOM 除去
if ($raw.Length -gt 0 -and [int]$raw[0] -eq 0xFEFF) { $raw = $raw.Substring(1) }

$blocks = $raw -split '(?m)^--- '
$entries = New-Object System.Collections.ArrayList

foreach ($b in $blocks) {
    $b = $b.Trim()
    if ([string]::IsNullOrWhiteSpace($b)) { continue }
    # 最初の行は "TIMESTAMP ---"
    $lines = $b -split "`r?`n"
    if ($lines.Count -lt 2) { continue }
    $header = $lines[0]
    if ($header -notmatch '^([\d-]+T[\d:]+)\s+---') { continue }
    $ts = $matches[1]

    # 差分スキャン: 前回位置より古いエントリはスキップ
    if ($lastProcessedTs -and ($ts -le $lastProcessedTs)) { continue }

    $fields = @{}
    for ($i = 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^(\w+):\s*(.*)$') {
            $fields[$matches[1]] = $matches[2]
        }
    }

    [void]$entries.Add([pscustomobject]@{
        timestamp     = $ts
        selected      = $fields['selected']
        candidates    = $fields['candidates']
        lastCommitted = $fields['lastCommitted']
        context       = $fields['context']
        forgetReading = $fields['forgetReading']
        buffer        = $fields['buffer']
        display       = $fields['display']
    })
}

# dedupe by lastCommitted (最頻を残す = 最初の登場を残す + count)
$grouped = $entries | Group-Object lastCommitted | Where-Object { -not [string]::IsNullOrEmpty($_.Name) }
$unique = New-Object System.Collections.ArrayList
foreach ($g in $grouped) {
    $first = $g.Group | Sort-Object timestamp | Select-Object -First 1
    $reading = ($g.Group | ForEach-Object { $_.forgetReading } | Where-Object { $_ } | Select-Object -First 1)
    [void]$unique.Add([pscustomobject]@{
        lastCommitted = $g.Name
        count         = $g.Count
        first_ts      = $first.timestamp
        forgetReading = [string]$reading
        selected      = $first.selected
        candidates    = $first.candidates
        context       = $first.context
    })
}

# 最新 ts を保存
$latestTs = if ($entries.Count -gt 0) {
    ($entries | Sort-Object timestamp | Select-Object -Last 1).timestamp
} else {
    $lastProcessedTs
}

$out = [ordered]@{
    iso_time                = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    log_path                = $LogPath
    total_entries_scanned   = $entries.Count
    unique_misconversions   = $unique.Count
    last_processed_timestamp = $latestTs
    misconversions          = @($unique)
}
$out | ConvertTo-Json -Depth 5 | Out-File -FilePath $stateFile -Encoding utf8

Write-Host "[scan] log:                 $LogPath"
Write-Host "[scan] scanned entries:     $($entries.Count) (since $lastProcessedTs)"
Write-Host "[scan] unique conversions:  $($unique.Count)"
Write-Host "[scan] latest ts:           $latestTs"
Write-Host "[scan] state:               $stateFile"

if ($unique.Count -gt 0) {
    Write-Host ""
    Write-Host "Top misconversions:"
    $unique | Sort-Object count -Descending | Select-Object -First 15 |
        Format-Table count, lastCommitted, forgetReading -AutoSize
}
