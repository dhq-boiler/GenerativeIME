using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class UninstallDonePageViewModel : PageViewModelBase
{
    [ObservableProperty] private string _detail = "";
    [ObservableProperty] private bool _isSuccess = true;
    [ObservableProperty] private string _title = "アンインストールが完了しました";

    public void Configure(bool success, string? errorMessage)
    {
        IsSuccess = success;
        if (success)
        {
            Title = "アンインストールが完了しました";
            Detail = "GenerativeIME を削除しました。";
        }
        else
        {
            Title = "アンインストールに失敗しました";
            Detail = errorMessage ?? "不明なエラーが発生しました。";
        }
    }

    [RelayCommand]
    private void Close()
    {
        Application.Current.Shutdown(IsSuccess ? 0 : 1);
    }
}