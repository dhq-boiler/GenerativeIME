using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class MaintenanceModePageViewModel : PageViewModelBase
{
    private readonly IDialogService _dialog;
    private readonly INavigationService _nav;
    private readonly IUninstallationService _uninstall;
    [ObservableProperty] private string _installedVersion = "";

    [ObservableProperty] private bool _isReinstallSelected = true;
    [ObservableProperty] private bool _isUninstallSelected;

    public MaintenanceModePageViewModel(INavigationService nav, IUninstallationService uninstall, IDialogService dialog)
    {
        _nav = nav;
        _uninstall = uninstall;
        _dialog = dialog;
        var manifest = _uninstall.ReadManifest();
        InstalledVersion = manifest?.Version ?? "";
    }

    [RelayCommand]
    private void Next()
    {
        if (IsUninstallSelected)
        {
            _nav.NavigateTo<UninstallConfirmationPageViewModel>();
        }
        else
        {
            _nav.NavigateTo<ModeSelectPageViewModel>();
        }
    }

    [RelayCommand]
    private void Cancel()
    {
        if (_dialog.ConfirmCancel())
        {
            Application.Current.Shutdown(0);
        }
    }
}