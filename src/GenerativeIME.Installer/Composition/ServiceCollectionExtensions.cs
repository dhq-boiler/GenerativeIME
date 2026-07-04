using GenerativeIME.Installer.Models;
using GenerativeIME.Installer.Services;
using GenerativeIME.Installer.ViewModels;
using GenerativeIME.Installer.ViewModels.Pages;
using Microsoft.Extensions.DependencyInjection;

namespace GenerativeIME.Installer.Composition;

public static class ServiceCollectionExtensions
{
    public static IServiceCollection AddInstallerServices(this IServiceCollection services)
    {
        // Shell + navigation.
        services.AddSingleton<InstallerShellViewModel>();
        services.AddSingleton<INavigationService, NavigationService>();

        // Shared install state carried between wizard pages.
        services.AddSingleton<InstallationContext>();

        // Services.
        services.AddSingleton<IPathService, PathService>();
        services.AddSingleton<IFileLockResolver, RestartManagerFileLockResolver>();
        services.AddSingleton<IArpService, ArpService>();
        services.AddSingleton<ITsfService, TsfService>();
        services.AddSingleton<IInstallationService, InstallationService>();
        services.AddSingleton<IUninstallationService, UninstallationService>();

        // Wizard pages that hold navigation state stay transient so re-visit
        // rebuilds fresh input. The *Done pages are Singleton because the
        // progress page configures them (Success/Detail) BEFORE navigating
        // to them — with Transient, NavigateTo resolved a fresh unconfigured
        // instance and the user always saw the default "完了しました" text
        // even when install actually failed.
        services.AddTransient<WelcomePageViewModel>();
        services.AddTransient<ModeSelectPageViewModel>();
        services.AddTransient<InstallProgressPageViewModel>();
        services.AddSingleton<DonePageViewModel>();
        services.AddTransient<MaintenanceModePageViewModel>();
        services.AddTransient<UninstallConfirmationPageViewModel>();
        services.AddTransient<UninstallProgressPageViewModel>();
        services.AddSingleton<UninstallDonePageViewModel>();
        services.AddTransient<ErrorPageViewModel>();

        return services;
    }
}
