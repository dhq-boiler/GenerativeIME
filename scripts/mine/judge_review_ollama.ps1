# judge_review_ollama.ps1 - conj_review.tsv の Ollama 下読み (Phase 0-3 方式)
#
# conjgap_miner が REVIEW に分類した「現在の変換結果も正当な競合解釈」の
# 各行について、ローカル Ollama に「この読みの最も自然な表記はどれか」を
# 選ばせ、裁定案を conj_review_judged.tsv に出力する。
#   ADD    <表記>  : 期待表記が現在の変換より自然 → 辞書追加候補
#   KEEP           : 現在の変換のほうが自然 → 現状維持
#   UNSURE         : モデルが期待/現在以外を答えた・応答不正 → 人手判断
#
# あくまで下読み。ADD をそのまま辞書化せず、人が確認してから反映する。
# 途中中断しても再実行すれば処理済み読みはスキップされる (追記モード)。
#
# Usage: .\judge_review_ollama.ps1 [-Limit N] [-Model qwen3.5:0.8b]
param(
    [int]$Limit = 0,
    [string]$Model = 'qwen3.5:0.8b',
    [string]$OllamaUrl = 'http://localhost:11434'
)

$ErrorActionPreference = 'Stop'

$here    = $PSScriptRoot
$inFile  = Join-Path $here 'out\conj_review.tsv'
$outFile = Join-Path $here 'out\conj_review_judged.tsv'

if (-not (Test-Path $inFile)) { throw "not found: $inFile" }

# 処理済み読みの集合 (再開用)
$done = @{}
if (Test-Path $outFile) {
    foreach ($l in Get-Content $outFile -Encoding UTF8) {
        $c = $l -split "`t"
        if ($c.Length -ge 3) { $done[$c[2]] = $true }
    }
}

# qwen3.5 は思考型モデルなので think=false が必須 (思考が全トークンを食って
# content が空になる)。format スキーマはこのモデル+think=false 経路では
# 強制されない実測だったため、返答は「候補文字列がちょうど1つ含まれるか」
# の寛容一致で解釈する。0か2つ以上マッチ → UNSURE。
function Judge($reading, $current, $expectedList) {
    $options = @($current) + $expectedList | Select-Object -Unique
    $prompt = "日本語の読み「$reading」を漢字かな交じりに変換するとき、最も自然で一般的な表記を候補から1つ選び、その表記だけを答えてください。候補: " +
              ($options -join ' / ')

    $req = @{
        model    = $Model
        stream   = $false
        options  = @{ temperature = 0; num_predict = 64 }
        messages = @(@{ role = 'user'; content = $prompt })
    }
    # qwen3.5 / gemma4 は思考型: think を切らないと思考が全トークンを
    # 食って content が空になる (num_predict は思考分も含む)。非思考型
    # モデルに think を渡すとエラーになるので、モデル名で分岐する。
    if ($Model -match 'qwen|gemma') { $req.think = $false }
    $body = $req | ConvertTo-Json -Depth 8

    try {
        $resp = Invoke-RestMethod -Uri "$OllamaUrl/api/chat" -Method Post `
                    -Body ([System.Text.Encoding]::UTF8.GetBytes($body)) `
                    -ContentType 'application/json' -TimeoutSec 60
        $content = [string]$resp.message.content
        if ([string]::IsNullOrWhiteSpace($content)) { return $null }
        $hits = @($options | Where-Object { $content.Contains($_) })
        if ($hits.Count -eq 1) { return $hits[0] }
        # 片方が他方の部分文字列 (説いた ⊂ 〜説いた等はないが 積んだ/詰んだ
        # 型の共通接尾はある) の場合、最長一致が唯一なら採用。
        $longest = $hits | Sort-Object Length -Descending | Select-Object -First 1
        if ($null -ne $longest -and
            @($hits | Where-Object { $_.Length -eq $longest.Length }).Count -eq 1) {
            return $longest
        }
        return $null
    } catch {
        return $null
    }
}

$rows = Get-Content $inFile -Encoding UTF8
if ($Limit -gt 0) { $rows = $rows | Select-Object -First $Limit }

$n = 0; $add = 0; $keep = 0; $unsure = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
foreach ($line in $rows) {
    $c = $line -split "`t"
    if ($c.Length -lt 4) { continue }
    $reading = $c[1]; $current = $c[2]
    $expected = $c[3].Trim('/') -split '/'
    if ($done.ContainsKey($reading)) { continue }

    $best = Judge $reading $current $expected
    $n++

    if ($null -eq $best) {
        $verdict = 'UNSURE'; $pick = ''
        $unsure++
    } elseif ($best -eq $current) {
        $verdict = 'KEEP'; $pick = $current
        $keep++
    } elseif ($expected -contains $best) {
        $verdict = 'ADD'; $pick = $best
        $add++
    } else {
        $verdict = 'UNSURE'; $pick = $best
        $unsure++
    }

    "$verdict`t$pick`t$reading`t$current`t$($c[3])" |
        Add-Content $outFile -Encoding UTF8

    if ($n % 100 -eq 0) {
        $rate = $n / $sw.Elapsed.TotalSeconds
        Write-Host ("{0} judged (ADD={1} KEEP={2} UNSURE={3}, {4:N1}/s)" -f $n, $add, $keep, $unsure, $rate)
    }
}
$sw.Stop()
Write-Host ("done: {0} judged in {1:N0}s  ADD={2} KEEP={3} UNSURE={4}" -f `
    $n, $sw.Elapsed.TotalSeconds, $add, $keep, $unsure)
