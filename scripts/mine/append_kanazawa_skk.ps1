# append_kanazawa_skk.ps1 - Convert corpus/medical/kanazawa-triples.tsv
# into SKK direct entries and append to SKK-JISYO.L.utf8. Each row emits:
#   reading /Japanese/English/
# The Latin combining form is preserved as a header comment above the
# section so a maintainer can look up why an entry is here.
#
# Duplicate protection: entries whose exact "reading /japanese/" line
# already appears in SKK-JISYO.L are skipped so re-runs are idempotent.
#
# MeCab misreadings that slipped past the auto-flow (空腸 → そらちょう
# instead of くうちょう, S状結腸 losing the S prefix) get patched via an
# override map at the bottom of this file. Grow the map as review turns
# up more.

[CmdletBinding()]
param(
    [string]$InTsv = (Join-Path $PSScriptRoot '..\..\corpus\medical\kanazawa-triples.tsv'),
    [string]$SkkFile = (Join-Path $PSScriptRoot '..\..\third_party\skk\SKK-JISYO.L.utf8')
)

$ErrorActionPreference = 'Stop'

# Reading fixes for the specific misparses observed on this corpus.
# key = Japanese surface, value = correct hiragana reading.
$readingFixes = @{
    '空腸'      = 'くうちょう'
    'S状結腸'   = 'えすじょうけっちょう'
    '角膜'      = 'かくまく'
    '虹彩'      = 'こうさい'
    '網膜'      = 'もうまく'
    '毛様体'    = 'もうようたい'
    '結膜'      = 'けつまく'
    '眼瞼'      = 'がんけん'
    '鼓膜'      = 'こまく'
    '涙嚢'      = 'るいのう'
    '甲状腺'    = 'こうじょうせん'
    '卵管'      = 'らんかん'
    '骨盤'      = 'こつばん'
    '仙骨'      = 'せんこつ'
    '恥骨'      = 'ちこつ'
    '腸骨'      = 'ちょうこつ'
    '坐骨'      = 'ざこつ'
    '踵骨'      = 'しょうこつ'
    '距骨'      = 'きょこつ'
    '肩甲骨'    = 'けんこうこつ'
    '肩峰'      = 'けんぽう'
    '手根骨'    = 'しゅこんこつ'
    '指節骨'    = 'しせつこつ'
    '軟骨'      = 'なんこつ'
    '毛細血管'  = 'もうさいけっかん'
    '血栓'      = 'けっせん'
    '芽細胞'    = 'がさいぼう'
    '髄膜'      = 'ずいまく'
    '脊髄'      = 'せきずい'
    '大脳'      = 'だいのう'
    '小脳'      = 'しょうのう'
    '扁桃'      = 'へんとう'
    '横隔膜'    = 'おうかくまく'
    '胸膜'      = 'きょうまく'
    '膵臓'      = 'すいぞう'
    '胆嚢'      = 'たんのう'
    '上腕骨'    = 'じょうわんこつ'
    '尺骨'      = 'しゃっこつ'
    '大腿骨'    = 'だいたいこつ'
    '橈骨'      = 'とうこつ'
    '腓骨'      = 'ひこつ'
    '膝蓋骨'    = 'しつがいこつ'
    '椎骨'      = 'ついこつ'
    '肋骨'      = 'ろっこつ'
    '胸骨'      = 'きょうこつ'
    '鎖骨'      = 'さこつ'
    '頭蓋骨'    = 'ずがいこつ'
    '強膜'      = 'きょうまく'
    '側腹部'    = 'そくふくぶ'
}

# Rows whose Japanese cell is really a descriptor phrase, not a term
# the user would type. Kept out of SKK entirely.
$dropJapanese = @(
    '腹側の', '尾へ', '前方の', '後方の', '外側の', '腹側', '尾へ（下方へ）',
    '軍艦'
)

# Existing SKK entries index so we don't emit duplicates.
$existing = @{}
foreach ($line in Get-Content $SkkFile -Encoding UTF8) {
    if ($line.Length -eq 0 -or $line[0] -eq ';') { continue }
    $sp = $line.IndexOf(' ')
    if ($sp -lt 1) { continue }
    $reading = $line.Substring(0, $sp)
    $existing[$reading] = $line.Substring($sp + 1)
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("")
[void]$sb.AppendLine(";; -- Kanazawa Medical U vocabulary (2026-07-02) auto-appended by")
[void]$sb.AppendLine(";;    scripts/mine/append_kanazawa_skk.ps1 from")
[void]$sb.AppendLine(";;    corpus/medical/kanazawa-triples.tsv (kt.kanazawa-med.ac.jp/english/2-1.pdf)")
[void]$sb.AppendLine(";;    Each entry: reading /日本語/English/  Latin combining form kept as")
[void]$sb.AppendLine(";;    a comment header per section for reference.")

$emitted = 0
$skipped = 0
$currentSection = ''
foreach ($row in Get-Content $InTsv -Encoding UTF8) {
    if ($row.Length -eq 0 -or $row[0] -eq '#') { continue }
    $c = $row -split "`t"
    if ($c.Count -lt 4) { continue }
    $reading = $c[0]
    $jp      = $c[1]
    $en      = $c[2]
    $latin   = $c[3]
    $section = if ($c.Count -ge 5) { $c[4] } else { '' }

    if ($section -ne $currentSection) {
        [void]$sb.AppendLine("")
        [void]$sb.AppendLine((";; ---- {0} ----" -f $section))
        $currentSection = $section
    }

    # Drop descriptor / directional forms that would clutter conversion
    # (「前方の」「腹側の」etc. -- the actual medical noun is「前方」/「腹側」
    # which is what the base SKK dict covers, and putting an entry that
    # ends in「の」would offer a candidate the user rarely wants).
    if ($dropJapanese -contains $jp) { continue }
    if ($jp.EndsWith('の')) { continue }
    if ($jp -match '^[尾]へ$') { continue }

    # Apply reading override if we have one.
    if ($readingFixes.ContainsKey($jp)) {
        $reading = $readingFixes[$jp]
    }
    if ([string]::IsNullOrEmpty($reading)) { continue }
    # Reading must be pure hiragana + optional 「ー」 to be a valid SKK key.
    if (-not ($reading -match '^[぀-ゟー]+$')) { continue }
    # Also require Japanese term contains a kanji (not just hiragana).
    if (-not ($jp -match '[一-鿿]')) { continue }

    # Build candidate list. English first-appearance dedupe (Japanese too).
    $cands = @($jp)
    if ($en -and ($cands -notcontains $en)) { $cands += $en }
    $candStr = ($cands -join '/')

    # Skip if the SKK reading is already present AND already contains
    # this exact Japanese candidate.
    if ($existing.ContainsKey($reading)) {
        if ($existing[$reading] -match [regex]::Escape('/' + $jp + '/')) {
            $skipped += 1
            continue
        }
    }

    [void]$sb.AppendLine(("{0} /{1}/  ;; {2}" -f $reading, $candStr, $latin))
    $emitted += 1
}

Add-Content -Path $SkkFile -Value $sb.ToString() -Encoding UTF8 -NoNewline
Write-Host ("appended {0} entries, skipped {1} duplicates" -f $emitted, $skipped)
Write-Host ("SKK-JISYO.L.utf8 now: {0:N0} bytes" -f (Get-Item $SkkFile).Length)
