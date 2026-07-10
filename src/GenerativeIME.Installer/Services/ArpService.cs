using GenerativeIME.Installer.Models;
using Microsoft.Win32;

namespace GenerativeIME.Installer.Services;

public interface IArpService
{
    void Register(InstallationManifest manifest, string uninstallerExePath);
    void Unregister();
    string? ReadInstalledVersion();
    string? ReadInstallLocation();
}

/// <summary>
///     HKLM Add/Remove Programs entry. Installer runs elevated so per-machine
///     registration is fine and shows up under "Apps" for every user.
/// </summary>
public sealed class ArpService : IArpService
{
    private const string SubKey =
        @"Software\Microsoft\Windows\CurrentVersion\Uninstall\GenerativeIME";

    public void Register(InstallationManifest manifest, string uninstallerExePath)
    {
        using var key = Registry.LocalMachine.CreateSubKey(SubKey);
        if (key is null)
        {
            return;
        }

        key.SetValue("DisplayName", "GenerativeIME");
        key.SetValue("DisplayVersion", manifest.Version);
        key.SetValue("Publisher", "GenerativeIME");
        key.SetValue("InstallLocation", manifest.InstallRoot);
        key.SetValue("InstallDate", manifest.InstalledAtUtc.ToString("yyyyMMdd"));
        key.SetValue("UninstallString", $"\"{uninstallerExePath}\" --uninstall");
        key.SetValue("DisplayIcon", $"{uninstallerExePath},0");
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("URLInfoAbout", "https://github.com/dhq-boiler/GenerativeIME");
    }

    public void Unregister()
    {
        try
        {
            Registry.LocalMachine.DeleteSubKeyTree(SubKey, false);
        }
        catch
        {
            /* best effort */
        }
    }

    public string? ReadInstalledVersion()
    {
        using var key = Registry.LocalMachine.OpenSubKey(SubKey);
        return key?.GetValue("DisplayVersion") as string;
    }

    public string? ReadInstallLocation()
    {
        using var key = Registry.LocalMachine.OpenSubKey(SubKey);
        return key?.GetValue("InstallLocation") as string;
    }
}