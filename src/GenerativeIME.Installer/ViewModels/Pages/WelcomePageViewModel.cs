using System.Windows;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class WelcomePageViewModel : PageViewModelBase
{
    private readonly IDialogService _dialog;
    private readonly INavigationService _nav;

    public WelcomePageViewModel(INavigationService nav, IDialogService dialog)
    {
        _nav = nav;
        _dialog = dialog;
    }

    [RelayCommand]
    private void Next()
    {
        _nav.NavigateTo<InstallProgressPageViewModel>();
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