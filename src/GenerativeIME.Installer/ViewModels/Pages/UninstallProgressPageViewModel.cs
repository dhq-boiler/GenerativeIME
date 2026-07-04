using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using GenerativeIME.Installer.Models;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class UninstallProgressPageViewModel : PageViewModelBase
{
    private readonly INavigationService _nav;
    private readonly IUninstallationService _uninstall;
    private readonly UninstallDonePageViewModel _done;

    [ObservableProperty] private string _statusText = "開始しています…";
    [ObservableProperty] private double _progressValue = 0.0;

    public UninstallProgressPageViewModel(
        INavigationService nav,
        IUninstallationService uninstall,
        UninstallDonePageViewModel done)
    {
        _nav = nav;
        _uninstall = uninstall;
        _done = done;
    }

    public async Task RunAsync()
    {
        var progress = new Progress<InstallProgress>(p =>
        {
            Application.Current.Dispatcher.Invoke(() =>
            {
                StatusText = p.Message;
                ProgressValue = p.Fraction;
            });
        });

        var result = await Task.Run(() => _uninstall.UninstallAsync(progress, CancellationToken.None));

        Application.Current.Dispatcher.Invoke(() =>
        {
            _done.Configure(result.Success, result.ErrorMessage);
            _nav.NavigateTo<UninstallDonePageViewModel>();
        });
    }
}
