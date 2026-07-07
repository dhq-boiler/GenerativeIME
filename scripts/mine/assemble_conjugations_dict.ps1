# assemble_conjugations_dict.ps1 - SKK-JISYO.conjugations.utf8 の組み立て
#
# 2つの入力を統合して生成辞書を再構築する:
#   1. out/conj_auto.utf8         - マイナーの AUTO バケット (無条件収録)
#   2. out/conj_review_judged.tsv - Ollama 下読みの ADD 判定行 (条件付き収録)
#
# ADD 行の収録条件:
#   - 読みが3かな以上 (2かな読みは同音衝突リスクが高すぎる)
#   - くる(カ変)衝突ブロックリスト外 — 来る は活用生成の対象外なので、
#     着る 系の直接エントリを与えると「きたら→着たら」が「来たら」を
#     乗っ取る。きた/きて/きます/きました は gemma が KEEP 判定 or
#     2かな除外だが、防御的に全部ブロックする。
#   - 最新の out/conj_review.tsv に同じ (読み, current) で存在すること。
#     コード修正でミナーの current が変わった行の裁定は古いので棄却し、
#     conj_review_judged.tsv からも間引く (→ judge_review_ollama.ps1 の
#     再実行で再判定される)。
#
# エントリは gemma の選択表記を先頭に、残りの期待表記を後ろに並べる。
param()

$ErrorActionPreference = 'Stop'

$here    = $PSScriptRoot
$root    = Split-Path -Parent (Split-Path -Parent $here)
$autoF   = Join-Path $here 'out\conj_auto.utf8'
$reviewF = Join-Path $here 'out\conj_review.tsv'
$judgedF = Join-Path $here 'out\conj_review_judged.tsv'
$destF   = Join-Path $root 'third_party\skk\SKK-JISYO.conjugations.utf8'

$kBlocked = @('きた', 'きて', 'きたら', 'きます', 'きました')

# 最新 REVIEW の (reading -> current) 対応
$reviewCur = @{}
foreach ($l in Get-Content $reviewF -Encoding UTF8) {
    $c = $l -split "`t"
    if ($c.Length -ge 4) { $reviewCur[$c[1]] = $c[2] }
}

# AUTO エントリ (読み → 行) と読み集合
$autoLines = Get-Content $autoF -Encoding UTF8
$autoReadings = @{}
foreach ($l in $autoLines) {
    $sp = $l.IndexOf(' ')
    if ($sp -gt 0) { $autoReadings[$l.Substring(0, $sp)] = $true }
}

# 裁定済み行の照合。fresh = current が最新 REVIEW と一致する行のみ残す。
$freshJudged = New-Object System.Collections.Generic.List[string]
$addLines    = New-Object System.Collections.Generic.List[string]
$stale = 0; $addUsed = 0; $addBlocked = 0
foreach ($l in Get-Content $judgedF -Encoding UTF8) {
    $c = $l -split "`t"
    if ($c.Length -lt 5) { continue }
    $verdict = $c[0]; $pick = $c[1]; $reading = $c[2]; $current = $c[3]; $exList = $c[4]

    if (-not $reviewCur.ContainsKey($reading) -or $reviewCur[$reading] -ne $current) {
        $stale++   # コード修正で消滅/変化したギャップ → 裁定破棄
        continue
    }
    $freshJudged.Add($l)

    if ($verdict -ne 'ADD') { continue }
    if ($reading.Length -lt 3 -or $kBlocked -contains $reading) { $addBlocked++; continue }
    if ($autoReadings.ContainsKey($reading)) { continue }  # AUTO 優先

    $surfaces = @($pick) + (($exList.Trim('/') -split '/') | Where-Object { $_ -ne $pick })
    $addLines.Add("$reading /" + ($surfaces -join '/') + "/")
    $addUsed++
}

# 古い裁定を間引いた judged を書き戻す (再実行で再判定される)
Copy-Item $judgedF "$judgedF.bak" -Force
$freshJudged | Set-Content $judgedF -Encoding UTF8

# 未裁定の読み (新規 REVIEW − fresh judged)
$judgedReadings = @{}
foreach ($l in $freshJudged) { $judgedReadings[($l -split "`t")[2]] = $true }
$pending = @($reviewCur.Keys | Where-Object { -not $judgedReadings.ContainsKey($_) }).Count

# 辞書本体の組み立て: 既存ヘッダー (;; 行) を保持して差し替え
$existing = Get-Content $destF -Encoding UTF8
$headerEnd = ($existing | Select-String -Pattern '^;; okuri-nasi entries\.$').LineNumber
$header = $existing[0..($headerEnd - 1)]

$section = @(
    ';;',
    ';; ---- REVIEW 裁定済み追加分 (judge_review_ollama.ps1 = gemma4:12b 下読み',
    ';;      + 人手スポットチェック)。組み立ては assemble_conjugations_dict.ps1 ----'
)
$header + $autoLines + $section + $addLines | Set-Content $destF -Encoding UTF8

Write-Host ("auto={0} judgedADD used={1} blocked={2} stale-dropped={3} pending-rejudge={4}" -f `
    $autoLines.Count, $addUsed, $addBlocked, $stale, $pending)
Write-Host "wrote: $destF"
if ($pending -gt 0) {
    Write-Host "NOTE: $pending 行が未裁定。judge_review_ollama.ps1 -Model gemma4:12b を再実行してください。"
}
