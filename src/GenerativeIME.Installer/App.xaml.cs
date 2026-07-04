using System;
using System.IO;
using System.Windows;
using GenerativeIME.Installer.Composition;
using GenerativeIME.Installer.Services;
using GenerativeIME.Installer.ViewModels;
using GenerativeIME.Installer.ViewModels.Pages;
using GenerativeIME.Installer.Views;
using Microsoft.Extensions.DependencyInjection;

namespace GenerativeIME.Installer;

public partial class App : Application
{
    private IServiceProvider? _services;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var services = new ServiceCollection();
        services.AddInstallerServices();
        _services = services.BuildServiceProvider();

        var nav = _services.GetRequiredService<INavigationService>();
        var uninstaller = _services.GetRequiredService<IUninstallationService>();

        // Landing decision:
        //   --uninstall → straight to the uninstall confirmation (called from
        //                 the ARP "Uninstall" button).
        //   existing install → MaintenanceModePage (Reinstall vs Uninstall).
        //   fresh          → WelcomePage → InstallPage → progress.
        var explicitUninstall = false;
        foreach (var arg in e.Args)
        {
            if (string.Equals(arg, "--uninstall", StringComparison.OrdinalIgnoreCase))
                explicitUninstall = true;
        }

        if (explicitUninstall)
        {
            nav.NavigateTo<UninstallConfirmationPageViewModel>();
        }
        else if (uninstaller.IsInstalled())
        {
            nav.NavigateTo<MaintenanceModePageViewModel>();
        }
        else
        {
            nav.NavigateTo<WelcomePageViewModel>();
        }

        var shell = new InstallerShell
        {
            DataContext = _services.GetRequiredService<InstallerShellViewModel>(),
        };
        shell.Show();
    }
}
