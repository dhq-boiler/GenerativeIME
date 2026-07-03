# build_msi.ps1 - Assemble a fresh v0.1.9 GenerativeIME MSI without any
# scratchpad-UUID path hardcoding. All inputs resolve relative to the
# repo root; the payload is staged into an installer/build/payload/
# directory that wix build harvests via $(var.PayloadDir).
#
# Usage:
#   .\installer\build_msi.ps1                   # full build (recommended)
#   .\installer\build_msi.ps1 -SkipRebuild      # reuse existing binaries
#
# Prereqs:
#   - VS 2026 v18 x64 build tools (msbuild + cl.exe) - any edition;
#     located via vswhere (scripts/buildenv.ps1)
#   - vcpkg mecab ($env:VcpkgRoot, default C:\vcpkg)
#   - WiX 5+ dotnet tool (wix --version)

[CmdletBinding()]
param(
    [switch]$SkipRebuild,
    # MSI ProductVersion (x.y.z). Empty = use the fallback defined in
    # Package.wxs. CI passes the auto-bumped release version here.
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot
$installer  = $PSScriptRoot
$buildDir   = Join-Path $installer 'build'
$payloadDir = Join-Path $buildDir 'payload'
$msiOut     = Join-Path $buildDir 'GenerativeIME.msi'
$tsfProj    = Join-Path $root 'src\GenerativeIME.Tsf\GenerativeIME.Tsf.vcxproj'
$tsfDll     = Join-Path $root 'src\GenerativeIME.Tsf\build\x64\Release\GenerativeIME.Tsf.dll'
$skkSrc     = Join-Path $root 'third_party\skk\SKK-JISYO.L.utf8'
$skkEmoji   = Join-Path $root 'third_party\skk\SKK-JISYO.emoji.utf8'
$skkLoan    = Join-Path $root 'third_party\skk\SKK-JISYO.loanwords.utf8'
$skkGodan   = Join-Path $root 'third_party\skk\SKK-JISYO.godan.utf8'
$unidicSrc  = Join-Path $root 'third_party\mecab\unidic-lite'
$setupCpp   = Join-Path $root 'src\GenerativeIME.Setup\GenerativeImeSetup.cpp'
$setupExe   = Join-Path $buildDir 'GenerativeImeSetup.exe'
$dmProj     = Join-Path $root 'src\GenerativeIME.DictManager\GenerativeIME.DictManager.csproj'
$dmPublish  = Join-Path $buildDir 'dictmgr'
. (Join-Path $root 'scripts\buildenv.ps1')
$msbuild    = $BuildEnv.MSBuild
$vcvars     = $BuildEnv.VcVars64
$vcpkgBin   = Join-Path $BuildEnv.VcpkgRoot 'installed\x64-windows\bin'

# vcvars64.bat calls vswhere.exe under the hood; put its parent on PATH.
$env:PATH = "$($BuildEnv.VswhereDir);" + $env:PATH

# Fresh build dir every time (except -SkipRebuild which reuses).
if (-not $SkipRebuild -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null

# --- Build TSF DLL (Release x64) --------------------------------------
if (-not $SkipRebuild) {
    Write-Host '[build] TSF DLL (Release x64)' -ForegroundColor Cyan
    & $msbuild $tsfProj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal /m 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "TSF build failed" }
}
if (-not (Test-Path $tsfDll)) { throw "TSF DLL missing: $tsfDll" }

# --- Build GenerativeImeSetup.exe --------------------------------------
if (-not $SkipRebuild) {
    Write-Host '[build] GenerativeImeSetup.exe' -ForegroundColor Cyan
    $clCmd = "`"$vcvars`" >nul && cl /nologo /EHsc /W3 /MT /Fe:`"$setupExe`" `"$setupCpp`""
    cmd /c $clCmd
    if ($LASTEXITCODE -ne 0) { throw "Setup EXE compile failed" }
}
if (-not (Test-Path $setupExe)) { throw "Setup EXE missing: $setupExe" }

# --- Build DictManager (WPF, net10, framework-dependent) ---------------
# Framework-dependent keeps the payload small; the target needs the
# .NET 10 Desktop Runtime installed (a one-time prerequisite).
if (-not $SkipRebuild) {
    Write-Host '[build] GenerativeIME.DictManager (net10 WPF)' -ForegroundColor Cyan
    & dotnet publish $dmProj -c Release --self-contained false -o $dmPublish --nologo -v m 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw "DictManager publish failed" }
}
$dmExe = Join-Path $dmPublish 'GenerativeIME.DictManager.exe'
if (-not (Test-Path $dmExe)) { throw "DictManager exe missing: $dmExe" }

# --- Stage payload ----------------------------------------------------
Write-Host '[stage] payload' -ForegroundColor Cyan
Copy-Item $tsfDll                          (Join-Path $payloadDir 'GenerativeIME.Tsf.dll') -Force
Copy-Item $skkSrc                          $payloadDir -Force
Copy-Item $skkEmoji                        $payloadDir -Force
Copy-Item $skkLoan                         $payloadDir -Force
Copy-Item $skkGodan                        $payloadDir -Force
Copy-Item (Join-Path $installer 'payload\SeedHkcu.ps1') $payloadDir -Force
Copy-Item $setupExe                        $payloadDir -Force
Copy-Item (Join-Path $vcpkgBin 'mecab.dll')       $payloadDir -Force
Copy-Item (Join-Path $vcpkgBin 'msvcp140.dll')    $payloadDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $vcpkgBin 'vcruntime140.dll') $payloadDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $vcpkgBin 'vcruntime140_1.dll') $payloadDir -Force -ErrorAction SilentlyContinue
# user-dictionary manager (exe + dll + runtimeconfig + deps)
Get-ChildItem $dmPublish -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.json' } |
    ForEach-Object { Copy-Item $_.FullName $payloadDir -Force }
# unidic-lite bundle
$unidicDst = Join-Path $payloadDir 'unidic-lite'
New-Item -ItemType Directory -Force -Path $unidicDst | Out-Null
Copy-Item -Recurse -Force (Join-Path $unidicSrc '*') $unidicDst

Write-Host ('  staged files:')
Get-ChildItem $payloadDir -File | ForEach-Object { Write-Host ("    {0,10:N0}  {1}" -f $_.Length, $_.Name) }

# --- Build MSI --------------------------------------------------------
Write-Host '[build] MSI via wix' -ForegroundColor Cyan
$wxs = Join-Path $installer 'Package.wxs'
$wixArgs = @('build', '-arch', 'x64', '-d', "PayloadDir=$payloadDir")
if ($Version) {
    Write-Host "  ProductVersion: $Version"
    $wixArgs += @('-d', "ProductVersion=$Version")
}
& wix @wixArgs -o $msiOut $wxs
if ($LASTEXITCODE -ne 0) { throw "wix build failed" }

Write-Host ''
Write-Host ('[done] {0}  ({1:N0} bytes)' -f $msiOut, (Get-Item $msiOut).Length) -ForegroundColor Green
Write-Host ('       SHA256 {0}' -f (Get-FileHash $msiOut -Algorithm SHA256).Hash)
