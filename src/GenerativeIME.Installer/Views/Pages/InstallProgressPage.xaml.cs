using System.Windows;
using System.Windows.Controls;
using GenerativeIME.Installer.ViewModels.Pages;

namespace GenerativeIME.Installer.Views.Pages;

public partial class InstallProgressPage : UserControl
{
    public InstallProgressPage() => InitializeComponent();

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (DataContext is InstallProgressPageViewModel vm)
        {
            await vm.RunAsync();
        }
    }
}
