# mine_wikipedia.ps1 - Fetch a small list of Wikipedia articles, tokenize
# each via IPADIC MeCab, and emit (reading, kanji, count, source) TSV rows.
# Complement to mine_aozora.ps1: Wikipedia bodies are modern-register
# general Japanese, exactly what we need for MS-IME replacement scoping.
#
# Requires:
#   - mecab.exe at C:\Program Files\MeCab\bin\mecab.exe (IPADIC bundled).
#     Install via https://github.com/ikegami-yukino/mecab/releases/latest.
#   - PowerShell 5.1+ (script itself saved UTF-8 with BOM so PS 5.1
#     doesn't cp932-mis-decode the Japanese Unicode escapes below).
#
# Usage:
#   .\mine_wikipedia.ps1 -Out ..\..\corpus\wikipedia\raw\pairs-raw.tsv -MaxArticles 20
#
# Output columns:
#   reading<TAB>kanji<TAB>1<TAB>source_title
#
# `reading` is the katakana reading MeCab gave us, converted to hiragana
# so it matches what an IME user would actually type. `kanji` is the
# surface form (must contain at least one CJK ideograph to be emitted).

[CmdletBinding()]
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\..\corpus\wikipedia\raw\pairs-raw.tsv'),
    [int]$MaxArticles = 10,
    [string]$MecabExe = 'C:\Program Files\MeCab\bin\mecab.exe',
    [switch]$Random,
    [int]$RandomBatchSize = 20
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $MecabExe)) {
    Write-Error "mecab.exe not found at $MecabExe. Install from https://github.com/ikegami-yukino/mecab/releases/latest"
    return
}

# Seed article titles: chosen to cover general modern registers -- urban
# geography, common nouns, everyday tech, business. Also the fallback
# when -Random is not set.
$seedTitles = @(
    '東京', '京都', '大阪', '横浜', '名古屋',
    '会社', '学校', '電車', '天気', '時間',
    'パソコン', 'スマートフォン', 'インターネット', 'アプリケーション',
    '料理', '音楽', '映画', '本', 'ゲーム', '旅行'
)

# -Random mode fetches random article titles from the MediaWiki random-page
# endpoint (rnnamespace=0 = main article namespace only, no talk/user/etc.).
# We batch-request rnlimit=up-to-20 per call and loop until we have
# $MaxArticles titles. This is way better than a hardcoded seed for coverage:
# a random sample covers registers we'd never think to enumerate (sports
# players, chemistry, geography, media franchises, ...).
function Get-RandomTitles([int]$Count) {
    $titles = @()
    while ($titles.Count -lt $Count) {
        $limit = [Math]::Min(20, $Count - $titles.Count)
        $url = "https://ja.wikipedia.org/w/api.php?action=query&format=json&list=random&rnnamespace=0&rnlimit=$limit&formatversion=2"
        try {
            $r = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 30 -UserAgent 'GenerativeIME-corpus-mining/0.1 (contact: dhq_boiler@live.jp)'
        } catch {
            Write-Warning ("random fetch failed: {0}" -f $_.Exception.Message)
            Start-Sleep -Seconds 2
            continue
        }
        $j = $r.Content | ConvertFrom-Json
        foreach ($t in $j.query.random) { $titles += $t.title }
    }
    return $titles | Select-Object -First $Count
}

if ($Random) {
    Write-Host ("[random] fetching {0} random article titles..." -f $MaxArticles)
    $titles = Get-RandomTitles -Count $MaxArticles
    Write-Host ("[random] got {0} titles: {1}..." -f $titles.Count, ($titles[0..([Math]::Min(4, $titles.Count - 1))] -join ', '))
} else {
    $titles = $seedTitles
}

$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
Set-Content -Path $Out -Value '' -Encoding UTF8 -NoNewline

$sjis = [System.Text.Encoding]::GetEncoding(932)
$scratch = 'C:\Users\dhq_b\AppData\Local\Temp\claude\C--Git-GenerativeIME\b5bcc637-24de-4ad3-b368-781caa736d7b\scratchpad'
if (-not (Test-Path $scratch)) { New-Item -ItemType Directory -Force -Path $scratch | Out-Null }

# Katakana -> Hiragana. Katakana U+30A1..U+30F3 map to Hiragana by -0x60,
# except 30FC (long vowel mark) which we keep as-is because IME users
# type it too.
function KataToHira([string]$k) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($c in $k.ToCharArray()) {
        $u = [int]$c
        if ($u -ge 0x30A1 -and $u -le 0x30F3) {
            [void]$sb.Append([char]($u - 0x60))
        } else {
            [void]$sb.Append($c)
        }
    }
    return $sb.ToString()
}

function ContainsKanji([string]$s) {
    foreach ($c in $s.ToCharArray()) {
        $u = [int]$c
        if (($u -ge 0x4E00 -and $u -le 0x9FFF) -or $u -eq 0x3005) { return $true }
    }
    return $false
}

function IsPureHiragana([string]$s) {
    if ($s.Length -eq 0) { return $false }
    foreach ($c in $s.ToCharArray()) {
        $u = [int]$c
        # 3041..309F is hiragana range; also allow 30FC (long-vowel mark)
        # since MeCab keeps it as-is inside loanword readings.
        if (-not (($u -ge 0x3041 -and $u -le 0x309F) -or $u -eq 0x30FC)) {
            return $false
        }
    }
    return $true
}

$totalPairs = 0
$articlesProcessed = 0

foreach ($title in $titles | Select-Object -First $MaxArticles) {
    Write-Host ("[fetch] {0}" -f $title)
    $encoded = [uri]::EscapeDataString($title)
    $url = "https://ja.wikipedia.org/w/api.php?action=query&format=json&prop=extracts&titles=$encoded&explaintext=1&exsectionformat=plain&formatversion=2"
    try {
        $resp = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 30 -UserAgent 'GenerativeIME-corpus-mining/0.1 (contact: dhq_boiler@live.jp)'
    } catch {
        Write-Warning ("  fetch failed: {0}" -f $_.Exception.Message)
        continue
    }
    $j = $resp.Content | ConvertFrom-Json
    $body = $j.query.pages[0].extract
    if (-not $body) {
        Write-Warning "  empty body; skipping"
        continue
    }
    # Truncate to first ~8KB per article -- lead section is where most
    # high-value pairs are; deeper sections have long lists / references.
    if ($body.Length -gt 4000) { $body = $body.Substring(0, 4000) }

    $inFile  = Join-Path $scratch ("wiki_in_"  + $articlesProcessed + '.txt')
    $outFile = Join-Path $scratch ("wiki_out_" + $articlesProcessed + '.txt')
    [System.IO.File]::WriteAllBytes($inFile, $sjis.GetBytes($body))
    & $MecabExe -o $outFile $inFile
    $outBytes = [System.IO.File]::ReadAllBytes($outFile)
    $outText  = $sjis.GetString($outBytes)

    $pairs = New-Object System.Collections.Generic.List[object]
    foreach ($line in $outText -split "`r?`n") {
        if ($line -eq 'EOS' -or $line.Length -eq 0) { continue }
        # Line format: surface\tPoS,subPoS,...,base,reading,pronunciation
        $tab = $line.IndexOf("`t")
        if ($tab -lt 0) { continue }
        $surface = $line.Substring(0, $tab)
        $fields  = ($line.Substring($tab + 1)) -split ','
        if ($fields.Count -lt 8) { continue }
        $readingKata = $fields[7]
        if (-not $readingKata -or $readingKata -eq '*') { continue }
        if (-not (ContainsKanji $surface)) { continue }
        $readingHira = KataToHira $readingKata
        if (-not (IsPureHiragana $readingHira)) { continue }
        # Guard against noise: reading must be reasonable length.
        if ($readingHira.Length -lt 1 -or $readingHira.Length -gt 20) { continue }
        $pairs.Add(@($readingHira, $surface))
    }

    Write-Host ("  extracted {0} (reading, kanji) pairs" -f $pairs.Count)
    $totalPairs += $pairs.Count
    $articlesProcessed += 1

    $sb = New-Object System.Text.StringBuilder
    foreach ($p in $pairs) {
        [void]$sb.Append($p[0]); [void]$sb.Append("`t")
        [void]$sb.Append($p[1]); [void]$sb.Append("`t1`t")
        [void]$sb.Append($title); [void]$sb.Append("`n")
    }
    Add-Content -Path $Out -Value $sb.ToString() -Encoding UTF8 -NoNewline

    Remove-Item -Force $inFile, $outFile -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host ("Articles processed: {0}" -f $articlesProcessed)
Write-Host ("Total raw pairs:    {0}" -f $totalPairs)
Write-Host ("Raw output:         {0}" -f (Resolve-Path $Out))

# Aggregation: dedupe (reading, kanji) pairs, sum counts, write sorted
# TSV to corpus/goldens/wikipedia-top.tsv. This file is the actual
# regression-test fixture; raw is kept for reproducibility only.
$goldenDir = Join-Path $PSScriptRoot '..\..\corpus\goldens'
if (-not (Test-Path $goldenDir)) { New-Item -ItemType Directory -Force -Path $goldenDir | Out-Null }
$goldenFile = Join-Path $goldenDir 'wikipedia-top.tsv'

$rows = Get-Content $Out -Encoding UTF8
$rawCount = $rows.Count
$agg = @{}
foreach ($row in $rows) {
    $parts = $row -split "`t"
    if ($parts.Length -lt 2) { continue }
    $key = "$($parts[0])`t$($parts[1])"
    if ($agg.ContainsKey($key)) { $agg[$key] += 1 } else { $agg[$key] = 1 }
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# wikipedia-top.tsv  -- aggregated (reading, kanji, count) from mine_wikipedia.ps1")
[void]$sb.AppendLine(("# generated from {0} raw rows, {1} distinct pairs" -f $rawCount, $agg.Count))
[void]$sb.AppendLine("# columns: reading<TAB>kanji<TAB>count")
foreach ($k in $agg.Keys | Sort-Object { -$agg[$_] }) {
    [void]$sb.Append($k); [void]$sb.Append("`t"); [void]$sb.AppendLine([string]$agg[$k])
}
[System.IO.File]::WriteAllText($goldenFile, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host ("Aggregated golden:  {0}" -f $goldenFile)
Write-Host ("  distinct pairs:   {0}" -f $agg.Count)
