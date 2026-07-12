# judge_misses.ps1 — .claude/state/last_e2e_run.json の corpus miss を Ollama で採点
#
# CLAUDE.md 定義のループで「Verification」を担う。ローカル Ollama に
# 「この読みで最も自然な表記は expected か got か」を選ばせ、
#   expected を選んだ  → REAL_MISS   (現在の変換は誤り、修正すべき)
#   got を選んだ       → AMBIGUOUS   (両方自然、修正しなくてよい)
#   どちらでもない/沈黙 → UNSURE      (人手判断)
# を出力する。
#
# 既存の scripts/mine/judge_review_ollama.ps1 と同じ prompt / 同じ
# think=false / 同じ寛容一致ロジックを使う。
#
# PS 5.1 の UTF-8 罠に注意: body は必ず UTF-8 バイトで送る。
# (memory: ollama-utf8-body-encoding.md)
#
# Usage:
#   .\scripts\loop\judge_misses.ps1
#   .\scripts\loop\judge_misses.ps1 -Model gemma4:12b

param(
    [string]$Model      = 'gemma3n:e4b',
    [string]$OllamaUrl  = 'http://localhost:11434',
    [int]$TimeoutSec    = 60
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$inFile    = Join-Path $root '.claude\state\last_e2e_run.json'
$outFile   = Join-Path $root '.claude\state\last_judge_run.json'

if (-not (Test-Path $inFile)) { throw "not found: $inFile — run run_e2e_loop.ps1 first" }

$run = Get-Content $inFile -Raw -Encoding UTF8 | ConvertFrom-Json
$misses = @($run.corpus_misses)
if ($misses.Count -eq 0) {
    Write-Host "[judge] no corpus misses to score" -ForegroundColor Green
    return
}

Write-Host "[judge] model:  $Model"
Write-Host "[judge] misses: $($misses.Count)"

function Judge($reading, $expected, $got) {
    $options = @($expected, $got) | Select-Object -Unique
    $prompt  = "日本語の読み「$reading」を漢字かな交じりに変換するとき、最も自然で一般的な表記を候補から1つ選び、その表記だけを答えてください。候補: " +
               ($options -join ' / ')

    $req = @{
        model    = $Model
        stream   = $false
        options  = @{ temperature = 0; num_predict = 64 }
        messages = @(@{ role = 'user'; content = $prompt })
    }
    # gemma / qwen は思考型なので think=false 必須。
    if ($Model -match 'qwen|gemma') { $req.think = $false }

    $body = $req | ConvertTo-Json -Depth 8

    try {
        $resp = Invoke-RestMethod -Uri "$OllamaUrl/api/chat" -Method Post `
                    -Body ([System.Text.Encoding]::UTF8.GetBytes($body)) `
                    -ContentType 'application/json' -TimeoutSec $TimeoutSec
        $content = [string]$resp.message.content
        if ([string]::IsNullOrWhiteSpace($content)) {
            return [pscustomobject]@{ choice = $null; raw = '' }
        }
        $hits = @($options | Where-Object { $content.Contains($_) })
        if ($hits.Count -eq 1) {
            return [pscustomobject]@{ choice = $hits[0]; raw = $content.Trim() }
        }
        # 両方含む or 片方が他方の部分文字列: 長い一致がユニークなら採用
        $longest = $hits | Sort-Object Length -Descending | Select-Object -First 1
        if ($null -ne $longest -and
            @($hits | Where-Object { $_.Length -eq $longest.Length }).Count -eq 1) {
            return [pscustomobject]@{ choice = $longest; raw = $content.Trim() }
        }
        return [pscustomobject]@{ choice = $null; raw = $content.Trim() }
    } catch {
        return [pscustomobject]@{ choice = $null; raw = "ERROR: $_" }
    }
}

$judged = New-Object System.Collections.ArrayList
$counts = @{ REAL_MISS = 0; AMBIGUOUS = 0; UNSURE = 0 }

foreach ($m in $misses) {
    $r = Judge $m.reading $m.expected $m.got
    $verdict = 'UNSURE'
    if     ($r.choice -eq $m.expected) { $verdict = 'REAL_MISS' }
    elseif ($r.choice -eq $m.got)      { $verdict = 'AMBIGUOUS' }
    $counts[$verdict]++

    # PS 5.1 は ?? 演算子なし。素朴に分岐。
    if ($null -eq $r.choice) {
        $line = "  {0,-9} reading={1,-8} expected={2}  got={3}   model->{4}" -f $verdict, $m.reading, $m.expected, $m.got, '<null>'
    } else {
        $line = "  {0,-9} reading={1,-8} expected={2}  got={3}   model->{4}" -f $verdict, $m.reading, $m.expected, $m.got, $r.choice
    }
    Write-Host $line

    [void]$judged.Add([pscustomobject]@{
        reading   = $m.reading
        expected  = $m.expected
        got       = $m.got
        verdict   = $verdict
        model_choice = $r.choice
        model_raw = $r.raw
    })
}

$out = [ordered]@{
    iso_time      = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    source_run    = $run.iso_time
    model         = $Model
    total         = $misses.Count
    real_miss     = $counts.REAL_MISS
    ambiguous     = $counts.AMBIGUOUS
    unsure        = $counts.UNSURE
    judged        = @($judged)
}

$out | ConvertTo-Json -Depth 6 | Out-File -FilePath $outFile -Encoding utf8

Write-Host ""
Write-Host "[judge] REAL_MISS: $($counts.REAL_MISS)" -ForegroundColor $(if ($counts.REAL_MISS -gt 0) { 'Yellow' } else { 'Green' })
Write-Host "[judge] AMBIGUOUS: $($counts.AMBIGUOUS)"
Write-Host "[judge] UNSURE:    $($counts.UNSURE)"
Write-Host "[judge] out:       $outFile"
