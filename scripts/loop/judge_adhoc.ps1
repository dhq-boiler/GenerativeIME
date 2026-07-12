# judge_adhoc.ps1 — 手選びの (reading, expected, got) 三つ組を e4b bias-safe で判定
#
# .claude/state/last_e2e_run.json に載っていない候補や、その中の特定サブセットを
# ピンポイントで判定したい場合に使う。判定ロジックは judge_bias_check.ps1 と同じ。
#
# Usage: 引数として TSV 形式の文字列 (reading `t expected `t got) を並べる
#   .\scripts\loop\judge_adhoc.ps1 -Rows @(
#     "しゅ`t種`t腫",
#     "ち`t地`t血"
#   )

param(
    [string[]]$Rows,
    [string]$Model      = 'gemma3n:e4b',
    [string]$OllamaUrl  = 'http://localhost:11434',
    [int]$TimeoutSec    = 60
)

$ErrorActionPreference = 'Stop'

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

Write-Host "[adhoc] model:  $Model"
foreach ($r in $Rows) {
    $c = $r -split "`t"
    if ($c.Length -lt 3) { Write-Host "  SKIP  bad row: $r"; continue }
    $reading = $c[0]; $expected = $c[1]; $got = $c[2]

    $a = AskOllama $reading @($expected, $got)
    $b = AskOllama $reading @($got, $expected)

    $mark = if ($a -eq $b -and $a -eq $expected) { 'STABLE-EXP' }
            elseif ($a -eq $b -and $a -eq $got)  { 'STABLE-GOT' }
            elseif ($a -eq $expected -and $b -eq $got) { 'FIRST-BIAS' }
            else { 'MIXED' }

    if ($null -eq $a) { $aDisp = '?' } else { $aDisp = $a }
    if ($null -eq $b) { $bDisp = '?' } else { $bDisp = $b }
    $line = "  {0,-11} reading={1,-8} expected={2}  got={3}   A->{4}  B->{5}" `
              -f $mark, $reading, $expected, $got, $aDisp, $bDisp
    Write-Host $line
}
