# GenerativeIME dictionary-load diagnostic.
# Checks whether the SKK/MeCab data files sit next to the REGISTERED TSF DLL.
# If any are missing, "furo -> furo only (no kanji)" is explained: SKK + MeCab
# both fail to load, so only the kana fallback remains.
$clsid = '{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}'
$regPaths = @(
  "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32",
  "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsid\InprocServer32",
  "HKCU:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32"
)
$dll = $null
foreach ($rp in $regPaths) {
  if (Test-Path $rp) {
    $v = (Get-ItemProperty $rp).'(default)'
    Write-Host "REG  $rp"
    Write-Host "     -> $v"
    if ($v) { $dll = $v }
  }
}
if (-not $dll) {
  Write-Host ""
  Write-Host "[NG] CLSID not registered. The IME is not installed/registered." -ForegroundColor Red
  return
}

$dir = Split-Path $dll
Write-Host ""
Write-Host "DLL folder: $dir"
$dllOk = if (Test-Path $dll) { 'OK' } else { '*** MISSING ***' }
Write-Host ("  {0,-24}: {1}" -f 'DLL itself', $dllOk)

$files = 'SKK-JISYO.L.utf8','SKK-JISYO.godan.utf8','SKK-JISYO.emoji.utf8','SKK-JISYO.loanwords.utf8','mecab.dll'
foreach ($f in $files) {
  $p = Join-Path $dir $f
  if (Test-Path $p) {
    $len = (Get-Item $p).Length
    Write-Host ("  {0,-24}: OK ({1} bytes)" -f $f, $len)
  } else {
    Write-Host ("  {0,-24}: *** MISSING ***" -f $f) -ForegroundColor Red
  }
}
# unidic-lite is a FOLDER; check its sys.dic inside.
$u = Join-Path $dir 'unidic-lite\sys.dic'
if (Test-Path $u) {
  $len = (Get-Item $u).Length
  Write-Host ("  {0,-24}: OK ({1} bytes)" -f 'unidic-lite\sys.dic', $len)
} else {
  Write-Host ("  {0,-24}: *** MISSING ***" -f 'unidic-lite\sys.dic') -ForegroundColor Red
}

Write-Host ""
Write-Host "Verdict: any *** MISSING *** above is the cause of 'furo -> furo only'."
Write-Host "Fix    : place that data set in the SAME folder (reinstall the MSI, or copy the payload)."
