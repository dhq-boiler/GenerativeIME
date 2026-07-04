using System.Windows;

namespace GenerativeIME.Installer.Services;

public interface IDialogService
{
    /// <summary>
    /// Ask the user "are you sure you want to cancel". Returns true if
    /// they confirm (installer should shut down), false if they want to
    /// stay in the wizard.
    /// </summary>
    bool ConfirmCancel();
}

public sealed class DialogService : IDialogService
{
    public bool ConfirmCancel()
    {
        var result = MessageBox.Show(
            "インストールを中止しますか？",
            "GenerativeIME セットアップ",
            MessageBoxButton.YesNo,
            MessageBoxImage.Question,
            MessageBoxResult.No);
        return result == MessageBoxResult.Yes;
    }
}
