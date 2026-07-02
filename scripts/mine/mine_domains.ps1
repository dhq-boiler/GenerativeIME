# mine_domains.ps1 - Orchestrator that runs mine_wikipedia.ps1 across a
# curated set of ja.wikipedia category batches, producing one
# corpus/wikipedia/raw/pairs-raw-<domain>.tsv plus one
# corpus/goldens/wikipedia-top-<domain>.tsv per domain, then
# concatenates all raws + regenerates ModernRanking from the combined
# frequency table.
#
# Domains are chosen for register coverage that random sampling misses:
#   - adult:   sexuality / adult work terminology (per user request)
#   - tech:    programming, IT, software engineering
#   - enter:   anime / manga / video game / voice actor vocabulary
#   - daily:   cooking, business, medicine, law
#   - gaming:  fictional characters, in-game locations, RPG concepts
#
# Each batch is capped so ONE domain doesn't dominate the merged corpus.
# Sequential (not parallel) to stay under Wikipedia API rate limits
# (they suggest <= 200 req/s but shared IP; we do ~1 req/s).
#
# Usage:
#   .\mine_domains.ps1              # runs all domains at default caps
#   .\mine_domains.ps1 -Domains adult,tech  # runs subset

[CmdletBinding()]
param(
    [string[]]$Domains = @('adult', 'tech', 'enter', 'daily', 'gaming'),
    [int]$CapPerDomain = 3000
)

$ErrorActionPreference = 'Stop'
$mine = Join-Path $PSScriptRoot 'mine_wikipedia.ps1'

# Parallel-fan-out: launch one Start-Job per domain, then wait for all to
# finish. Wikipedia's API is fine with 4-6 concurrent connections from one
# IP so long as each keeps to a reasonable rate; our mine_wikipedia.ps1
# does ~1 req/s per instance, so 5 parallel jobs = ~5 req/s aggregate,
# well inside their polite-use guidance.
#
# Trade-off: Start-Job spawns a full powershell.exe per job (~50 MB each),
# but this box has plenty of RAM and the throughput improvement is worth
# it (5x speedup on a corpus mine that would otherwise take 10+ hours).
[bool]$Parallel = $true

# Curated category lists per domain. All are ja.wikipedia Category:
# pages without the "Category:" prefix. mine_wikipedia paginates through
# each with cmcontinue until CapPerDomain articles are collected total.
$domainCategories = @{
    'adult'  = @(
        '性', '性の文化', '性用語', '性行為', '性器', '性愛', '性教育',
        '官能小説', 'ポルノグラフィ', '成人向け作品',
        'BDSM', '性風俗', 'セクシュアリティ'
    )
    'tech'   = @(
        'プログラミング', 'プログラミング言語', 'ソフトウェア工学',
        'コンピュータサイエンス', 'アルゴリズム', 'データ構造',
        'オペレーティングシステム', 'データベース', 'Web技術',
        'IT用語', 'コンピュータネットワーク', 'コンピュータハードウェア',
        'デザインパターン (ソフトウェア)'
    )
    'enter'  = @(
        'アニメ作品', '漫画作品', 'ビデオゲーム', '声優',
        'テレビドラマ', '日本映画', 'ライトノベル',
        'アニメスタジオ', 'ゲーム機', 'ゲームソフト',
        '格闘ゲーム', 'ロールプレイングゲーム',
        'アクションゲーム', 'シミュレーションゲーム'
    )
    'daily'  = @(
        '料理', '郷土料理', '世界の料理', '調味料', '食品',
        'ビジネス', 'ビジネス用語', '経営学', '経済学',
        '金融', '銀行', '証券', '会計',
        '医学', '医学用語', '病気', '解剖学',
        '法律', '法律用語', '刑法', '民法'
    )
    'gaming' = @(
        '架空のキャラクター', '架空の生物', '架空の武器', '架空の魔法',
        '架空の場所', '架空の国家', '架空の乗り物',
        '格闘ゲームのキャラクター', 'ロールプレイングゲームのキャラクター',
        'ビデオゲームの用語', 'ゲームジャンル', 'ゲームシステム'
    )
}

# Kick off one Start-Job per domain.
$jobs = @()
foreach ($domain in $Domains) {
    if (-not $domainCategories.ContainsKey($domain)) {
        Write-Warning ("unknown domain: {0} (available: {1})" -f $domain, ($domainCategories.Keys -join ', '))
        continue
    }
    $cats = $domainCategories[$domain]
    $rawOut = Join-Path $PSScriptRoot ("..\..\corpus\wikipedia\raw\pairs-raw-" + $domain + ".tsv")
    $logOut = Join-Path $PSScriptRoot ("..\..\corpus\wikipedia\raw\pairs-raw-" + $domain + ".log")
    Write-Host ('[start] ' + $domain + ' (' + $cats.Count + ' cats, cap ' + $CapPerDomain + ')') -ForegroundColor Cyan
    $j = Start-Job -Name ("mine-" + $domain) -ScriptBlock {
        param($script, $cats, $cap, $out, $log)
        & $script -Category $cats -MaxArticles $cap -Out $out *> $log
    } -ArgumentList $mine, $cats, $CapPerDomain, $rawOut, $logOut
    $jobs += $j
}

# Poll every 60 s to give a heartbeat; block until every job finishes.
Write-Host ("[wait] {0} parallel domain mines running; polling every 60s" -f $jobs.Count) -ForegroundColor Yellow
$runStart = Get-Date
while ($jobs | Where-Object { $_.State -eq 'Running' }) {
    Start-Sleep -Seconds 60
    $elapsed = (Get-Date) - $runStart
    $running = @($jobs | Where-Object { $_.State -eq 'Running' })
    $done    = @($jobs | Where-Object { $_.State -in 'Completed', 'Failed', 'Stopped' })
    Write-Host ("[{0:mm\:ss}] running: {1}   done: {2}" -f $elapsed, ($running.Name -join ','), ($done.Name -join ','))
}
foreach ($j in $jobs) {
    if ($j.State -eq 'Failed') { Write-Warning ("job {0} failed" -f $j.Name) }
    Receive-Job $j | Out-Null   # drain output
    Remove-Job $j -Force
}
$totalElapsed = (Get-Date) - $runStart
Write-Host ("[done] all domain mines finished in {0:hh\:mm\:ss}" -f $totalElapsed) -ForegroundColor Green

# After every domain finishes, combine all pairs-raw-*.tsv into a single
# merged aggregate and regenerate the modernranking table from it. The
# per-domain goldens remain around for later inspection / A/B testing.
Write-Host ''
Write-Host '==== merging all raws into combined golden ====' -ForegroundColor Cyan
$rawDir = Join-Path $PSScriptRoot '..\..\corpus\wikipedia\raw'
$goldenDir = Join-Path $PSScriptRoot '..\..\corpus\goldens'
$combined = @{}
$rawTotal = 0
foreach ($f in Get-ChildItem $rawDir -Filter 'pairs-raw-*.tsv') {
    foreach ($row in Get-Content $f.FullName -Encoding UTF8) {
        if ($row.Length -eq 0) { continue }
        $parts = $row -split "`t"
        if ($parts.Length -lt 2) { continue }
        $rawTotal++
        $key = "$($parts[0])`t$($parts[1])"
        if ($combined.ContainsKey($key)) { $combined[$key] += 1 } else { $combined[$key] = 1 }
    }
}
$mergedGolden = Join-Path $goldenDir 'wikipedia-top-combined.tsv'
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# wikipedia-top-combined.tsv  -- merged (reading, kanji, count) across all domain runs")
[void]$sb.AppendLine(("# generated from {0} raw rows, {1} distinct pairs" -f $rawTotal, $combined.Count))
[void]$sb.AppendLine("# columns: reading<TAB>kanji<TAB>count")
foreach ($k in $combined.Keys | Sort-Object { -$combined[$_] }) {
    [void]$sb.Append($k); [void]$sb.Append("`t"); [void]$sb.AppendLine([string]$combined[$k])
}
[System.IO.File]::WriteAllText($mergedGolden, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host ("merged golden: {0}" -f $mergedGolden)
Write-Host ("  distinct pairs: {0}" -f $combined.Count)
