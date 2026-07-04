using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GenerativeIME.Installer.Models;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class ModeSelectPageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;
    private readonly InstallationContext _context;

    [ObservableProperty] private bool _isSafeSelected = true;
    [ObservableProperty] private bool _isAggressiveSelected = false;

    public ModeSelectPageViewModel(INavigationService nav, InstallationContext context)
    {
        _nav = nav;
        _context = context;
    }

    [RelayCommand]
    private void Next()
    {
        _context.Mode = IsAggressiveSelected ? UpgradeMode.Aggressive : UpgradeMode.Safe;
        _nav.NavigateTo<InstallProgressPageViewModel>();
    }

    [RelayCommand]
    private void Back()
    {
        if (_nav.CanGoBack) _nav.GoBack();
        else _nav.NavigateTo<MaintenanceModePageViewModel>();
    }
}
