# buildenv.ps1 - resolve Visual Studio / vcpkg locations for build scripts.
# Dot-source this; it defines $BuildEnv (hashtable) with:
#   VsPath     - VS installation root (vswhere-resolved, any edition)
#   MSBuild    - full path to MSBuild.exe
#   VcVars64   - full path to vcvars64.bat
#   VcpkgRoot  - vcpkg root ($env:VcpkgRoot override, default C:\vcpkg;
#                same contract as <VcpkgRoot> in GenerativeIME.Tsf.vcxproj)
#   VswhereDir - directory containing vswhere.exe (vcvars64.bat shells out
#                to it, so callers put this on PATH before running vcvars)
#
# Hardcoding 'C:\Program Files\Microsoft Visual Studio\18\Community' broke
# builds on machines with a different edition (Professional/Enterprise) or
# install drive; vswhere is installed with every VS 2017+ and is the
# supported way to locate an installation.

$vswhereDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vswhere    = Join-Path $vswhereDir 'vswhere.exe'

$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1
}
if (-not $vsPath) {
    # vswhere missing or no C++ workload found: fall back to the path this
    # project has historically been developed against.
    $vsPath = 'C:\Program Files\Microsoft Visual Studio\18\Community'
}

$vcpkgRoot = if ($env:VcpkgRoot) { $env:VcpkgRoot } else { 'C:\vcpkg' }

$BuildEnv = @{
    VsPath     = $vsPath
    MSBuild    = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
    VcVars64   = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    VcpkgRoot  = $vcpkgRoot
    VswhereDir = $vswhereDir
}
