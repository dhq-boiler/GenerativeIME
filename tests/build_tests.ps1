# Build + run the GenerativeIME core unit tests.
#
# Compiles tests/test_runner.cpp together with a curated subset of the IME's
# source files (romajitokana.cpp + symboldictionary.cpp for now) into a
# single console EXE, then runs it. Skips the full TSF integration — see
# the comment block at the top of test_runner.cpp.
#
# Prereqs: VS 2026 v18 + vcpkg mecab. Run from any PowerShell.

$ErrorActionPreference = 'Stop'

$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root 'src\GenerativeIME.Tsf'
$tests = Join-Path $root 'tests'
$out   = Join-Path $tests 'test_runner.exe'

$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$installer = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$env:PATH = "$installer;" + $env:PATH

# Output goes next to the IME DLL so g_hInst-derived ResolveDictPath
# /ResolveDictDir calls inside skkdictionary / mecabanalyzer find the
# co-located SKK-JISYO.L.utf8 / unidic-lite/ that the IME build's
# post-build event already staged. (We leave g_hInst = nullptr; the
# Win32 GetModuleFileName(NULL, ...) call returns this EXE's own path
# in that case, which is what we want.)
$build = Join-Path $root 'src\GenerativeIME.Tsf\build\x64\Debug'
$out   = Join-Path $build 'test_runner.exe'

# Source files to compile alongside the test runner. Adding a module
# usually means adding its link-time deps below too.
$srcs = @(
    "$src\romajitokana.cpp",
    "$src\symboldictionary.cpp",
    "$src\learningstore.cpp",
    "$src\skkdictionary.cpp",
    "$src\mecabanalyzer.cpp",
    "$src\bunsetsu.cpp"
)

$mecabInc = 'C:\vcpkg\installed\x64-windows\include'
$mecabLib = 'C:\vcpkg\installed\x64-windows\debug\lib'

$compile = "`"$vcvars`" >nul && cl /nologo /EHsc /std:c++20 /utf-8 " +
           "/MDd /D _CRT_SECURE_NO_WARNINGS /D _DEBUG " +
           "/I `"$mecabInc`" " +
           "/Fe:`"$out`" `"$tests\test_runner.cpp`" " +
           ($srcs | ForEach-Object { "`"$_`" " }) + " " +
           "/link /LIBPATH:`"$mecabLib`" mecab.lib shlwapi.lib shell32.lib ole32.lib"

cmd /c $compile
if ($LASTEXITCODE -ne 0) { Write-Host "Compile failed"; exit 1 }

# Sync data files that SkkDictionary / MecabAnalyzer resolve via
# g_hInst-derived paths. The IME build's post-build event stages these
# too, but only for the config it was invoked with — running tests
# without a Debug IME build (or after only editing dict data)
# otherwise reads a stale SKK-JISYO.L.utf8 and the SKK regression tests
# report bogus failures.
Copy-Item -Force (Join-Path $root 'third_party\skk\SKK-JISYO.L.utf8') $build
$unidicSrc = Join-Path $root 'third_party\mecab\unidic-lite'
$unidicDst = Join-Path $build 'unidic-lite'
if (-not (Test-Path $unidicDst)) { New-Item -ItemType Directory -Path $unidicDst | Out-Null }
Copy-Item -Force -Recurse (Join-Path $unidicSrc '*') $unidicDst

& $out
exit $LASTEXITCODE
