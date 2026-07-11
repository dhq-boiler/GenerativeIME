using GenerativeIME.Installer.Composition;
using GenerativeIME.Installer.Models;
using GenerativeIME.Installer.Services;
using Microsoft.Extensions.DependencyInjection;

namespace GenerativeIME.Installer;

/// <summary>
///     Exit codes returned to the parent process (WDAC's DesktopHelper or a manual cmd
///     invocation). Keep in sync with docs/wdac-exe-installer-spec.md §2.3.
/// </summary>
internal enum SilentExitCode
{
    Success = 0,
    GeneralFailure = 1,
    FileLocked = 2,
    PermissionDenied = 3
}

/// <summary>
///     Headless install path for automation. Builds the same DI container the WPF
///     wizard uses, drives <see cref="IInstallationService" /> directly, streams
///     progress to stdout, and maps the result to a stable exit code.
///
///     Console output relies on the parent process either owning a console (cmd)
///     or redirecting stdout via ProcessStartInfo. WinExe means no console is
///     auto-allocated — see Program.Main's AttachConsole call for the cmd case.
/// </summary>
internal static class SilentRunner
{
    public static async Task<int> RunAsync(CliOptions options)
    {
        try
        {
            var services = new ServiceCollection();
            services.AddInstallerServices();
            using var provider = services.BuildServiceProvider();

            var ctx = provider.GetRequiredService<InstallationContext>();
            ctx.Mode = options.Mode;
            if (!string.IsNullOrEmpty(options.InstallRoot))
            {
                ctx.InstallRoot = options.InstallRoot;
            }

            WriteLine($"[silent] install root: {ctx.InstallRoot}");
            WriteLine($"[silent] mode: {ctx.Mode}");

            var progress = new Progress<InstallProgress>(p =>
                WriteLine($"[silent] {p.Fraction,6:P1} {p.Message}"));

            var svc = provider.GetRequiredService<IInstallationService>();
            var result = await svc.InstallAsync(ctx, progress, CancellationToken.None);

            if (result.Success)
            {
                WriteLine($"[silent] installed OK (version {result.Manifest?.Version ?? "?"})");
                return (int)SilentExitCode.Success;
            }

            WriteError($"[silent] install FAILED: {result.ErrorMessage}");
            return (int)ClassifyFailure(result.ErrorMessage);
        }
        catch (UnauthorizedAccessException ex)
        {
            WriteError($"[silent] permission denied: {ex.Message}");
            return (int)SilentExitCode.PermissionDenied;
        }
        catch (Exception ex)
        {
            WriteError($"[silent] unhandled: {ex}");
            return (int)SilentExitCode.GeneralFailure;
        }
    }

    private static SilentExitCode ClassifyFailure(string? errorMessage)
    {
        // InstallationService returns Japanese messages; match on the substring
        // that is unique to the file-lock branch ("ファイルを使用中"). Other
        // failures collapse to GeneralFailure — the human-readable message
        // still goes to stderr so callers can distinguish if needed.
        if (!string.IsNullOrEmpty(errorMessage)
            && errorMessage.Contains("ファイルを使用中", StringComparison.Ordinal))
        {
            return SilentExitCode.FileLocked;
        }

        return SilentExitCode.GeneralFailure;
    }

    private static void WriteLine(string message)
    {
        try
        {
            Console.Out.WriteLine(message);
            Console.Out.Flush();
        }
        catch
        {
            // Silent mode may run without any stdout sink (double-clicked from
            // Explorer). Losing progress lines there is fine — the exit code
            // still reaches the shell.
        }
    }

    private static void WriteError(string message)
    {
        try
        {
            Console.Error.WriteLine(message);
            Console.Error.Flush();
        }
        catch
        {
        }
    }
}
