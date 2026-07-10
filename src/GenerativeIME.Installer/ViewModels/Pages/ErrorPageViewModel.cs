using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class ErrorPageViewModel : PageViewModelBase
{
    [ObservableProperty] private string _detail = "";
    [ObservableProperty] private string _title = "エラー";

    [RelayCommand]
    private void Close()
    {
        Application.Current.Shutdown(1);
    }
}