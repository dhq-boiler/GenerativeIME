using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class WelcomePageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;
    private readonly IDialogService _dialog;

    public WelcomePageViewModel(INavigationService nav, IDialogService dialog)
    {
        _nav = nav;
        _dialog = dialog;
    }

    [RelayCommand]
    private void Next() => _nav.NavigateTo<InstallProgressPageViewModel>();

    [RelayCommand]
    private void Cancel()
    {
        if (_dialog.ConfirmCancel())
            System.Windows.Application.Current.Shutdown(0);
    }
}
