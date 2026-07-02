# mine_aozora.ps1 - Extract (reading, kanji) pairs from Aozora Bunko works
# via their ruby annotations. Ruby syntax in Aozora plain text:
#   [FF5C]KANJI<3008>READING<3009>   -- explicit base + reading (|kanji<<reading>>)
#   KANJI<3008>READING<3009>         -- bare kanji + reading auto-detected
# Both are treated as high-confidence pairs since a human transcriber
# wrote them by hand for that specific work.
#
# Comments and strings kept ASCII-only so PowerShell 5.1 does not misread
# the file as cp932. Japanese characters used only via \u escapes in
# regex patterns and the book-list table (as literal Unicode chars
# survives if we save with BOM, so it's ASCII too for portability).
#
# Usage:
#   .\mine_aozora.ps1 -Out ..\..\corpus\aozora\raw\pairs-raw.tsv
#
# Output TSV columns (tab-separated):
#   reading<TAB>kanji<TAB>count<TAB>source_book
#
# Rows are appended per book. aggregate_aozora.ps1 sums counts across
# runs into corpus/goldens/aozora-top500.tsv.

[CmdletBinding()]
param(
    [string]$Out = (Join-Path $PSScriptRoot '..\..\corpus\aozora\raw\pairs-raw.tsv'),
    [int]$MaxBooks = 5
)

$ErrorActionPreference = 'Stop'

# Curated list of short, well-known Aozora works with confirmed direct
# .txt URLs on Aozora's own hosting. Kept small on purpose -- Phase 0
# is about verifying the pipeline, not exhaustive coverage.
# Columns: card=cardId, book=bookId, ruby=rubyRevisionId, plus tags for logging.
$books = @(
    @{ card='000035'; book='1567';  ruby='4948';  tag='dazai-hashire-merosu'    }
    @{ card='000148'; book='752';   ruby='2028';  tag='soseki-botchan'          }
    @{ card='000879'; book='127';   ruby='150';   tag='akutagawa-rashomon'      }
    @{ card='000081'; book='43';    ruby='19793'; tag='miyazawa-chumon'         }
    @{ card='000119'; book='624';   ruby='872';   tag='nakajima-sangetsuki'     }
)

$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# Truncate output; each run rebuilds the raw file.
Set-Content -Path $Out -Value '' -Encoding UTF8 -NoNewline

# Regex patterns. All Japanese chars via \u escapes so the file stays ASCII.
#   [FF5C] = full-width vertical bar (explicit-base marker)
#   [3008]/[3009] = angle brackets around reading
#   [4E00-9FFF] = CJK unified ideographs
#   [3041-309F] = Hiragana
#   [30A0-30FF] = Katakana
#   [3005] = ideographic iteration mark (kanji-repetition)
#   [3006] = kanji-nomi (rare, for names)
#   [30F5] [30F6] = katakana small ka/ke
$annotationRe = '［＃[^］]*］'  # [# ... ] editor annotation
$explicitRe   = '｜([^｜《》]+)《([ぁ-ゟ゠-ヿ]+)》'
$bareRe       = '([一-鿿々〆ヵヶ]+)《([ぁ-ゟ゠-ヿ]+)》'

$totalPairs = 0
$booksProcessed = 0

foreach ($b in $books | Select-Object -First $MaxBooks) {
    $zipUrl = "https://www.aozora.gr.jp/cards/$($b.card)/files/$($b.book)_ruby_$($b.ruby).zip"
    $tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("aozora_" + $b.book)
    if (Test-Path $tmpDir) { Remove-Item -Recurse -Force $tmpDir }
    New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null
    $zipPath = Join-Path $tmpDir 'work.zip'
    Write-Host ("[fetch] {0}" -f $b.tag)
    try {
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing -TimeoutSec 30
    } catch {
        Write-Warning ("  fetch failed: {0}" -f $_.Exception.Message)
        continue
    }
    Expand-Archive -Path $zipPath -DestinationPath $tmpDir -Force
    $txtFile = Get-ChildItem -Path $tmpDir -Filter '*.txt' | Select-Object -First 1
    if (-not $txtFile) {
        Write-Warning "  no .txt in zip; skipping"
        continue
    }
    # Aozora uses cp932 (Shift-JIS) for the .txt content itself.
    $bytes = [System.IO.File]::ReadAllBytes($txtFile.FullName)
    $sjis = [System.Text.Encoding]::GetEncoding(932)
    $text = $sjis.GetString($bytes)

    # Strip editor annotations so they don't leak into the ruby capture.
    $text = [regex]::Replace($text, $annotationRe, '')

    $pairs = New-Object System.Collections.Generic.List[object]

    # Pass 1: explicit-base ruby (kanji or mixed base marked with FF5C).
    $explicitMatches = [regex]::Matches($text, $explicitRe)
    foreach ($m in $explicitMatches) {
        $kanji   = $m.Groups[1].Value
        $reading = $m.Groups[2].Value
        if ($kanji.Length -ge 1 -and $reading.Length -ge 1) {
            $pairs.Add(@($reading, $kanji))
        }
    }
    # Remove the matched spans so bare-base pass doesn't double-capture.
    $textAfter = [regex]::Replace($text, $explicitRe, '$1')

    # Pass 2: bare kanji base immediately followed by <3008>reading<3009>.
    $bareMatches = [regex]::Matches($textAfter, $bareRe)
    foreach ($m in $bareMatches) {
        $kanji   = $m.Groups[1].Value
        $reading = $m.Groups[2].Value
        if ($kanji.Length -ge 1 -and $reading.Length -ge 1) {
            $pairs.Add(@($reading, $kanji))
        }
    }

    Write-Host ("  extracted {0} ruby pairs" -f $pairs.Count)
    $totalPairs += $pairs.Count
    $booksProcessed += 1

    $srcTag = $b.tag
    $sb = New-Object System.Text.StringBuilder
    foreach ($p in $pairs) {
        [void]$sb.Append($p[0]); [void]$sb.Append("`t")
        [void]$sb.Append($p[1]); [void]$sb.Append("`t1`t")
        [void]$sb.Append($srcTag); [void]$sb.Append("`n")
    }
    Add-Content -Path $Out -Value $sb.ToString() -Encoding UTF8 -NoNewline

    Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host ("Books processed: {0}" -f $booksProcessed)
Write-Host ("Total raw pairs: {0}" -f $totalPairs)
Write-Host ("Raw output:      {0}" -f (Resolve-Path $Out))
