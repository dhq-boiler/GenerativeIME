using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class ErrorPageViewModel : PageViewModelBase
{
    [ObservableProperty] private string _title = "エラー";
    [ObservableProperty] private string _detail = "";

    [RelayCommand]
    private void Close() => System.Windows.Application.Current.Shutdown(1);
}
