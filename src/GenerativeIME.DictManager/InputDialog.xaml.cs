using System.Windows;
using System.Windows.Input;

namespace GenerativeIME.DictManager;

public partial class InputDialog : Window
{
    private InputDialog()
    {
        InitializeComponent();
        Loaded += (_, _) => { Input.Focus(); Input.SelectAll(); };
    }

    // Returns the entered text, or null if cancelled / empty.
    public static string? Show(Window owner, string title, string prompt, string initial = "")
    {
        var dlg = new InputDialog
        {
            Owner = owner,
            Title = title,
        };
        dlg.PromptText.Text = prompt;
        dlg.Input.Text = initial;
        return dlg.ShowDialog() == true ? dlg.Input.Text : null;
    }

    private void Ok_Click(object sender, RoutedEventArgs e) { DialogResult = true; }
    private void Cancel_Click(object sender, RoutedEventArgs e) { DialogResult = false; }
}
