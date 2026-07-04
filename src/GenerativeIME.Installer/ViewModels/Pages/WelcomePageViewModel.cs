using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class WelcomePageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;

    public WelcomePageViewModel(INavigationService nav)
    {
        _nav = nav;
    }

    [RelayCommand]
    private void Next() => _nav.NavigateTo<InstallProgressPageViewModel>();

    [RelayCommand]
    private void Cancel() => System.Windows.Application.Current.Shutdown(0);
}
