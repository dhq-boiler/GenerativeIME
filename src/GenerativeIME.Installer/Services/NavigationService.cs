using System;
using System.Collections.Generic;
using GenerativeIME.Installer.ViewModels;
using Microsoft.Extensions.DependencyInjection;

namespace GenerativeIME.Installer.Services;

public interface INavigationService
{
    void NavigateTo<TViewModel>() where TViewModel : PageViewModelBase;
    void GoBack();
    bool CanGoBack { get; }
}

public sealed class NavigationService : INavigationService
{
    private readonly IServiceProvider _services;
    private readonly InstallerShellViewModel _shell;
    private readonly Stack<Type> _history = new();

    public NavigationService(IServiceProvider services, InstallerShellViewModel shell)
    {
        _services = services;
        _shell = shell;
    }

    public bool CanGoBack => _history.Count > 1;

    public void NavigateTo<TViewModel>() where TViewModel : PageViewModelBase
    {
        var vm = _services.GetRequiredService<TViewModel>();
        _history.Push(typeof(TViewModel));
        _shell.CurrentPage = vm;
    }

    public void GoBack()
    {
        if (!CanGoBack) return;
        _history.Pop();
        var prev = _history.Peek();
        _shell.CurrentPage = (PageViewModelBase)_services.GetRequiredService(prev);
    }
}
