namespace GenerativeIME.Installer.Models;

/// <summary>
///     JSON blob written to InstallRoot/install-manifest.json. Read by the
///     installer on second launch to detect an existing install, and by the
///     uninstaller to know what to remove.
/// </summary>
public sealed record InstallationManifest(
    string Version,
    string InstallRoot,
    DateTime InstalledAtUtc);