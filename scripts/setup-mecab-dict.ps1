# Copies the unidic-lite dictionary that `pip install unidic-lite` placed
# inside the active Python environment over into this repo's third_party/
# directory, where the post-build event expects to find it.
#
# Usage:
#   pip install unidic-lite          (once)
#   .\scripts\setup-mecab-dict.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$dstDir   = Join-Path $repoRoot 'third_party\mecab\unidic-lite'

Write-Host 'Locating unidic_lite.DICDIR ...'
$srcDir = & python -c 'import unidic_lite; print(unidic_lite.DICDIR, end="")'
if (-not $srcDir -or -not (Test-Path $srcDir)) {
    Write-Error 'unidic-lite not found. Run: python -m pip install unidic-lite'
    exit 1
}

Write-Host "  source: $srcDir"
Write-Host "  target: $dstDir"

if (-not (Test-Path $dstDir)) {
    New-Item -ItemType Directory -Force $dstDir | Out-Null
}

Copy-Item -Path (Join-Path $srcDir '*') -Destination $dstDir -Force -Recurse

$sizeMb = '{0:N1}' -f ((Get-ChildItem $dstDir -Recurse | Measure-Object Length -Sum).Sum / 1MB)
Write-Host ("Copied. Total size: {0} MB" -f $sizeMb)
