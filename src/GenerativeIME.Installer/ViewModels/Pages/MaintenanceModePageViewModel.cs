using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class MaintenanceModePageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;
    private readonly IUninstallationService _uninstall;

    [ObservableProperty] private bool _isReinstallSelected = true;
    [ObservableProperty] private bool _isUninstallSelected = false;
    [ObservableProperty] private string _installedVersion = "";

    public MaintenanceModePageViewModel(INavigationService nav, IUninstallationService uninstall)
    {
        _nav = nav;
        _uninstall = uninstall;
        var manifest = _uninstall.ReadManifest();
        InstalledVersion = manifest?.Version ?? "";
    }

    [RelayCommand]
    private void Next()
    {
        if (IsUninstallSelected)
            _nav.NavigateTo<UninstallConfirmationPageViewModel>();
        else
            _nav.NavigateTo<ModeSelectPageViewModel>();
    }

    [RelayCommand]
    private void Cancel() => System.Windows.Application.Current.Shutdown(0);
}
