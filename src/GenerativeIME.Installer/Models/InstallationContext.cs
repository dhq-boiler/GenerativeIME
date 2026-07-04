namespace GenerativeIME.Installer.Models;

/// <summary>
/// State shared across wizard pages. The mode selected on ModeSelectPage
/// controls whether the aggressive process-kill path runs before file copy.
/// </summary>
public sealed class InstallationContext
{
    public UpgradeMode Mode { get; set; } = UpgradeMode.Safe;
    public string InstallRoot { get; set; } = @"C:\Program Files\GenerativeIME";
    public string? PreviousVersion { get; set; }
    public bool IsUpgrade => PreviousVersion is not null;
}

public enum UpgradeMode
{
    /// <summary>
    /// Nothing terminated. If a DLL is locked, install fails cleanly and the
    /// user is told which app is holding it — they close it and retry.
    /// </summary>
    Safe = 0,

    /// <summary>
    /// DictManager / ctfmon killed before file replace. No reboot required,
    /// but any unsaved work in DictManager is lost and every app's IME
    /// state is re-initialized when ctfmon respawns.
    /// </summary>
    Aggressive = 1,
}
