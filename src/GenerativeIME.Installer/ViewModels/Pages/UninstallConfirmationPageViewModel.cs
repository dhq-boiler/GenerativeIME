using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class UninstallConfirmationPageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;
    private readonly IUninstallationService _uninstall;

    [ObservableProperty] private string _installedVersion = "";
    [ObservableProperty] private string _installLocation = "";

    public UninstallConfirmationPageViewModel(INavigationService nav, IUninstallationService uninstall)
    {
        _nav = nav;
        _uninstall = uninstall;
        var manifest = _uninstall.ReadManifest();
        InstalledVersion = manifest?.Version ?? "(不明)";
        InstallLocation  = manifest?.InstallRoot ?? "";
    }

    [RelayCommand]
    private void Uninstall() => _nav.NavigateTo<UninstallProgressPageViewModel>();

    [RelayCommand]
    private void Cancel() => System.Windows.Application.Current.Shutdown(0);
}
