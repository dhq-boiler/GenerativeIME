# SeedHkcu.ps1 - per-user TSF activation seed for GenerativeIME.
# Invoked once per user by Active Setup (StubPath) at first logon after install.
# Writes the HKCU registry state Windows 11 requires before a TSF text service
# can actually be activated (HKLM registration + regsvr32 alone leave the TIP
# "registered but dormant").
#
# 2026-07-02 update (v0.1.9): switched from KLID E0210411 substitute path to
# direct 04110411 preload. Rationale: v0.1.8 shipped BOTH a KLID keyboard
# layout entry AND a TSF profile, and both appeared in the input-source picker
# as "GenerativeIME" - one from HKLM Keyboard Layouts, one from CTF Assemblies.
# The KLID component was removed from Package.wxs; SeedHkcu now preloads the
# standard Japanese HKL and relies on CTF Assemblies (below) to route it to
# our TSF profile. No CTF\Substitutes write anymore either since the target
# KLID no longer exists.

$ErrorActionPreference = 'Continue'

# GUID_TFCAT_TIP_KEYBOARD - the CTF assembly category for keyboard TIPs.
$catId = '{34745C63-B2F0-4784-8B67-5E12C8701A31}'
# Our TextService CLSID + AddLanguageProfile profile GUID.
$clsid = '{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}'
$prof  = '{F267F064-7917-4631-BB73-567C314F43BE}'
# Standard Japanese keyboard HKL. Windows will route it to our TSF profile
# via the CTF Assemblies entry seeded below.
$klid  = '04110411'

# --- HKCU\CTF\Assemblies\0x00000411\{catId} ---
$asm = "HKCU:\Software\Microsoft\CTF\Assemblies\0x00000411\$catId"
New-Item -Force -Path $asm | Out-Null
Set-ItemProperty -Path $asm -Name 'Default'        -Value $prof
Set-ItemProperty -Path $asm -Name 'Profile'        -Value $clsid
Set-ItemProperty -Path $asm -Name 'KeyboardLayout' -Value 0x04110411 -Type DWord

# --- HKCU\Keyboard Layout\Preload - append standard Japanese HKL in the next slot ---
$preload = 'HKCU:\Keyboard Layout\Preload'
New-Item -Force -Path $preload | Out-Null
$existing = (Get-Item $preload).GetValueNames() | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ }
if (-not ($existing | Where-Object { $true })) { $existing = @(0) }
# Skip if our KLID is already present in any slot.
$already = $false
foreach ($n in (Get-Item $preload).GetValueNames()) {
    if ((Get-ItemProperty -Path $preload -Name $n).$n -eq $klid) { $already = $true }
}
if (-not $already) {
    $next = (($existing | Measure-Object -Maximum).Maximum) + 1
    Set-ItemProperty -Path $preload -Name "$next" -Value $klid
}

# NOTE: v0.1.9 no longer writes to HKCU\Software\Microsoft\CTF\Substitutes.
# The previous "04110411 -> E0210411" mapping only made sense while the KLID
# component existed; without it the substitute would dangle.

exit 0
