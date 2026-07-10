using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace GenerativeIME.Installer.ViewModels.Pages;

public sealed partial class DonePageViewModel : PageViewModelBase
{
    [ObservableProperty] private string _detail = "GenerativeIME をお使いいただけます。";
    [ObservableProperty] private bool _isSuccess = true;
    [ObservableProperty] private string _title = "インストールが完了しました";

    public void Configure(bool success, string? errorMessage)
    {
        IsSuccess = success;
        if (success)
        {
            Title = "インストールが完了しました";
            Detail = "Win+Space で入力方式を切り替えると GenerativeIME を選択できます。";
        }
        else
        {
            Title = "インストールに失敗しました";
            Detail = errorMessage ?? "不明なエラーが発生しました。";
        }
    }

    [RelayCommand]
    private void Close()
    {
        Application.Current.Shutdown(IsSuccess ? 0 : 1);
    }
}