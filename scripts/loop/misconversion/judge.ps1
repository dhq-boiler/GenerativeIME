# judge.ps1 — scan.ps1 の出力を Claude Haiku で判定
#
# corpus 回帰と違い open-ended (「正しかったはずの入力は何」を推測)。
# 選択肢が事前に無いので、選ばせる形の Ollama e4b は向かない。
# Haiku に JSON で {intended, confidence, category, rationale} を返させる。
#
# Categories:
#   reading_top   — reading→top-kanji 差替 (modernranking の kOverrideTable 追加候補)
#   user_dict     — 未登録語 / 固有名詞 (%APPDATA%\GenerativeIME\dict に .utf8 追加)
#   bunsetsu      — 文節境界誤り (bunsetsu.cpp head-priority override 追加候補)
#   complex       — 人間判断が必要
#
# コスト目安: Haiku ~$0.03/コール、5 misconversions で ~$0.15。
#
# Usage:
#   .\scripts\loop\misconversion\judge.ps1
#   .\scripts\loop\misconversion\judge.ps1 -Limit 3

param(
    [int]$Limit       = 0,
    [string]$Model    = 'haiku',
    [int]$TimeoutSec  = 120
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$inFile    = Join-Path $root '.claude\state\misconversion_scan.json'
$outFile   = Join-Path $root '.claude\state\misconversion_judge.json'

if (-not (Test-Path $inFile)) { throw "not found: $inFile — run scan.ps1 first" }

$scan   = Get-Content $inFile -Raw -Encoding UTF8 | ConvertFrom-Json
$items  = @($scan.misconversions)
if ($Limit -gt 0) { $items = $items | Select-Object -First $Limit }

Write-Host "[judge] model:  $Model"
Write-Host "[judge] items:  $($items.Count)"

function Ask($m) {
    $reading = if ([string]::IsNullOrWhiteSpace($m.forgetReading)) { '（記録なし）' } else { $m.forgetReading }
    # context は Ctrl+F5 連打で複数エントリの committed 文字列が積み上がり
    # ノイズになるので、末尾 40 文字だけ渡す。判定への影響を抑える。
    $context = if ([string]::IsNullOrWhiteSpace($m.context)) { '（なし）' } else {
        if ($m.context.Length -gt 40) { '…' + $m.context.Substring($m.context.Length - 40) } else { $m.context }
    }
    # 候補窓に出ていた別解も渡す。読み逆算のヒントになる (例: 唖ました の
    # 隣に カタカナ オシマシタ が出ていれば 押しました の可能性が高い)。
    $cands = if ([string]::IsNullOrWhiteSpace($m.candidates)) { '（なし）' } else {
        # 長すぎ (単字候補が延々並ぶことがある) なら 200 字で切る
        if ($m.candidates.Length -gt 200) { $m.candidates.Substring(0, 200) + '…' } else { $m.candidates }
    }

    $prompt = @"
GenerativeIME (日本語 IME) で以下の誤変換が Ctrl+F5 で記録された。

- 誤って確定された文字列: 「$($m.lastCommitted)」
- 打っていた読み: 「$reading」
- 候補窓の別解: $cands
- 直前 context (末尾 40 字): 「$context」

ユーザーが本来入力したかった正しい表記を推測し、以下の 5 フィールドをすべて含む JSON オブジェクトを 1 つだけ返す。

フィールド名は変更禁止。特に "rationale" を "evidence" 等に置換しないこと。
code fence (``\`json ... ``\`) 禁止。生の JSON テキストのみ返す。

{"intended":"...","reading":"...","confidence":"high|medium|low","category":"reading_top|user_dict|bunsetsu|complex","rationale":"60字以内の判定理由"}

カテゴリの定義:
- reading_top: 単一 reading の top 候補が違うだけ
- user_dict:  未登録の固有名詞 / 外来語 / 複合語
- bunsetsu:   文節境界の切り方が誤り
- complex:    上記に当てはまらず人間判断が必要
"@

    $raw = "" | & claude -p $prompt --output-format json --model $Model 2>$null
    if ([string]::IsNullOrWhiteSpace($raw)) { return $null }
    try {
        $obj = $raw | ConvertFrom-Json
    } catch { return $null }
    $result = [string]$obj.result
    $cost   = [double]($obj.total_cost_usd)
    # 生 JSON を抽出。まず全体を try、失敗したら {}...{} 最大範囲を探す。
    # code fence (```json ... ```) にも対応。
    $stripped = $result -replace '(?s)^```(?:json)?\s*', '' -replace '(?s)\s*```\s*$', ''
    $parsed = $null
    try { $parsed = $stripped | ConvertFrom-Json } catch { }
    if ($null -eq $parsed) {
        $first = $result.IndexOf('{')
        $last  = $result.LastIndexOf('}')
        if ($first -ge 0 -and $last -gt $first) {
            $slice = $result.Substring($first, $last - $first + 1)
            try { $parsed = $slice | ConvertFrom-Json } catch { }
        }
    }
    if ($null -eq $parsed) {
        return [pscustomobject]@{ raw = $result; cost = $cost }
    }
    return [pscustomobject]@{
        intended       = [string]$parsed.intended
        reading_guess  = [string]$parsed.reading
        confidence     = [string]$parsed.confidence
        category       = [string]$parsed.category
        rationale      = [string]$parsed.rationale
        raw            = $result
        cost           = $cost
    }
}

$judged  = New-Object System.Collections.ArrayList
$costSum = 0.0

foreach ($m in $items) {
    Write-Host ""
    Write-Host ("[judge] {0} (count={1})" -f $m.lastCommitted, $m.count)
    $r = Ask $m
    if ($null -eq $r -or -not $r.intended) {
        Write-Host "  UNSURE (judge failed to parse)" -ForegroundColor Yellow
        [void]$judged.Add([pscustomobject]@{
            lastCommitted = $m.lastCommitted
            count         = $m.count
            forgetReading = $m.forgetReading
            verdict       = 'UNSURE'
            intended      = $null
            confidence    = $null
            category      = $null
            rationale     = if ($r -and $r.raw) { $r.raw } else { 'no response' }
        })
        if ($r) { $costSum += [double]$r.cost }
        continue
    }
    $costSum += [double]$r.cost

    Write-Host "  intended:   $($r.intended)"
    Write-Host "  category:   $($r.category)    (confidence: $($r.confidence))"
    Write-Host "  rationale:  $($r.rationale)"

    # forgetReading (ログ実測) を優先、無ければ Haiku 推定 reading_guess を使う。
    $reading = if ([string]::IsNullOrWhiteSpace($m.forgetReading)) { $r.reading_guess } else { $m.forgetReading }
    [void]$judged.Add([pscustomobject]@{
        lastCommitted = $m.lastCommitted
        count         = $m.count
        forgetReading = $m.forgetReading
        reading_used  = $reading
        reading_guess = $r.reading_guess
        verdict       = 'JUDGED'
        intended      = $r.intended
        confidence    = $r.confidence
        category      = $r.category
        rationale     = $r.rationale
    })
}

$out = [ordered]@{
    iso_time       = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss')
    source_scan    = $scan.iso_time
    model          = $Model
    total          = $items.Count
    total_cost_usd = [math]::Round($costSum, 4)
    judged         = @($judged)
}
$out | ConvertTo-Json -Depth 6 | Out-File -FilePath $outFile -Encoding utf8

Write-Host ""
Write-Host "[judge] total items: $($items.Count)"
Write-Host ("[judge] cost:        `${0}" -f ([math]::Round($costSum, 4)))
Write-Host "[judge] out:         $outFile"

# カテゴリ別サマリ
$bySrc = $judged | Where-Object { $_.verdict -eq 'JUDGED' } | Group-Object category
Write-Host ""
Write-Host "By category:"
foreach ($g in $bySrc) {
    Write-Host ("  {0,-13} {1}" -f $g.Name, $g.Count)
}
