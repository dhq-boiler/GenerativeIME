using GenerativeIME.Installer.Models;

namespace GenerativeIME.Installer;

/// <summary>
///     Command-line flags recognised by <see cref="Program.Main" /> before WPF starts.
///     Only <see cref="Silent" /> affects the entry-point branch; <see cref="Uninstall" />
///     is also parsed for the App.xaml.cs uninstall path but re-read there rather than
///     threaded through, so we do not need to keep the two parsers in lock-step.
/// </summary>
internal sealed record CliOptions(
    bool Silent,
    bool Uninstall,
    UpgradeMode Mode,
    string? InstallRoot)
{
    public static CliOptions Parse(string[] args)
    {
        var silent = false;
        var uninstall = false;
        var mode = UpgradeMode.Safe;
        string? installRoot = null;

        foreach (var raw in args)
        {
            if (string.IsNullOrWhiteSpace(raw))
            {
                continue;
            }

            var arg = raw.Trim();
            if (string.Equals(arg, "--silent", StringComparison.OrdinalIgnoreCase)
                || string.Equals(arg, "-s", StringComparison.OrdinalIgnoreCase))
            {
                silent = true;
            }
            else if (string.Equals(arg, "--aggressive", StringComparison.OrdinalIgnoreCase))
            {
                mode = UpgradeMode.Aggressive;
            }
            else if (string.Equals(arg, "--uninstall", StringComparison.OrdinalIgnoreCase))
            {
                uninstall = true;
            }
            else if (arg.StartsWith("--install-root=", StringComparison.OrdinalIgnoreCase))
            {
                installRoot = arg["--install-root=".Length..].Trim('"');
            }
        }

        return new CliOptions(silent, uninstall, mode, installRoot);
    }
}
