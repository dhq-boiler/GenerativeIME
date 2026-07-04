@echo off
REM GenerativeIME installer wrapper.
REM
REM Double-click this .cmd (or run it from any prompt); it opens a small
REM Windows-Forms dialog asking which upgrade mode to use, then invokes
REM msiexec accordingly. The MSI itself defaults to safe mode — the
REM aggressive path only fires when this wrapper passes FORCE_KILL=1.
REM
REM Direct msiexec use is still supported:
REM   msiexec /i GenerativeIME.msi                # safe (this is the default)
REM   msiexec /i GenerativeIME.msi FORCE_KILL=1   # aggressive, no reboot
setlocal
set "MSI=%~dp0GenerativeIME.msi"
if not exist "%MSI%" (
    echo GenerativeIME.msi not found next to this script.
    echo Expected: %MSI%
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -MsiPath "%MSI%"
exit /b %ERRORLEVEL%
