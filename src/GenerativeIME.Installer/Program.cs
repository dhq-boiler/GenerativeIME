using System;

namespace GenerativeIME.Installer;

/// <summary>
/// Hand-rolled entry point so the same GenerativeImeSetup.exe can branch between
/// the WPF wizard and a bare uninstall trigger without spinning up WPF machinery
/// prematurely. The csproj points StartupObject here, bypassing the auto-generated
/// App.g.Main.
/// </summary>
internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        var app = new App();
        app.InitializeComponent();
        return app.Run();
    }
}
