# Build + run the conjugation-gap miner (conjgap_miner.cpp).
#
# Reconstructs verb / adjective terminal forms from SKK-JISYO.L okuri-ari
# entries, machine-generates conjugated forms per UniDic cType, probes each
# generated reading through the same deterministic conversion path the IME
# uses, and emits:
#   scripts/mine/out/conj_auto.utf8   - safe dictionary additions
#   scripts/mine/out/conj_review.tsv  - ambiguous cases for human review
#
# Usage:  .\mine_conjugation_gaps.ps1 [-MaxVerbs N]   (N=0 : no limit)
param([int]$MaxVerbs = 0)

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src   = Join-Path $root 'src\GenerativeIME.Tsf'
$here  = $PSScriptRoot
$build = Join-Path $root 'src\GenerativeIME.Tsf\build\x64\Debug'
$out   = Join-Path $build 'conjgap_miner.exe'
$outDir = Join-Path $here 'out'

. (Join-Path $root 'scripts\buildenv.ps1')
$vcvars = $BuildEnv.VcVars64
$env:PATH = "$($BuildEnv.VswhereDir);" + $env:PATH

$srcs = @(
    "$src\skkdictionary.cpp",
    "$src\mecabanalyzer.cpp",
    "$src\bunsetsu.cpp",
    "$src\modernranking.cpp"
)

$mecabInc = Join-Path $BuildEnv.VcpkgRoot 'installed\x64-windows\include'
$mecabLib = Join-Path $BuildEnv.VcpkgRoot 'installed\x64-windows\debug\lib'

$compile = "`"$vcvars`" >nul && cl /nologo /EHsc /std:c++20 /utf-8 /O2 " +
           "/MDd /D _CRT_SECURE_NO_WARNINGS /D _DEBUG " +
           "/I `"$mecabInc`" /I `"$src`" " +
           "/Fe:`"$out`" `"$here\conjgap_miner.cpp`" " +
           ($srcs | ForEach-Object { "`"$_`" " }) + " " +
           "/link /LIBPATH:`"$mecabLib`" mecab.lib shlwapi.lib shell32.lib ole32.lib user32.lib"

cmd /c $compile
if ($LASTEXITCODE -ne 0) { Write-Host "Compile failed"; exit 1 }

# Dictionaries must be staged next to the exe (same as build_tests.ps1).
# EXCEPTION: SKK-JISYO.conjugations.utf8 (this miner's own output, staged by
# the IME build) must NOT be loaded during mining — its direct entries would
# turn every previously-found gap into an OK and the regenerated conj_auto
# would silently lose them. Remove any staged copy first.
$staleConj = Join-Path $build 'SKK-JISYO.conjugations.utf8'
if (Test-Path $staleConj) { Remove-Item -Force $staleConj }
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.L.utf8') $build
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.emoji.utf8') $build
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.loanwords.utf8') $build
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.godan.utf8') $build
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.propernouns.utf8') $build
$unidicSrc = Join-Path $root 'third_party\mecab\unidic-lite'
$unidicDst = Join-Path $build 'unidic-lite'
if (-not (Test-Path $unidicDst)) { New-Item -ItemType Directory -Path $unidicDst | Out-Null }
Copy-Item -Force -Recurse (Join-Path $unidicSrc '*') $unidicDst

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$corpus = Join-Path $root 'corpus\goldens\wikipedia-top-combined.tsv'
& $out (Join-Path $root 'third_party\skk\SKK-JISYO.L.utf8') $outDir $MaxVerbs $corpus
exit $LASTEXITCODE
