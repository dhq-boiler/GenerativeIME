using System.Reflection;
using CommunityToolkit.Mvvm.ComponentModel;

namespace GenerativeIME.Installer.ViewModels;

public partial class InstallerShellViewModel : ObservableObject
{
    [ObservableProperty]
    private PageViewModelBase? _currentPage;

    public string FooterText { get; }

    public InstallerShellViewModel()
    {
        var ver = Assembly.GetExecutingAssembly().GetName().Version;
        var verString = ver is null ? "" : $"{ver.Major}.{ver.Minor}.{ver.Build}";
        FooterText = string.IsNullOrEmpty(verString)
            ? "GenerativeIME"
            : $"GenerativeIME v{verString}";
    }
}
