# build_installer.ps1 - Build the single-file WPF installer.
#
# Produces installer/build/GenerativeImeInstaller.exe with the entire
# payload (TSF DLL, DictManager, MeCab + UniDic-Lite dict, SKK dicts,
# GenerativeImeSetup.exe, SeedHkcu.ps1) embedded as a zip resource.
# End users get one exe — double-click, UAC, done.
#
# Framework-dependent single-file publish: needs .NET 10 Desktop Runtime
# on the target machine, keeps the exe under ~150 MB. Same trade-off as
# the DictManager csproj.
#
# Usage:
#   .\installer\build_installer.ps1                   # full build
#   .\installer\build_installer.ps1 -SkipRebuild      # reuse binaries
#   .\installer\build_installer.ps1 -Version 0.2.30   # CI-driven version

[CmdletBinding()]
param(
    [switch]$SkipRebuild,
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot
$installer  = $PSScriptRoot
$buildDir   = Join-Path $installer 'build'
$payloadDir = Join-Path $buildDir 'payload'
$exeOut     = Join-Path $buildDir 'GenerativeImeInstaller.exe'
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
$installerProj = Join-Path $root 'src\GenerativeIME.Installer\GenerativeIME.Installer.csproj'
$embeddedDir   = Join-Path $root 'src\GenerativeIME.Installer\Embedded'
$embeddedZip   = Join-Path $embeddedDir 'payload.zip'
$publishDir    = Join-Path $buildDir 'installer-publish'

. (Join-Path $root 'scripts\buildenv.ps1')
$msbuild = $BuildEnv.MSBuild
$vcvars  = $BuildEnv.VcVars64
$vcpkgBin = Join-Path $BuildEnv.VcpkgRoot 'installed\x64-windows\bin'
$env:PATH = "$($BuildEnv.VswhereDir);" + $env:PATH

# Fresh build dir every full run.
if (-not $SkipRebuild) {
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }
    if (Test-Path $embeddedZip) { Remove-Item -Force $embeddedZip }
}
New-Item -ItemType Directory -Force -Path $payloadDir  | Out-Null
New-Item -ItemType Directory -Force -Path $embeddedDir | Out-Null

# --- Build TSF DLL (Release x64) ---
if (-not $SkipRebuild) {
    Write-Host '[build] TSF DLL' -ForegroundColor Cyan
    & $msbuild $tsfProj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal /m 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw 'TSF build failed' }
}
if (-not (Test-Path $tsfDll)) { throw "TSF DLL missing: $tsfDll" }

# --- Build GenerativeImeSetup.exe ---
if (-not $SkipRebuild) {
    Write-Host '[build] GenerativeImeSetup.exe' -ForegroundColor Cyan
    $clCmd = "`"$vcvars`" >nul && cl /nologo /EHsc /W3 /MT /Fe:`"$setupExe`" `"$setupCpp`""
    cmd /c $clCmd
    if ($LASTEXITCODE -ne 0) { throw 'Setup EXE compile failed' }
}
if (-not (Test-Path $setupExe)) { throw "Setup EXE missing: $setupExe" }

# --- Build DictManager (net10 WPF publish, framework-dependent) ---
if (-not $SkipRebuild) {
    Write-Host '[build] DictManager' -ForegroundColor Cyan
    & dotnet publish $dmProj -c Release --self-contained false -o $dmPublish --nologo -v m 2>&1 | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { throw 'DictManager publish failed' }
}

# --- Stage payload ---
Write-Host '[stage] payload' -ForegroundColor Cyan
Copy-Item $tsfDll     (Join-Path $payloadDir 'GenerativeIME.Tsf.dll') -Force
Copy-Item $skkSrc     $payloadDir -Force
Copy-Item $skkEmoji   $payloadDir -Force
Copy-Item $skkLoan    $payloadDir -Force
Copy-Item $skkGodan   $payloadDir -Force
Copy-Item (Join-Path $installer 'payload\SeedHkcu.ps1') $payloadDir -Force
Copy-Item $setupExe   $payloadDir -Force
Copy-Item (Join-Path $vcpkgBin 'mecab.dll')          $payloadDir -Force
Copy-Item (Join-Path $vcpkgBin 'msvcp140.dll')       $payloadDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $vcpkgBin 'vcruntime140.dll')   $payloadDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $vcpkgBin 'vcruntime140_1.dll') $payloadDir -Force -ErrorAction SilentlyContinue
Get-ChildItem $dmPublish -File |
    Where-Object { $_.Extension -in '.exe', '.dll', '.json' } |
    ForEach-Object { Copy-Item $_.FullName $payloadDir -Force }
$unidicDst = Join-Path $payloadDir 'unidic-lite'
New-Item -ItemType Directory -Force -Path $unidicDst | Out-Null
Copy-Item -Recurse -Force (Join-Path $unidicSrc '*') $unidicDst

$payloadSize = (Get-ChildItem $payloadDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ('  payload total: {0:N0} bytes ({1:N0} MB)' -f $payloadSize, ($payloadSize / 1MB))

# --- Zip payload into embedded resource ---
# The zip lands INSIDE the installer project so csproj's EmbeddedResource
# picks it up on next build. Use fastest compression (Optimal + Deflate
# is default) — brotli would compress better but System.IO.Compression's
# ZipArchive on the extract side reads deflate only.
Write-Host '[zip] payload → embedded resource' -ForegroundColor Cyan
if (Test-Path $embeddedZip) { Remove-Item -Force $embeddedZip }
Add-Type -AssemblyName 'System.IO.Compression.FileSystem'
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $payloadDir, $embeddedZip,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)  # don't include the top folder as a container
$zipSize = (Get-Item $embeddedZip).Length
Write-Host ('  {0}  ({1:N0} bytes / {2:N0} MB)' -f $embeddedZip, $zipSize, ($zipSize / 1MB))

# --- Build + publish installer as single-file exe ---
Write-Host '[publish] installer (single-file, framework-dependent)' -ForegroundColor Cyan
$publishArgs = @(
    'publish', $installerProj,
    '-c', 'Release',
    '-r', 'win-x64',
    '--self-contained', 'false',
    '-p:PublishSingleFile=true',
    '-p:IncludeNativeLibrariesForSelfExtract=true',
    '-p:EnableCompressionInSingleFile=true',
    '-o', $publishDir,
    '--nologo', '-v', 'm'
)
if ($Version) {
    $publishArgs += "-p:InstallerVersion=$Version"
    Write-Host "  InstallerVersion: $Version"
}
& dotnet @publishArgs 2>&1 | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) { throw 'installer publish failed' }

$srcExe = Join-Path $publishDir 'GenerativeImeInstaller.exe'
if (-not (Test-Path $srcExe)) { throw "publish did not produce exe: $srcExe" }
Copy-Item $srcExe $exeOut -Force

$exeSize = (Get-Item $exeOut).Length
Write-Host ''
Write-Host ('[done] {0}  ({1:N0} bytes / {2:N0} MB)' -f $exeOut, $exeSize, ($exeSize / 1MB)) -ForegroundColor Green
Write-Host ('       SHA256 {0}' -f (Get-FileHash $exeOut -Algorithm SHA256).Hash)
