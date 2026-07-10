using System.IO;

namespace GenerativeIME.Installer.Services;

public interface IPathService
{
    string DefaultInstallRoot { get; }
    string ManifestPath(string installRoot);
    string TsfDllPath(string installRoot);
    string DictManagerExePath(string installRoot);
    string SeedHkcuPs1Path(string installRoot);
    string InstallerSelfCopyPath(string installRoot);
}

public sealed class PathService : IPathService
{
    public string DefaultInstallRoot =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "GenerativeIME");

    public string ManifestPath(string installRoot)
    {
        return Path.Combine(installRoot, "install-manifest.json");
    }

    public string TsfDllPath(string installRoot)
    {
        return Path.Combine(installRoot, "GenerativeIME.Tsf.dll");
    }

    public string DictManagerExePath(string installRoot)
    {
        return Path.Combine(installRoot, "GenerativeIME.DictManager.exe");
    }

    public string SeedHkcuPs1Path(string installRoot)
    {
        return Path.Combine(installRoot, "SeedHkcu.ps1");
    }

    /// <summary>Where the installer copies itself for later uninstall (ARP UninstallString points here).</summary>
    public string InstallerSelfCopyPath(string installRoot)
    {
        return Path.Combine(installRoot, "GenerativeImeInstaller.exe");
    }
}