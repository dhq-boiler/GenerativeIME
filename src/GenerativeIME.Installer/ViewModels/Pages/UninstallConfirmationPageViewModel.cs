using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class UninstallConfirmationPageViewModel : PageViewModelBase
{
    private readonly IDialogService _dialog;
    private readonly INavigationService _nav;
    private readonly IUninstallationService _uninstall;
    [ObservableProperty] private string _installLocation = "";

    [ObservableProperty] private string _installedVersion = "";

    public UninstallConfirmationPageViewModel(INavigationService nav, IUninstallationService uninstall,
        IDialogService dialog)
    {
        _nav = nav;
        _uninstall = uninstall;
        _dialog = dialog;
        var manifest = _uninstall.ReadManifest();
        InstalledVersion = manifest?.Version ?? "(不明)";
        InstallLocation = manifest?.InstallRoot ?? "";
    }

    [RelayCommand]
    private void Uninstall()
    {
        _nav.NavigateTo<UninstallProgressPageViewModel>();
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