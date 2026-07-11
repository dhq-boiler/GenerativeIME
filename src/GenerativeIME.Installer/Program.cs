using System.Runtime.InteropServices;

namespace GenerativeIME.Installer;

/// <summary>
///     Hand-rolled entry point so the same GenerativeImeInstaller.exe can branch between
///     the WPF wizard, a bare uninstall trigger, and headless --silent automation without
///     spinning up WPF machinery prematurely. The csproj points StartupObject here,
///     bypassing the auto-generated App.g.Main.
/// </summary>
internal static class Program
{
    private const int ATTACH_PARENT_PROCESS = -1;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(int dwProcessId);

    [STAThread]
    public static int Main(string[] args)
    {
        var options = CliOptions.Parse(args);

        if (options.Silent)
        {
            // WinExe has no console. When invoked from cmd, hook the parent's
            // console so silent-mode progress lines are visible; when spawned
            // by WDAC's Process.Start with RedirectStandardOutput=true, the
            // stdout handle is already a pipe and AttachConsole is a no-op.
            AttachConsole(ATTACH_PARENT_PROCESS);
            return SilentRunner.RunAsync(options).GetAwaiter().GetResult();
        }

        var app = new App();
        app.InitializeComponent();
        return app.Run();
    }
}
