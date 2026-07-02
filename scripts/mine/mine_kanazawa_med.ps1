# mine_kanazawa_med.ps1 - Extract (Japanese kanji, English, Latin combining
# form) triples from the Kanazawa Medical University medical-English PDF
# (kt.kanazawa-med.ac.jp/english/2-1.pdf), derive hiragana readings via
# MeCab, and emit SKK direct entries in the format:
#   reading /日本語/English/
#
# The Latin combining form is preserved as a comment above each block
# for future reference / re-run but not emitted as a candidate (users
# typically don't type "gastr/o" mid-composition).
#
# Requires: mecab.exe (already installed by mine_wikipedia.ps1).

[CmdletBinding()]
param(
    [string]$InTxt = 'C:\Users\dhq_b\AppData\Local\Temp\claude\C--Git-GenerativeIME\b5bcc637-24de-4ad3-b368-781caa736d7b\scratchpad\kmuh\2-1.txt',
    [string]$OutTsv = (Join-Path $PSScriptRoot '..\..\corpus\medical\kanazawa-triples.tsv'),
    [string]$MecabExe = 'C:\Program Files\MeCab\bin\mecab.exe'
)

$ErrorActionPreference = 'Stop'
$outDir = Split-Path -Parent $OutTsv
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# Parse the PDF text via "trim + split on 2-or-more spaces" - way more
# robust than trying to write one regex that handles ASCII, kanji, small
# katakana, and U+2010 hyphens all at once.
#
# Layout examples:
#   "  1 gastr/o                         胃 (stomach)"
#   " 15 col/o                             結腸、大腸 (colon, large intestine)"
# After trim + split on / {2,}/ we get 3 cells:
#   [0] "1 gastr/o"     -> discard leading digit, keep "gastr/o" as latin form
#   [1] "胃"            -> Japanese
#   [2] "(stomach)"     -> English inside parens
# Some lines have the Japanese + English cells merged so we peel a
# trailing "( ... )" off the last cell instead of relying on split.
$rows = New-Object System.Collections.Generic.List[object]
$currentSection = ''
foreach ($line in Get-Content $InTxt -Encoding UTF8) {
    if ($line -match '^Section\s+\d+\s+(.+)$') {
        $currentSection = $Matches[1].Trim()
        continue
    }
    $trimmed = $line.Trim()
    if ([string]::IsNullOrEmpty($trimmed)) { continue }
    # Line must start with a digit (entry ordinal).
    if (-not ($trimmed -match '^\d+\s')) { continue }

    # Split on 2+ spaces to get logical cells.
    $cells = [regex]::Split($trimmed, ' {2,}') | Where-Object { $_ }
    if ($cells.Count -lt 2) { continue }

    # First cell = "<num> <latin form>". Strip the leading number.
    $first = $cells[0]
    $latin = ($first -replace '^\d+\s+', '').Trim()

    # Skip prefix / suffix rows (they start with "-" or a Unicode hyphen).
    if ($latin.Length -eq 0) { continue }
    if ($latin.StartsWith('-') -or $latin.StartsWith([char]0x2010)) { continue }
    if ($latin.EndsWith('-')   -or $latin.EndsWith([char]0x2010))   { continue }

    # Last cell(s) hold the Japanese + (English). If we got a clean 3-cell
    # split, cells[1]=JP, cells[2]="(EN)". If merged into one cell, peel.
    $jp = ''
    $en = ''
    if ($cells.Count -ge 3) {
        $jp = $cells[1].Trim()
        $en = $cells[2].Trim()
    } else {
        $jp = $cells[1].Trim()
    }
    # Peel parenthetical English off whichever cell it's stuck to.
    $parenMatch = [regex]::Match($jp, '^(.+?)\s*[\(（]([^\)）]+)[\)）]\s*$')
    if ($parenMatch.Success) {
        $jp = $parenMatch.Groups[1].Value.Trim()
        $en = $parenMatch.Groups[2].Value.Trim()
    } else {
        # Otherwise strip stray parens off English cell.
        $en = ($en -replace '^[\(（]', '') -replace '[\)）]$', ''
        $en = $en.Trim()
    }
    if ([string]::IsNullOrEmpty($jp)) { continue }

    # Japanese cell must contain at least one CJK / kana character to be
    # useful as an SKK entry. Descriptor cells ("以上、過剰") get rejected.
    if (-not ($jp -match '[一-鿿゠-ヿ぀-ゟ]')) { continue }

    # Sometimes the Japanese cell holds multiple comma-separated terms
    # ("結腸、大腸"). Split so each gets its own SKK entry.
    $jpTerms = @($jp -split '[、,／・]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })

    foreach ($t in $jpTerms) {
        $t = ($t -replace '[（\(].*$', '').Trim()
        if ($t.Length -eq 0) { continue }
        $rows.Add([pscustomobject]@{
            Section  = $currentSection
            Latin    = $latin
            Japanese = $t
            English  = $en
        })
    }
}

Write-Host ("parsed {0} (Japanese, English, Latin) rows" -f $rows.Count)

# Get MeCab readings in a batch. Build one input file with all Japanese
# terms, one per line, cp932-encoded.
$scratch = 'C:\Users\dhq_b\AppData\Local\Temp\claude\C--Git-GenerativeIME\b5bcc637-24de-4ad3-b368-781caa736d7b\scratchpad'
$mecabIn  = Join-Path $scratch 'kmuh-in.txt'
$mecabOut = Join-Path $scratch 'kmuh-out.txt'
$sjis = [System.Text.Encoding]::GetEncoding(932)
$sb = New-Object System.Text.StringBuilder
foreach ($r in $rows) { [void]$sb.AppendLine($r.Japanese) }
[System.IO.File]::WriteAllBytes($mecabIn, $sjis.GetBytes($sb.ToString()))
& $MecabExe -o $mecabOut $mecabIn
$outText = $sjis.GetString([System.IO.File]::ReadAllBytes($mecabOut))

# MeCab emits per-morpheme lines terminated by "EOS". Collect the
# katakana reading from field 8 for every morpheme between EOS boundaries
# and concatenate.
function KataToHira([string]$k) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($c in $k.ToCharArray()) {
        $u = [int]$c
        if ($u -ge 0x30A1 -and $u -le 0x30F3) { [void]$sb.Append([char]($u - 0x60)) }
        else { [void]$sb.Append($c) }
    }
    return $sb.ToString()
}
$readings = New-Object System.Collections.Generic.List[string]
$cur = ''
foreach ($line in $outText -split "`r?`n") {
    if ($line -eq 'EOS') {
        $readings.Add($cur)
        $cur = ''
        continue
    }
    if ($line.Length -eq 0) { continue }
    $tab = $line.IndexOf("`t")
    if ($tab -lt 0) { continue }
    $fields = ($line.Substring($tab + 1)) -split ','
    if ($fields.Count -lt 8) { continue }
    $kata = $fields[7]
    if ($kata -eq '*' -or [string]::IsNullOrEmpty($kata)) {
        $cur += $line.Substring(0, $tab)
    } else {
        $cur += (KataToHira $kata)
    }
}

# Attach reading to each row.
for ($i = 0; $i -lt $rows.Count; $i++) {
    if ($i -lt $readings.Count) {
        $rows[$i] | Add-Member -MemberType NoteProperty -Name Reading -Value $readings[$i]
    } else {
        $rows[$i] | Add-Member -MemberType NoteProperty -Name Reading -Value ''
    }
}

# Dedupe by (reading, japanese) so re-runs don't inflate.
$seen = New-Object System.Collections.Generic.HashSet[string]
$final = @()
foreach ($r in $rows) {
    if ([string]::IsNullOrEmpty($r.Reading)) { continue }
    $key = $r.Reading + "`t" + $r.Japanese
    if ($seen.Add($key)) { $final += $r }
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# kanazawa-triples.tsv - Japanese medical vocabulary with English + Latin combining form")
[void]$sb.AppendLine("# source: kt.kanazawa-med.ac.jp/english/2-1.pdf")
[void]$sb.AppendLine("# columns: reading<TAB>japanese<TAB>english<TAB>latin<TAB>section")
foreach ($r in $final) {
    [void]$sb.AppendLine(("{0}`t{1}`t{2}`t{3}`t{4}" -f $r.Reading, $r.Japanese, $r.English, $r.Latin, $r.Section))
}
[System.IO.File]::WriteAllText($OutTsv, $sb.ToString(), [System.Text.UTF8Encoding]::new($false))
Write-Host ("wrote {0} unique triples to {1}" -f $final.Count, $OutTsv)
