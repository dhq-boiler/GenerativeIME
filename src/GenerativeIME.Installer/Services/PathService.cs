using System;
using System.IO;

namespace GenerativeIME.Installer.Services;

public interface IPathService
{
    string DefaultInstallRoot { get; }
    string ManifestPath(string installRoot);
    string TsfDllPath(string installRoot);
    string DictManagerExePath(string installRoot);
    string SeedHkcuPs1Path(string installRoot);
    string GenerativeImeSetupExePath(string installRoot);
    string InstallerSelfCopyPath(string installRoot);
}

public sealed class PathService : IPathService
{
    public string DefaultInstallRoot =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "GenerativeIME");

    public string ManifestPath(string installRoot) =>
        Path.Combine(installRoot, "install-manifest.json");

    public string TsfDllPath(string installRoot) =>
        Path.Combine(installRoot, "GenerativeIME.Tsf.dll");

    public string DictManagerExePath(string installRoot) =>
        Path.Combine(installRoot, "GenerativeIME.DictManager.exe");

    public string SeedHkcuPs1Path(string installRoot) =>
        Path.Combine(installRoot, "SeedHkcu.ps1");

    public string GenerativeImeSetupExePath(string installRoot) =>
        Path.Combine(installRoot, "GenerativeImeSetup.exe");

    /// <summary>Where the installer copies itself for later uninstall (ARP UninstallString points here).</summary>
    public string InstallerSelfCopyPath(string installRoot) =>
        Path.Combine(installRoot, "GenerativeImeInstaller.exe");
}
