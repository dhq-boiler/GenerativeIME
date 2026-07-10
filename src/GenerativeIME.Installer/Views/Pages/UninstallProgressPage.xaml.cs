using System.Windows;
using System.Windows.Controls;
using GenerativeIME.Installer.ViewModels.Pages;

namespace GenerativeIME.Installer.Views.Pages;

public partial class UninstallProgressPage : UserControl
{
    public UninstallProgressPage()
    {
        InitializeComponent();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (DataContext is UninstallProgressPageViewModel vm)
        {
            await vm.RunAsync();
        }
    }
}