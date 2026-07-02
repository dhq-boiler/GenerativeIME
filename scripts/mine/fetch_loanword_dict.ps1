# Distill JMdict (EDRDG, CC BY-SA 4.0) into a katakana-loanword -> original
# English word SKK dictionary: third_party/skk/SKK-JISYO.loanwords.utf8.
#
# For every JMdict entry whose reading is pure katakana (+ chouonpu), emit
#
#   <hiragana reading> /<katakana>/<word>/<Word>/<WORD>/
#
# so じぇねれーてぃぶ-style inputs offer the original spelling in three
# casings right after the katakana form. Entries are merged per reading;
# within-entry duplicates are dropped.
#
# The English word comes from two places:
#   1. <lsource>: authoritative, but rare — JMdict only spells out the
#      source when it isn't obvious (a few hundred entries). No xml:lang
#      attribute means English (DTD default); explicit non-eng languages
#      and wasei (ls_wasei="y") coinages are skipped.
#   2. First-sense single-word <gloss> of a katakana-ONLY entry (no
#      <keb>): for true loanwords the first gloss IS the source word
#      (コンピューター -> "computer"). To keep translations that are not
#      transliterations out (ダントツ -> "unrivaled"), a first-sound gate
#      checks that the English initial letter is phonetically compatible
#      with the reading's first kana. lsource words bypass the gate.
#
# Readings that collide with Japanese function words (です -> デス "death",
# ます -> マス "trout", さん -> サン "sun", …) are blocklisted: as direct
# entries they would both pollute the candidate list for extremely common
# grammar and disable the okuri-ari garbage filter for that reading
# (HasDirectEntry bypasses the MeCab ReadsAs check in the IME).
#
# The official JMdict distribution is one-XML-element-per-line, which lets
# this script stream it with cheap line regexes instead of a DOM parse
# (the DOM route takes minutes in PowerShell for the ~200k entries).
#
# Idempotent: regenerates the output from scratch on every run.

param(
    [string]$Url     = 'http://ftp.edrdg.org/pub/Nihongo/JMdict_e.gz',
    [string]$Source  = '',   # local JMdict_e.gz; skips download when set
    [string]$OutFile = (Join-Path $PSScriptRoot '..\..\third_party\skk\SKK-JISYO.loanwords.utf8')
)

$ErrorActionPreference = 'Stop'

if (-not $Source) {
    $Source = Join-Path $env:TEMP 'JMdict_e.gz'
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Source -UseBasicParsing
}

# katakana (U+30A1..U+30F6) + chouonpu only; converted to hiragana by -0x60.
function KataToHira([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $s.ToCharArray()) {
        $cp = [int]$ch
        if ($cp -ge 0x30A1 -and $cp -le 0x30F6) { [void]$sb.Append([char]($cp - 0x60)) }
        elseif ($cp -eq 0x30FC)                 { [void]$sb.Append([char]$cp) }
        else { return $null }
    }
    $sb.ToString()
}

# First-sound gate: hiragana initial -> plausible English initial letters.
# Deliberately permissive (コ -> c/k/q covers computer, koala, queue); its
# job is only to reject flagrant translations like ダントツ -> "unrivaled".
$gate = @{}
foreach ($pair in @(
    @('a','a'), @('i','iye'), @('u','uwvo'), @('e','ea'), @('o','oau'),
    @('ka','ckqg'), @('ga','g'), @('sa','scz'), @('za','zjgx'),
    @('ta','tcz'), @('da','d'), @('na','n'), @('ha','fhpw'),
    @('ba','bv'), @('pa','p'), @('ma','m'), @('ya','yue'),
    @('ra','rl'), @('wa','wo'), @('vu','v')
)) { $gate[$pair[0]] = $pair[1] }
function GateLetters([char]$kana) {
    $cp = [int]$kana
    switch ($cp) {
        { $_ -in 0x3041,0x3042 } { return $gate['a'] }
        { $_ -in 0x3043,0x3044 } { return $gate['i'] }
        { $_ -in 0x3045,0x3046 } { return $gate['u'] }
        { $_ -in 0x3047,0x3048 } { return $gate['e'] }
        { $_ -in 0x3049,0x304A } { return $gate['o'] }
        { $_ -in 0x304B,0x304D,0x304F,0x3051,0x3053 } { return $gate['ka'] }
        { $_ -in 0x304C,0x304E,0x3050,0x3052,0x3054 } { return $gate['ga'] }
        { $_ -in 0x3055,0x3057,0x3059,0x305B,0x305D } { return $gate['sa'] }
        { $_ -in 0x3056,0x3058,0x305A,0x305C,0x305E } { return $gate['za'] }
        { $_ -in 0x305F,0x3061,0x3063,0x3064,0x3066,0x3068 } { return $gate['ta'] }
        { $_ -in 0x3060,0x3062,0x3065,0x3067,0x3069 } { return $gate['da'] }
        { $_ -in 0x306A,0x306B,0x306C,0x306D,0x306E } { return $gate['na'] }
        { $_ -in 0x306F,0x3072,0x3075,0x3078,0x307B } { return $gate['ha'] }
        { $_ -in 0x3070,0x3073,0x3076,0x3079,0x307C } { return $gate['ba'] }
        { $_ -in 0x3071,0x3074,0x3077,0x307A,0x307D } { return $gate['pa'] }
        { $_ -in 0x307E,0x307F,0x3080,0x3081,0x3082 } { return $gate['ma'] }
        { $_ -in 0x3083,0x3084,0x3085,0x3086,0x3087,0x3088 } { return $gate['ya'] }
        { $_ -in 0x3089,0x308A,0x308B,0x308C,0x308D } { return $gate['ra'] }
        { $_ -in 0x308E,0x308F,0x3090,0x3091,0x3092 } { return $gate['wa'] }
        { $_ -eq 0x3094 } { return $gate['vu'] }
        default { return '' }   # unknown initial: gate rejects
    }
}

# Hiragana readings that are overwhelmingly Japanese grammar when typed
# alone: copulas/auxiliaries, particles, formal nouns, honorifics,
# demonstratives. Katakana homophones of these are noise, not conversions.
$blocklist = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($w in @(
    'です','ます','でした','でしょ','ましょ','ません',
    'でも','まで','から','など','だけ','ばかり','ながら','けど','けれど',
    'って','ちゃ','じゃ','たり','たら','なら','ので','のに','かも','こそ',
    'さえ','しか','ずつ','より','ほど','くらい','ぐらい',
    'こと','もの','ため','わけ','はず','まま','とき','ところ','よう','そう',
    'みたい','つもり','ほう',
    'さん','ちゃん','くん','さま','どの','その','あの','この'
)) { [void]$blocklist.Add($w) }

$gz = New-Object System.IO.FileStream($Source, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read)
$unzip = New-Object System.IO.Compression.GZipStream($gz, [System.IO.Compression.CompressionMode]::Decompress)
$reader = New-Object System.IO.StreamReader($unzip, [System.Text.Encoding]::UTF8)

# reading -> ordered list of "katakana|word" pairs
$map = New-Object 'System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[string]]'

$rebs = New-Object System.Collections.Generic.List[string]
$lsWords = New-Object System.Collections.Generic.List[string]
$glossWords = New-Object System.Collections.Generic.List[string]
$hasKeb = $false
$senseIdx = 0
$entries = 0

while ($null -ne ($line = $reader.ReadLine())) {
    $t = $line.TrimStart()
    if ($t.StartsWith('<reb>')) {
        if ($t -match '^<reb>([^<]+)</reb>') { $rebs.Add($Matches[1]) }
    }
    elseif ($t.StartsWith('<keb>')) {
        $hasKeb = $true
    }
    elseif ($t.StartsWith('<sense>')) {
        $senseIdx++
    }
    elseif ($t.StartsWith('<lsource')) {
        if ($t -match '^<lsource([^>]*)>([^<]+)</lsource>') {
            $attrs = $Matches[1]; $src = $Matches[2]
            $isEng   = ($attrs -notmatch 'xml:lang=') -or ($attrs -match 'xml:lang="eng"')
            $isWasei = $attrs -match 'ls_wasei="y"'
            if ($isEng -and -not $isWasei -and $src -match '^[A-Za-z][A-Za-z-]+$') {
                $lsWords.Add($src)
            }
        }
    }
    elseif ($t.StartsWith('<gloss>')) {
        # first sense only: later senses drift semantically
        if ($senseIdx -eq 1 -and $t -match '^<gloss>([A-Za-z][A-Za-z-]+)</gloss>$') {
            $glossWords.Add($Matches[1])
        }
    }
    elseif ($t.StartsWith('</entry>')) {
        $entries++
        # lsource words always count; gloss words only for katakana-only
        # entries (no kanji headword) and only through the first-sound gate.
        foreach ($reb in $rebs) {
            $hira = KataToHira $reb
            if (-not $hira -or $hira.Length -lt 2) { continue }
            if ($blocklist.Contains($hira)) { continue }
            $words = New-Object System.Collections.Generic.List[string]
            foreach ($w in $lsWords) { $words.Add($w) }
            if (-not $hasKeb -and $glossWords.Count -gt 0) {
                $allowed = GateLetters $hira[0]
                foreach ($w in $glossWords) {
                    if ($allowed -and $allowed.Contains([string][char]::ToLowerInvariant($w[0]))) {
                        $words.Add($w)
                    }
                }
            }
            if ($words.Count -eq 0) { continue }
            if (-not $map.ContainsKey($hira)) {
                $map[$hira] = New-Object System.Collections.Generic.List[string]
            }
            foreach ($w in $words) { $map[$hira].Add("$reb|$w") }
        }
        $rebs.Clear(); $lsWords.Clear(); $glossWords.Clear()
        $hasKeb = $false; $senseIdx = 0
    }
}
$reader.Close()

Write-Host ("JMdict entries scanned: {0:N0}; readings with English source: {1:N0}" -f $entries, $map.Count)

function CasedForms([string]$w) {
    $lower = $w.ToLowerInvariant()
    $cap   = $lower.Substring(0,1).ToUpperInvariant() + $lower.Substring(1)
    $upper = $w.ToUpperInvariant()
    @($w, $lower, $cap, $upper) | Select-Object -Unique
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add(';; -*- mode: fundamental; coding: utf-8 -*-')
$out.Add(';; SKK-JISYO.loanwords - katakana loanword readings -> original English')
$out.Add(';; spellings, generated from JMdict by scripts/mine/fetch_loanword_dict.ps1.')
$out.Add(';;')
$out.Add(';; This file uses material from JMdict (http://www.edrdg.org/jmdict/j_jmdict.html),')
$out.Add(';; property of the Electronic Dictionary Research and Development Group,')
$out.Add(';; used in conformance with the Group''s licence (CC BY-SA 4.0).')
$out.Add(';; https://www.edrdg.org/edrdg/licence.html')
$out.Add(';;')
$out.Add(';; okuri-ari entries.')
$out.Add(';; okuri-nasi entries.')

$emitted = 0
foreach ($key in ($map.Keys | Sort-Object)) {
    $cands = New-Object System.Collections.Generic.List[string]
    foreach ($pair in $map[$key]) {
        $kata, $word = $pair.Split('|', 2)
        if (-not $cands.Contains($kata)) { [void]$cands.Add($kata) }
        foreach ($form in (CasedForms $word)) {
            if (-not $cands.Contains($form)) { [void]$cands.Add($form) }
        }
    }
    if ($cands.Count -lt 2) { continue }
    $out.Add(($key + ' /' + ($cands -join '/') + '/'))
    $emitted++
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutFile, (($out -join "`n") + "`n"), $utf8NoBom)

Write-Host ("Wrote {0:N0} entries to {1} ({2:N0} bytes)" -f $emitted, $OutFile, (Get-Item $OutFile).Length)
