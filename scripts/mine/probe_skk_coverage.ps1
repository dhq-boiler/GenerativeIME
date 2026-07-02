# probe_skk_coverage.ps1 - For each (reading, expected_kanji) in a golden
# TSV, look up the reading in SKK-JISYO.L.utf8 and check whether the
# first (top) candidate matches expected_kanji. Reports pass rate,
# categorized misses, and a small sample of each failure class.
#
# This is a *raw SKK* probe: it ignores our IME's HasDirectEntry bypass,
# ReadsAs filter, MeCab-based bunsetsu synthesis, and learning fav.
# Useful as a lower-bound coverage snapshot -- if SKK by itself already
# nails the top, the runtime code path very likely does too.
#
# Usage:
#   .\probe_skk_coverage.ps1 -Golden ..\..\corpus\goldens\wikipedia-top.tsv -MinCount 100

[CmdletBinding()]
param(
    [string]$Golden = (Join-Path $PSScriptRoot '..\..\corpus\goldens\wikipedia-top.tsv'),
    [string]$Skk    = (Join-Path $PSScriptRoot '..\..\third_party\skk\SKK-JISYO.L.utf8'),
    [int]$MinCount  = 100,
    [int]$SampleMisses = 15
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Golden)) { Write-Error "Golden not found: $Golden"; return }
if (-not (Test-Path $Skk))    { Write-Error "SKK not found: $Skk"; return }

Write-Host ("[load] SKK dict {0} ..." -f $Skk)
# Build a reading -> first-candidate hash. SKK line format is
#   reading /cand1;annotation/cand2/.../
# First-candidate extraction: substring after the first "/", up to the
# next "/" or ";". We skip okuri-ari lines (their reading ends with an
# ASCII letter marking the okurigana class) since those don't
# correspond to a whole-reading direct hit.
$skkTop = @{}
$reader = [System.IO.File]::OpenText($Skk)
$loaded = 0
while ($null -ne ($line = $reader.ReadLine())) {
    if ($line.Length -eq 0 -or $line[0] -eq ';') { continue }
    $sp = $line.IndexOf(' ')
    if ($sp -lt 1) { continue }
    $reading = $line.Substring(0, $sp)
    # Skip okuri-ari (reading last char is ASCII letter a-z).
    $lastChar = [int]$reading[$reading.Length - 1]
    if ($lastChar -ge 0x61 -and $lastChar -le 0x7A) { continue }
    $rest = $line.Substring($sp + 1)
    if (-not $rest.StartsWith('/')) { continue }
    $endSlash = $rest.IndexOf('/', 1)
    if ($endSlash -lt 2) { continue }
    $top = $rest.Substring(1, $endSlash - 1)
    # Strip ";annotation" from candidate.
    $semi = $top.IndexOf(';')
    if ($semi -ge 0) { $top = $top.Substring(0, $semi) }
    $skkTop[$reading] = $top
    $loaded += 1
}
$reader.Close()
Write-Host ("[load] {0} direct-hit readings indexed" -f $loaded)

Write-Host ("[read] golden {0} (min count = {1})" -f $Golden, $MinCount)
$entries = @()
foreach ($line in Get-Content $Golden -Encoding UTF8) {
    if ($line.Length -eq 0 -or $line[0] -eq '#') { continue }
    $p = $line -split "`t"
    if ($p.Count -lt 3) { continue }
    $count = [int]$p[2]
    if ($count -lt $MinCount) { break }  # file is sorted desc
    $entries += ,[pscustomobject]@{
        Reading  = $p[0]
        Expected = $p[1]
        Count    = $count
    }
}
Write-Host ("[read] {0} golden entries above threshold" -f $entries.Count)

$pass = 0
$missNoEntry = @()      # reading not in SKK at all
$missWrongTop = @()     # reading in SKK but first cand != expected
foreach ($g in $entries) {
    if (-not $skkTop.ContainsKey($g.Reading)) {
        $missNoEntry += $g
        continue
    }
    $top = $skkTop[$g.Reading]
    if ($top -eq $g.Expected) {
        $pass += 1
    } else {
        $missWrongTop += ,[pscustomobject]@{
            Reading = $g.Reading
            Expected = $g.Expected
            SkkTop = $top
            Count = $g.Count
        }
    }
}

$total = $entries.Count
$missTotal = $total - $pass
Write-Host ''
Write-Host '=== SKK top-candidate coverage ==='
Write-Host ("  Total golden entries:  {0}" -f $total)
Write-Host ("  PASS (SKK top == expected):  {0}  ({1:F1}%)" -f $pass, (100.0 * $pass / $total))
Write-Host ("  MISS (no SKK entry at all):  {0}" -f $missNoEntry.Count)
Write-Host ("  MISS (SKK top is wrong):     {0}" -f $missWrongTop.Count)
Write-Host ''

if ($missNoEntry.Count -gt 0) {
    Write-Host ("--- sample of 'no SKK entry' misses (top {0} by frequency) ---" -f $SampleMisses)
    $missNoEntry | Sort-Object -Property Count -Descending | Select-Object -First $SampleMisses | ForEach-Object {
        Write-Host ("  {0,5}x  {1,-14}  expected: {2}" -f $_.Count, $_.Reading, $_.Expected)
    }
    Write-Host ''
}
if ($missWrongTop.Count -gt 0) {
    Write-Host ("--- sample of 'SKK top wrong' misses (top {0} by frequency) ---" -f $SampleMisses)
    $missWrongTop | Sort-Object -Property Count -Descending | Select-Object -First $SampleMisses | ForEach-Object {
        Write-Host ("  {0,5}x  {1,-14}  expected: {2,-8}  skk_top: {3}" -f $_.Count, $_.Reading, $_.Expected, $_.SkkTop)
    }
}
