# run_e2e_loop.ps1 — GenerativeIME E2E 回帰ループの 1 イテレーション
#
# CLAUDE.md 定義のループを 1 回だけ回し、結果を .claude/state/last_e2e_run.json に落とす。
# エージェント（人間または Claude Code）はこの JSON を見て次の一手を決める。
# 「終わりまで一気に回す」動きはしない — コスト暴走防止。
#
# 出力 JSON スキーマ:
#   {
#     "iso_time":       "2026-07-12T12:34:56",
#     "iteration":      3,              # このループの通算回数（state に累積）
#     "build_exit":     0,
#     "test_exit":      1,
#     "total_run":      42,
#     "total_failed":   3,
#     "unit_failures":  [ { "test": "romaji_basic", "line": 105 }, ... ],
#     "corpus_stats":   { "pass": 340, "total": 387, "rate": 87.9 },
#     "corpus_misses":  [ { "reading": "し", "expected": "市", "got": "氏" }, ... ],
#     "regression_key": "hash-of-sorted-failures",
#     "consecutive_fail_count": 2,      # 同じ regression_key が連続何回目か
#     "elapsed_sec":    45.2,
#     "verdict":        "FAIL" | "PASS" | "BUILD_ERROR"
#   }
#
# Usage:
#   .\scripts\loop\run_e2e_loop.ps1                  # 通常実行
#   .\scripts\loop\run_e2e_loop.ps1 -SkipBuild       # 直近ビルドを流用
#   .\scripts\loop\run_e2e_loop.ps1 -DryRun          # 状態 JSON を書かない

param(
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildScr  = Join-Path $root 'tests\build_tests.ps1'
$stateDir  = Join-Path $root '.claude\state'
$stateFile = Join-Path $stateDir 'last_e2e_run.json'
$logDir    = Join-Path $stateDir 'logs'

if (-not (Test-Path $stateDir)) { New-Item -ItemType Directory -Path $stateDir -Force | Out-Null }
if (-not (Test-Path $logDir))   { New-Item -ItemType Directory -Path $logDir   -Force | Out-Null }

$startTs = Get-Date
$isoTime = $startTs.ToString('yyyy-MM-ddTHH:mm:ss')
$logFile = Join-Path $logDir ("run_" + $startTs.ToString('yyyyMMdd_HHmmss') + '.log')

Write-Host "[loop] run @ $isoTime" -ForegroundColor Cyan
Write-Host "[loop] log -> $logFile"

# ---- 1. Build + run tests -------------------------------------------------

$testExe = Join-Path $root 'src\GenerativeIME.Tsf\build\x64\Debug\test_runner.exe'
$testOut = ''
$buildExit = 0
$testExit  = 0

if ($SkipBuild -and (Test-Path $testExe)) {
    Write-Host "[loop] -SkipBuild: reusing $testExe"
    $testOut = & $testExe 2>&1 | Out-String
    $testExit = $LASTEXITCODE
} else {
    # build_tests.ps1 は build + run を一気にやるので、丸ごと拾って exit を見る。
    $testOut = & powershell -NoProfile -File $buildScr 2>&1 | Out-String
    $testExit = $LASTEXITCODE
    # build フェーズ失敗（コンパイルエラー）は文字列で識別。
    if ($testOut -match 'Compile failed') { $buildExit = 1 }
}

$testOut | Out-File -FilePath $logFile -Encoding utf8

# ---- 2. Parse output -----------------------------------------------------

$totalRun    = 0
$totalFailed = 0
$currentTest = ''
$unitFailures = New-Object System.Collections.ArrayList
$corpusMisses = New-Object System.Collections.ArrayList
$corpusPass  = 0
$corpusTotal = 0

foreach ($line in ($testOut -split "`r?`n")) {
    if ($line -match '^\[ RUN \] (.+)$') {
        $currentTest = $matches[1].Trim()
        continue
    }
    # 'Total: N run, M failed'
    if ($line -match '^Total: (\d+) run, (\d+) failed') {
        $totalRun    = [int]$matches[1]
        $totalFailed = [int]$matches[2]
        continue
    }
    # EXPECT_EQ_W / EXPECT_TRUE: '  FAIL <file>:<line>...'
    if ($line -match '^\s+FAIL\s+([^:]+):(\d+)') {
        [void]$unitFailures.Add([pscustomobject]@{
            test = $currentTest
            file = $matches[1]
            line = [int]$matches[2]
        })
        continue
    }
    # corpus miss: '    miss: reading=X  expected=Y  got=Z'
    if ($line -match 'miss:\s+reading=(\S+)\s+expected=(\S+)\s+got=(\S+)') {
        [void]$corpusMisses.Add([pscustomobject]@{
            reading  = $matches[1]
            expected = $matches[2]
            got      = $matches[3]
        })
        continue
    }
    # 'corpus stats: X/Y passed (Z%)'
    if ($line -match 'corpus stats:\s+(\d+)/(\d+) passed') {
        $corpusPass  = [int]$matches[1]
        $corpusTotal = [int]$matches[2]
        continue
    }
}

$corpusRate = if ($corpusTotal -gt 0) { [math]::Round(100.0 * $corpusPass / $corpusTotal, 2) } else { 0 }

# ---- 3. Regression key (連続失敗判定用) ---------------------------------

# unit-test 失敗と corpus miss をソートして結合、SHA1 で識別子化。
# 「同じ失敗が続いている」ことを検出するためだけの用途。
$sig = @()
foreach ($u in $unitFailures) { $sig += "U:$($u.test):$($u.file):$($u.line)" }
foreach ($m in $corpusMisses) { $sig += "C:$($m.reading):$($m.expected):$($m.got)" }
$sigJoin = ($sig | Sort-Object) -join '|'
$regressionKey = if ($sigJoin) {
    $sha = [System.Security.Cryptography.SHA1]::Create()
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($sigJoin)
    -join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
} else { '' }

# ---- 4. Merge with previous state ----------------------------------------

$prev = $null
if (Test-Path $stateFile) {
    try { $prev = Get-Content $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $prev = $null }
}

$iteration = 1
$consecutiveFail = 0
if ($prev) {
    $iteration = [int]$prev.iteration + 1
    if ($regressionKey -and $prev.regression_key -eq $regressionKey) {
        $consecutiveFail = [int]$prev.consecutive_fail_count + 1
    } elseif ($regressionKey) {
        $consecutiveFail = 1
    }
}

$verdict = if ($buildExit -ne 0) { 'BUILD_ERROR' }
           elseif ($testExit -eq 0 -and $totalFailed -eq 0) { 'PASS' }
           else { 'FAIL' }

$elapsed = [math]::Round(((Get-Date) - $startTs).TotalSeconds, 1)

$state = [ordered]@{
    iso_time                = $isoTime
    iteration               = $iteration
    build_exit              = $buildExit
    test_exit               = $testExit
    total_run               = $totalRun
    total_failed            = $totalFailed
    unit_failures           = @($unitFailures)
    corpus_stats            = [ordered]@{
        pass  = $corpusPass
        total = $corpusTotal
        rate  = $corpusRate
    }
    corpus_misses           = @($corpusMisses)
    regression_key          = $regressionKey
    consecutive_fail_count  = $consecutiveFail
    elapsed_sec             = $elapsed
    verdict                 = $verdict
    log_file                = $logFile
}

# ---- 5. Persist ----------------------------------------------------------

$json = $state | ConvertTo-Json -Depth 6
if (-not $DryRun) {
    $json | Out-File -FilePath $stateFile -Encoding utf8
}

# ---- 6. Report to console ------------------------------------------------

$colorMap = @{ 'PASS' = 'Green'; 'FAIL' = 'Yellow'; 'BUILD_ERROR' = 'Red' }
$color = $colorMap[$verdict]
if (-not $color) { $color = 'White' }

Write-Host ""
Write-Host "[loop] verdict:     $verdict" -ForegroundColor $color
Write-Host "[loop] iteration:   $iteration"
Write-Host "[loop] tests:       $totalRun run, $totalFailed failed"
Write-Host "[loop] corpus:      $corpusPass/$corpusTotal ($corpusRate%)"
Write-Host "[loop] unit fails:  $($unitFailures.Count)"
Write-Host "[loop] corpus miss: $($corpusMisses.Count)"
Write-Host "[loop] regression:  $regressionKey"
Write-Host "[loop] consec fail: $consecutiveFail"
Write-Host "[loop] elapsed:     ${elapsed}s"
Write-Host "[loop] state:       $stateFile"
Write-Host ""

# 停止条件のヒント（CLAUDE.md と同期）
if ($consecutiveFail -ge 2) {
    Write-Host "[loop] STOP: 連続失敗 2 回以上 -> .claude/agents/e2e-fixer を呼べ" -ForegroundColor Magenta
}
if ($iteration -ge 5) {
    Write-Host "[loop] STOP: iteration 5 到達 -> ユーザーに報告して指示を仰げ" -ForegroundColor Magenta
}
if ($verdict -eq 'PASS') {
    Write-Host "[loop] DONE: 完了条件を満たした" -ForegroundColor Green
}

# exit code: 呼び出し側（hook や CI）が判定できるようにする
if ($buildExit -ne 0) { exit 2 }
if ($testExit  -ne 0) { exit 1 }
exit 0
