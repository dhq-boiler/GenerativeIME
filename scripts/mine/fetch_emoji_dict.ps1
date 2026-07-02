# Fetch skk-dev/dict's SKK-JISYO.emoji (CLDR-derived, Unicode License) and
# distill it into third_party/skk/SKK-JISYO.emoji.utf8 for the IME:
#
#   * keep only kana-reading keys (^[ぁ-ゖー]+$). ASCII keys like "ok" or
#     "10:00" would be misparsed by SkkDictionary's okuri-ari detection
#     (trailing ASCII letter = okuri stem) and are untypeable through our
#     romaji composition anyway.
#   * strip ";U+xxxx" annotations (the loader strips them too; dropping them
#     here keeps the shipped file small).
#   * append VS16 (U+FE0F) after codepoints that default to TEXT
#     presentation (Emoji_Presentation=No in emoji-data.txt) and have a
#     registered emoji variation sequence (emoji-variation-sequences.txt),
#     so ❤ ☺ 🗺 … render in color. The upstream dict carries no VS16 at
#     all. Emoji-presentation-default bases (😀 🐕 ⭐ …) are left alone —
#     adding VS16 there would break RGI ZWJ sequences like 🐕‍🦺. Also
#     skipped when the next codepoint is a skin-tone modifier (VS16 must
#     not precede U+1F3FB..1F3FF) or already VS16.
#
# The Unicode license header is copied through verbatim — its terms require
# the notice to accompany redistributed data.
#
# Idempotent: regenerates the output from scratch on every run.

param(
    [string]$Url     = 'https://raw.githubusercontent.com/skk-dev/dict/master/SKK-JISYO.emoji',
    [string]$VsUrl   = 'https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-variation-sequences.txt',
    [string]$EdUrl   = 'https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt',
    [string]$Source  = '',   # local copy; skips download when set
    [string]$VsFile  = '',   # local emoji-variation-sequences.txt; skips download when set
    [string]$EdFile  = '',   # local emoji-data.txt; skips download when set
    [string]$OutFile = (Join-Path $PSScriptRoot '..\..\third_party\skk\SKK-JISYO.emoji.utf8')
)

$ErrorActionPreference = 'Stop'

if (-not $Source) {
    $Source = Join-Path $env:TEMP 'SKK-JISYO.emoji.download'
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Source -UseBasicParsing
}
if (-not $VsFile) {
    $VsFile = Join-Path $env:TEMP 'emoji-variation-sequences.download'
    Write-Host "Downloading $VsUrl"
    Invoke-WebRequest -Uri $VsUrl -OutFile $VsFile -UseBasicParsing
}
if (-not $EdFile) {
    $EdFile = Join-Path $env:TEMP 'emoji-data.download'
    Write-Host "Downloading $EdUrl"
    Invoke-WebRequest -Uri $EdUrl -OutFile $EdFile -UseBasicParsing
}

$lines = [System.IO.File]::ReadAllLines($Source, [System.Text.Encoding]::UTF8)

# Codepoints with a registered "XXXX FE0F ; emoji style" variation sequence.
# Only these legitimately take VS16; blanket insertion would smuggle stray
# invisible characters into users' committed text.
$vs16Bases = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($vsLine in [System.IO.File]::ReadAllLines($VsFile)) {
    if ($vsLine -match '^([0-9A-F]{4,6}) FE0F\s*;\s*emoji style') {
        [void]$vs16Bases.Add([Convert]::ToInt32($Matches[1], 16))
    }
}
if ($vs16Bases.Count -lt 100) { throw "emoji-variation-sequences parse produced only $($vs16Bases.Count) bases — format change?" }

# Codepoints whose DEFAULT presentation is already emoji (Emoji_Presentation
# property). These render in color without VS16, and inserting VS16 would
# de-RGI ZWJ sequences that contain them.
$emojiDefault = New-Object 'System.Collections.Generic.HashSet[int]'
foreach ($edLine in [System.IO.File]::ReadAllLines($EdFile)) {
    if ($edLine -match '^([0-9A-F]{4,6})(?:\.\.([0-9A-F]{4,6}))?\s*;\s*Emoji_Presentation\b') {
        $lo = [Convert]::ToInt32($Matches[1], 16)
        $hi = if ($Matches[2]) { [Convert]::ToInt32($Matches[2], 16) } else { $lo }
        for ($cp = $lo; $cp -le $hi; $cp++) { [void]$emojiDefault.Add($cp) }
    }
}
if ($emojiDefault.Count -lt 500) { throw "emoji-data parse produced only $($emojiDefault.Count) Emoji_Presentation codepoints — format change?" }
$vs16Bases.ExceptWith($emojiDefault)
Write-Host "VS16-needing (text-default) bases: $($vs16Bases.Count)"

# Emoji presentation: walk UTF-16 code units, re-emit each codepoint, and
# slip in VS16 after any base with a registered emoji variation sequence.
function Add-EmojiPresentation([string]$cand) {
    $sb = New-Object System.Text.StringBuilder
    $i = 0
    while ($i -lt $cand.Length) {
        $cp = [char]::ConvertToUtf32($cand, $i)
        $len = if ([char]::IsHighSurrogate($cand[$i])) { 2 } else { 1 }
        [void]$sb.Append($cand, $i, $len)
        $i += $len

        if ($script:vs16Bases.Contains($cp)) {
            $nextCp = if ($i -lt $cand.Length) { [char]::ConvertToUtf32($cand, $i) } else { -1 }
            $nextIsVs16 = $nextCp -eq 0xFE0F
            $nextIsSkin = $nextCp -ge 0x1F3FB -and $nextCp -le 0x1F3FF
            if (-not $nextIsVs16 -and -not $nextIsSkin) {
                [void]$sb.Append([char]0xFE0F)
            }
        }
    }
    $sb.ToString()
}

$out = New-Object System.Collections.Generic.List[string]

# License / provenance header: every leading comment line before the first
# entry or section marker.
foreach ($line in $lines) {
    if (-not $line.StartsWith(';')) { break }
    $out.Add($line)
}
$out.Add(';;')
$out.Add(';; Filtered for GenerativeIME by scripts/mine/fetch_emoji_dict.ps1:')
$out.Add(';; kana-reading keys only, annotations stripped, VS16 added.')
$out.Add(';; okuri-ari entries.')
$out.Add(';; okuri-nasi entries.')

$kept = 0
foreach ($line in $lines) {
    if (-not $line -or $line.StartsWith(';')) { continue }

    $sp = $line.IndexOf(' ')
    if ($sp -lt 1) { continue }
    $key  = $line.Substring(0, $sp)
    $body = $line.Substring($sp + 1)

    # hiragana + chouonpu only — spelled with \u escapes because PS 5.1
    # reads BOM-less UTF-8 scripts as ANSI and would mangle kana literals.
    if ($key -notmatch ('^[' + [string][char]0x3041 + '-' + [string][char]0x3096 + [string][char]0x30FC + ']+$')) { continue }
    if (-not $body.StartsWith('/'))    { continue }

    $cands = New-Object System.Collections.Generic.List[string]
    foreach ($raw in $body.Split('/')) {
        if (-not $raw) { continue }
        $cand = $raw
        $semi = $cand.IndexOf(';')
        if ($semi -ge 0) { $cand = $cand.Substring(0, $semi) }
        if (-not $cand) { continue }
        $cand = Add-EmojiPresentation $cand
        if (-not $cands.Contains($cand)) { $cands.Add($cand) }
    }
    if ($cands.Count -eq 0) { continue }

    $out.Add(($key + ' /' + ($cands -join '/') + '/'))
    $kept++
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutFile, (($out -join "`n") + "`n"), $utf8NoBom)

Write-Host ("Wrote {0} entries to {1} ({2:N0} bytes)" -f $kept, $OutFile, (Get-Item $OutFile).Length)
