using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using GenerativeIME.Installer.Models;
using GenerativeIME.Installer.Services;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class InstallProgressPageViewModel : PageViewModelBase
{
    private readonly InstallationContext _context;
    private readonly DonePageViewModel _done;
    private readonly IInstallationService _install;
    private readonly INavigationService _nav;
    [ObservableProperty] private double _progressValue;

    [ObservableProperty] private string _statusText = "開始しています…";

    public InstallProgressPageViewModel(
        INavigationService nav,
        InstallationContext context,
        IInstallationService install,
        DonePageViewModel done)
    {
        _nav = nav;
        _context = context;
        _install = install;
        _done = done;
    }

    /// <summary>Started by the view's Loaded event.</summary>
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

        var result = await Task.Run(() => _install.InstallAsync(_context, progress, CancellationToken.None));

        Application.Current.Dispatcher.Invoke(() =>
        {
            _done.Configure(result.Success, result.ErrorMessage);
            _nav.NavigateTo<DonePageViewModel>();
        });
    }
}