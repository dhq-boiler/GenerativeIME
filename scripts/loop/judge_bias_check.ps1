# judge_bias_check.ps1 — judge の position bias 検定
#
# .claude/state/last_e2e_run.json の misses について、候補順を
#   (A) expected / got  の順
#   (B) got / expected  の順
# の両方で Ollama に問い合わせ、A と B で選択が変わったかを見る。
#
#   同じ選択が返る → その項目については判定が安定 (bias でない)
#   選択が入れ替わる → position bias の疑いが強い (先頭を選んだだけ)
#
# 元の judge の verdict は「両方で expected を選んだときのみ REAL_MISS」に
# 引き締められる。
#
# Usage:
#   .\scripts\loop\judge_bias_check.ps1 -Model gemma4:12b

param(
    [string]$Model      = 'gemma4:12b',
    [string]$OllamaUrl  = 'http://localhost:11434',
    [int]$TimeoutSec    = 180,
    [string]$OutSuffix  = ''     # 並行実行や履歴保持用。'e4b' なら last_judge_bias.e4b.json
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$inFile  = Join-Path $root '.claude\state\last_e2e_run.json'
$suffix  = if ([string]::IsNullOrEmpty($OutSuffix)) { '' } else { ".$OutSuffix" }
$outFile = Join-Path $root ".claude\state\last_judge_bias${suffix}.json"

$run    = Get-Content $inFile -Raw -Encoding UTF8 | ConvertFrom-Json
$misses = @($run.corpus_misses)

Write-Host "[bias] model:  $Model"
Write-Host "[bias] misses: $($misses.Count)"

function AskOllama($reading, $ordered) {
    $prompt = "日本語の読み「$reading」を漢字かな交じりに変換するとき、最も自然で一般的な表記を候補から1つ選び、その表記だけを答えてください。候補: " +
              ($ordered -join ' / ')
    $req = @{
        model    = $Model
        stream   = $false
        options  = @{ temperature = 0; num_predict = 64 }
        messages = @(@{ role = 'user'; content = $prompt })
    }
    if ($Model -match 'qwen|gemma') { $req.think = $false }
    $body = $req | ConvertTo-Json -Depth 8
    try {
        $resp = Invoke-RestMethod -Uri "$OllamaUrl/api/chat" -Method Post `
                    -Body ([System.Text.Encoding]::UTF8.GetBytes($body)) `
                    -ContentType 'application/json' -TimeoutSec $TimeoutSec
        $content = [string]$resp.message.content
        $hits = @($ordered | Where-Object { $content.Contains($_) })
        if ($hits.Count -eq 1) { return $hits[0] }
        $longest = $hits | Sort-Object Length -Descending | Select-Object -First 1
        if ($null -ne $longest -and
            @($hits | Where-Object { $_.Length -eq $longest.Length }).Count -eq 1) {
            return $longest
        }
        return $null
    } catch { return $null }
}

$rows      = New-Object System.Collections.ArrayList
$firstBias = 0    # A と B で選択が入れ替わり、両方とも「先頭」を選んだ回数
$stable    = 0    # A と B で同じ選択

foreach ($m in $misses) {
    $a = AskOllama $m.reading @($m.expected, $m.got)
    $b = AskOllama $m.reading @($m.got, $m.expected)

    $isStable = ($a -eq $b)
    $isFirstBias = ((-not $isStable) -and $a -eq $m.expected -and $b -eq $m.got)

    if ($isStable) { $stable++ }
    if ($isFirstBias) { $firstBias++ }

    $mark = if ($isStable) { 'STABLE' }
            elseif ($isFirstBias) { 'FIRST-BIAS' }
            else { 'MIXED' }

    if ($null -eq $a) { $aDisp = '?' } else { $aDisp = $a }
    if ($null -eq $b) { $bDisp = '?' } else { $bDisp = $b }
    $line = "  {0,-11} reading={1,-8} expected={2}  got={3}   A->{4}  B->{5}" `
              -f $mark, $m.reading, $m.expected, $m.got, $aDisp, $bDisp
    Write-Host $line

    [void]$rows.Add([pscustomobject]@{
        reading      = $m.reading
        expected     = $m.expected
        got          = $m.got
        choice_A     = $a
        choice_B     = $b
        stable       = $isStable
        first_bias   = $isFirstBias
    })
}

$out = [ordered]@{
    iso_time    = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    model       = $Model
    total       = $misses.Count
    stable      = $stable
    first_bias  = $firstBias
    rows        = @($rows)
}
$out | ConvertTo-Json -Depth 6 | Out-File -FilePath $outFile -Encoding utf8

Write-Host ""
Write-Host "[bias] STABLE:     $stable  (順序に依存しない項目)" -ForegroundColor Green
Write-Host "[bias] FIRST-BIAS: $firstBias  (毎回先頭を選ぶ症状)" -ForegroundColor $(if ($firstBias -gt 0) { 'Red' } else { 'Green' })
Write-Host "[bias] out:        $outFile"
