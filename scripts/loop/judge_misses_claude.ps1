# judge_misses_claude.ps1 — .claude/state/last_e2e_run.json の corpus miss を
# `claude -p` (Claude Haiku) で採点。
#
# Ollama 版 (judge_misses.ps1) と対を成す。ローカル gemma4:12b は position bias
# が強すぎて単独では使えないため、こちらは常に A/B 両順序で問い合わせて
#   両方で expected を選択  → REAL_MISS  (確定)
#   両方で got を選択       → AMBIGUOUS  (両方自然と認めた)
#   ちぐはぐ                 → UNSURE     (bias 又はモデル不安定)
#   応答なし                 → UNSURE
# と厳しめに判定する。
#
# コスト目安 (Haiku, 2026-07 実測): ~$0.03/コール。1 miss = 2 コール = ~$0.06。
# 実行時にコスト合計を最後に表示するので、暴走時はすぐ気付けるはず。
# それでも「夜間に空回り」を防ぐため、-Limit で採点件数の上限を必ず設定できる。
#
# 参考メモリ: ollama-judge-position-bias.md
#
# Usage:
#   .\scripts\loop\judge_misses_claude.ps1
#   .\scripts\loop\judge_misses_claude.ps1 -Limit 3          # 最初の 3 件だけ
#   .\scripts\loop\judge_misses_claude.ps1 -Model sonnet     # デフォルトは haiku

param(
    [string]$Model     = 'haiku',
    [int]$Limit        = 0,
    [int]$TimeoutSec   = 120
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$inFile  = Join-Path $root '.claude\state\last_e2e_run.json'
$outFile = Join-Path $root '.claude\state\last_judge_claude.json'

if (-not (Test-Path $inFile)) { throw "not found: $inFile — run run_e2e_loop.ps1 first" }

$run    = Get-Content $inFile -Raw -Encoding UTF8 | ConvertFrom-Json
$misses = @($run.corpus_misses)
if ($Limit -gt 0) { $misses = $misses | Select-Object -First $Limit }

if ($misses.Count -eq 0) {
    Write-Host "[claude-judge] no corpus misses to score" -ForegroundColor Green
    return
}

Write-Host "[claude-judge] model:  $Model"
Write-Host "[claude-judge] misses: $($misses.Count)  (queries = misses x 2)"

function AskClaude($reading, $ordered) {
    $prompt = "日本語の読み「$reading」を漢字かな交じりに変換するとき、最も自然で一般的な表記を候補から1つ選び、その表記だけを答えてください。候補: " +
              ($ordered -join ' / ')
    # claude -p は stdin を 3s 待つ癖があるので空文字列を pipe して即進める。
    # 2>$null で stderr の warning を落とすが、native command なので memory
    # (ollama-utf8-body-encoding は無関係) — ここでは終了コードを気にしない。
    $raw = "" | & claude -p $prompt --output-format json --model $Model 2>$null
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return [pscustomobject]@{ choice = $null; cost = 0.0; raw = '' }
    }
    try {
        $obj = $raw | ConvertFrom-Json
    } catch {
        return [pscustomobject]@{ choice = $null; cost = 0.0; raw = $raw }
    }
    $content = [string]$obj.result
    $cost    = [double]($obj.total_cost_usd)
    $hits = @($ordered | Where-Object { $content.Contains($_) })
    if ($hits.Count -eq 1) {
        return [pscustomobject]@{ choice = $hits[0]; cost = $cost; raw = $content }
    }
    $longest = $hits | Sort-Object Length -Descending | Select-Object -First 1
    if ($null -ne $longest -and
        @($hits | Where-Object { $_.Length -eq $longest.Length }).Count -eq 1) {
        return [pscustomobject]@{ choice = $longest; cost = $cost; raw = $content }
    }
    return [pscustomobject]@{ choice = $null; cost = $cost; raw = $content }
}

$judged  = New-Object System.Collections.ArrayList
$counts  = @{ REAL_MISS = 0; AMBIGUOUS = 0; UNSURE = 0 }
$costSum = 0.0

foreach ($m in $misses) {
    $a = AskClaude $m.reading @($m.expected, $m.got)
    $b = AskClaude $m.reading @($m.got, $m.expected)
    $costSum += $a.cost + $b.cost

    $verdict = 'UNSURE'
    if ($null -ne $a.choice -and $null -ne $b.choice) {
        if     ($a.choice -eq $m.expected -and $b.choice -eq $m.expected) { $verdict = 'REAL_MISS' }
        elseif ($a.choice -eq $m.got      -and $b.choice -eq $m.got)      { $verdict = 'AMBIGUOUS' }
    }
    $counts[$verdict]++

    if ($null -eq $a.choice) { $aDisp = '?' } else { $aDisp = $a.choice }
    if ($null -eq $b.choice) { $bDisp = '?' } else { $bDisp = $b.choice }
    $line = "  {0,-9} reading={1,-8} expected={2}  got={3}   A->{4}  B->{5}" `
              -f $verdict, $m.reading, $m.expected, $m.got, $aDisp, $bDisp
    Write-Host $line

    [void]$judged.Add([pscustomobject]@{
        reading    = $m.reading
        expected   = $m.expected
        got        = $m.got
        verdict    = $verdict
        choice_A   = $a.choice
        choice_B   = $b.choice
        cost_usd_A = $a.cost
        cost_usd_B = $b.cost
    })
}

$out = [ordered]@{
    iso_time      = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    source_run    = $run.iso_time
    judge         = 'claude'
    model         = $Model
    total         = $misses.Count
    real_miss     = $counts.REAL_MISS
    ambiguous     = $counts.AMBIGUOUS
    unsure        = $counts.UNSURE
    total_cost_usd = [math]::Round($costSum, 4)
    judged        = @($judged)
}

$out | ConvertTo-Json -Depth 6 | Out-File -FilePath $outFile -Encoding utf8

Write-Host ""
Write-Host "[claude-judge] REAL_MISS: $($counts.REAL_MISS)" -ForegroundColor $(if ($counts.REAL_MISS -gt 0) { 'Yellow' } else { 'Green' })
Write-Host "[claude-judge] AMBIGUOUS: $($counts.AMBIGUOUS)"
Write-Host "[claude-judge] UNSURE:    $($counts.UNSURE)"
Write-Host ("[claude-judge] cost:      `${0}" -f ([math]::Round($costSum, 4)))
Write-Host "[claude-judge] out:       $outFile"
