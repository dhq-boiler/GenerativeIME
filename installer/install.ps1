# GenerativeIME installer wrapper: presents the upgrade-mode choice
# (aggressive vs safe) and invokes msiexec. Called from install.cmd.
#
# Aggressive = FORCE_KILL=1 → DictManager/GenerativeImeSetup/ctfmon are
#              taskkilled so the DLL locks release and no reboot is
#              required. Any unsaved DictManager work is lost.
# Safe       = MSI default → nothing terminated. If a file is locked
#              Windows Installer schedules the replacement for the next
#              reboot; the user is prompted (interactive) or told via
#              the log (silent). Nothing gets killed behind the user.
#
# The dialog runs before msiexec even reads the MSI, so a Cancel here
# does not modify system state at all.

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MsiPath,
    # Bypass the dialog (for scripted install). Values: aggressive / safe.
    [ValidateSet('aggressive','safe','')][string]$Mode = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $MsiPath)) {
    Write-Error "MSI not found: $MsiPath"
    exit 1
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

function Show-ModeDialog {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'GenerativeIME インストール'
    $form.Size = New-Object System.Drawing.Size(520, 340)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $lblTitle = New-Object System.Windows.Forms.Label
    $lblTitle.Text = 'アップデート方法を選んでください'
    $lblTitle.Font = New-Object System.Drawing.Font('Yu Gothic UI', 12, [System.Drawing.FontStyle]::Bold)
    $lblTitle.AutoSize = $true
    $lblTitle.Location = New-Object System.Drawing.Point(20, 15)
    $form.Controls.Add($lblTitle)

    # Aggressive radio
    $rbAggressive = New-Object System.Windows.Forms.RadioButton
    $rbAggressive.Text = '強引にアップデート (再起動なし)'
    $rbAggressive.Font = New-Object System.Drawing.Font('Yu Gothic UI', 10, [System.Drawing.FontStyle]::Bold)
    $rbAggressive.AutoSize = $true
    $rbAggressive.Location = New-Object System.Drawing.Point(25, 55)
    $rbAggressive.Checked = $false
    $form.Controls.Add($rbAggressive)

    $lblAggressive = New-Object System.Windows.Forms.Label
    $lblAggressive.Text = "  DictManager と ctfmon を強制終了してから DLL を差し替えます。" + [Environment]::NewLine + `
                          "  DictManager の未保存作業は失われます。" + [Environment]::NewLine + `
                          "  他のアプリの IME 状態も一瞬リセットされます。"
    $lblAggressive.Font = New-Object System.Drawing.Font('Yu Gothic UI', 9)
    $lblAggressive.AutoSize = $true
    $lblAggressive.Location = New-Object System.Drawing.Point(45, 80)
    $form.Controls.Add($lblAggressive)

    # Safe radio
    $rbSafe = New-Object System.Windows.Forms.RadioButton
    $rbSafe.Text = '安全にアップデート (必要なら再起動)'
    $rbSafe.Font = New-Object System.Drawing.Font('Yu Gothic UI', 10, [System.Drawing.FontStyle]::Bold)
    $rbSafe.AutoSize = $true
    $rbSafe.Location = New-Object System.Drawing.Point(25, 160)
    $rbSafe.Checked = $true
    $form.Controls.Add($rbSafe)

    $lblSafe = New-Object System.Windows.Forms.Label
    $lblSafe.Text = "  実行中のアプリは終了させません。" + [Environment]::NewLine + `
                    "  DLL がロック中なら次回再起動時に差し替えられます。" + [Environment]::NewLine + `
                    "  作業中のアプリを閉じてから実行すると再起動不要になります。"
    $lblSafe.Font = New-Object System.Drawing.Font('Yu Gothic UI', 9)
    $lblSafe.AutoSize = $true
    $lblSafe.Location = New-Object System.Drawing.Point(45, 185)
    $form.Controls.Add($lblSafe)

    # Buttons
    $btnOk = New-Object System.Windows.Forms.Button
    $btnOk.Text = 'インストール'
    $btnOk.Size = New-Object System.Drawing.Size(120, 32)
    $btnOk.Location = New-Object System.Drawing.Point(260, 260)
    $btnOk.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.AcceptButton = $btnOk
    $form.Controls.Add($btnOk)

    $btnCancel = New-Object System.Windows.Forms.Button
    $btnCancel.Text = 'キャンセル'
    $btnCancel.Size = New-Object System.Drawing.Size(120, 32)
    $btnCancel.Location = New-Object System.Drawing.Point(385, 260)
    $btnCancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.CancelButton = $btnCancel
    $form.Controls.Add($btnCancel)

    $result = $form.ShowDialog()
    if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
        return $null
    }
    return $(if ($rbAggressive.Checked) { 'aggressive' } else { 'safe' })
}

$chosen = if ($Mode) { $Mode } else { Show-ModeDialog }

if (-not $chosen) {
    Write-Host 'キャンセルされました。'
    exit 2
}

# Log to %TEMP% so a failed install leaves something to diagnose.
$log = Join-Path $env:TEMP ('GenerativeIME-install-{0:yyyyMMdd-HHmmss}.log' -f (Get-Date))

$msiArgs = @('/i', $MsiPath, '/norestart', '/L*V', $log)
if ($chosen -eq 'aggressive') {
    $msiArgs += 'FORCE_KILL=1'
}

Write-Host ("Mode: {0}" -f $chosen)
Write-Host ("Log:  {0}" -f $log)
Write-Host ('msiexec ' + ($msiArgs -join ' '))
Write-Host ''

$p = Start-Process msiexec -ArgumentList $msiArgs -Wait -PassThru -Verb runAs
$code = $p.ExitCode

# 1641 = success, reboot initiated; 3010 = success, reboot required.
switch ($code) {
    0    { Write-Host 'インストール完了' -ForegroundColor Green }
    1602 { Write-Host 'ユーザーによりキャンセルされました' -ForegroundColor Yellow }
    1618 { Write-Host '別の Windows Installer が実行中です。しばらく待って再実行してください。' -ForegroundColor Yellow }
    3010 { Write-Host 'インストール完了 (再起動が推奨されます — 安全モードでファイルロック検出)' -ForegroundColor Yellow }
    1641 { Write-Host 'インストール完了 (Windows Installer が再起動を予定しました)' -ForegroundColor Yellow }
    default { Write-Host ("インストール失敗: exit code {0}" -f $code) -ForegroundColor Red; Write-Host "ログ: $log" }
}

exit $code
