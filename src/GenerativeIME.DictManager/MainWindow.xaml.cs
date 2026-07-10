using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace GenerativeIME.DictManager;

public partial class MainWindow : Window
{
    private DictFile? _current;
    private ObservableCollection<DictFile> _dicts = new();
    private bool _dirty;
    private ObservableCollection<DictEntry> _entries = new();
    private string _header = "";
    private bool _loading; // suppress dirty-marking while (re)loading

    public MainWindow()
    {
        InitializeComponent();
        PathHint.Text = UserDictStore.DictDir();
        ReloadDicts(true);
    }

    private void ReloadDicts(bool selectFirst = false, string? selectPathName = null)
    {
        _dicts = UserDictStore.ListDicts();
        DictList.ItemsSource = _dicts;

        if (selectPathName != null)
        {
            var match = _dicts.FirstOrDefault(d => d.DisplayName == selectPathName);
            if (match != null)
            {
                DictList.SelectedItem = match;
            }
        }
        else if (selectFirst && _dicts.Count > 0)
        {
            DictList.SelectedItem = _dicts[0];
        }

        Status.Text = $"{_dicts.Count} 辞書";
    }

    private void DictList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_dirty && _current != null && !PromptDiscard())
        {
            return;
        }

        LoadSelected();
    }

    private void LoadSelected()
    {
        _current = DictList.SelectedItem as DictFile;

        // Detach previous entry subscriptions.
        if (_entries != null)
        {
            _entries.CollectionChanged -= Entries_CollectionChanged;
            foreach (var en in _entries)
            {
                en.PropertyChanged -= Entry_PropertyChanged;
            }
        }

        var has = _current != null;
        RenameBtn.IsEnabled = ExportBtn.IsEnabled = DeleteDictBtn.IsEnabled =
            AddRowBtn.IsEnabled = DelRowBtn.IsEnabled = EntryGrid.IsEnabled = has;

        if (!has)
        {
            EntriesTitle.Text = "辞書を選択してください";
            _entries = new ObservableCollection<DictEntry>();
            EntryGrid.ItemsSource = null;
            SetDirty(false);
            return;
        }

        _loading = true;
        try
        {
            (_header, _entries) = UserDictStore.Load(_current!.FullPath);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"読み込みに失敗しました:\n{ex.Message}", "エラー",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            _entries = new ObservableCollection<DictEntry>();
        }

        _entries.CollectionChanged += Entries_CollectionChanged;
        foreach (var en in _entries)
        {
            en.PropertyChanged += Entry_PropertyChanged;
        }

        EntryGrid.ItemsSource = _entries;
        EntriesTitle.Text = _current!.DisplayName;
        _loading = false;
        SetDirty(false);
    }

    private void Entries_CollectionChanged(object? s, NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems != null)
        {
            foreach (DictEntry en in e.NewItems)
            {
                en.PropertyChanged += Entry_PropertyChanged;
            }
        }

        if (e.OldItems != null)
        {
            foreach (DictEntry en in e.OldItems)
            {
                en.PropertyChanged -= Entry_PropertyChanged;
            }
        }

        if (!_loading)
        {
            SetDirty(true);
        }
    }

    private void Entry_PropertyChanged(object? s, PropertyChangedEventArgs e)
    {
        if (!_loading)
        {
            SetDirty(true);
        }
    }

    private void EntryGrid_CellEditEnding(object? sender, DataGridCellEditEndingEventArgs e)
    {
        if (!_loading)
        {
            SetDirty(true);
        }
    }

    private void SetDirty(bool dirty)
    {
        _dirty = dirty;
        SaveBtn.IsEnabled = dirty;
        DirtyHint.Text = dirty ? "未保存の変更があります" : "";
    }

    private bool PromptDiscard()
    {
        var r = MessageBox.Show(this, "未保存の変更があります。破棄しますか？", "確認",
            MessageBoxButton.OKCancel, MessageBoxImage.Question);
        return r == MessageBoxResult.OK;
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        if (_current == null)
        {
            return;
        }

        EntryGrid.CommitEdit(DataGridEditingUnit.Row, true);
        try
        {
            UserDictStore.Save(_current.FullPath, _header, _entries);
            _current.EntryCount = _entries.Count(x =>
                x.Reading.Trim().Length > 0 && x.Candidates.Trim().Trim('/').Length > 0);
            SetDirty(false);
            Status.Text = $"保存しました — {_current.DisplayName}（{_current.EntryCount} 語）。"
                          + "変更は次に開くアプリから有効になります。";
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"保存に失敗しました:\n{ex.Message}", "エラー",
                MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void AddRow_Click(object sender, RoutedEventArgs e)
    {
        var en = new DictEntry();
        _entries.Add(en);
        EntryGrid.SelectedItem = en;
        EntryGrid.ScrollIntoView(en);
    }

    private void DelRow_Click(object sender, RoutedEventArgs e)
    {
        var sel = EntryGrid.SelectedItems.OfType<DictEntry>().ToList();
        foreach (var en in sel)
        {
            _entries.Remove(en);
        }

        if (sel.Count > 0)
        {
            SetDirty(true);
        }
    }

    private void NewDict_Click(object sender, RoutedEventArgs e)
    {
        var name = InputDialog.Show(this, "新しい辞書", "辞書の名前:", "my-dictionary");
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        try
        {
            var f = UserDictStore.Create(name);
            ReloadDicts(selectPathName: f.DisplayName);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "エラー", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void Import_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "辞書をインポート",
            Filter = "SKK 辞書 (*.utf8;*.txt)|*.utf8;*.txt|すべてのファイル (*.*)|*.*",
            Multiselect = true
        };
        if (dlg.ShowDialog(this) != true)
        {
            return;
        }

        DictFile? last = null;
        foreach (var src in dlg.FileNames)
        {
            try
            {
                last = UserDictStore.Import(src);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, $"{Path.GetFileName(src)} のインポートに失敗:\n{ex.Message}",
                    "エラー", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        ReloadDicts(selectPathName: last?.DisplayName);
        Status.Text = "インポートしました。変更は次に開くアプリから有効になります。";
    }

    private void RenameDict_Click(object sender, RoutedEventArgs e)
    {
        if (_current == null)
        {
            return;
        }

        var name = InputDialog.Show(this, "辞書の名前を変更", "新しい名前:", _current.DisplayName);
        if (string.IsNullOrWhiteSpace(name))
        {
            return;
        }

        try
        {
            // Mutates the existing DictFile (notifies DisplayName/FullPath), so
            // the list item and any unsaved edits in the open grid are kept.
            _current.Rename(name);
            EntriesTitle.Text = _current.DisplayName;
            Status.Text = $"名前を変更しました — {_current.DisplayName}";
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "エラー", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void Export_Click(object sender, RoutedEventArgs e)
    {
        if (_current == null)
        {
            return;
        }

        var dlg = new SaveFileDialog
        {
            Title = "辞書をエクスポート",
            FileName = _current.DisplayName + ".utf8",
            Filter = "SKK 辞書 (*.utf8)|*.utf8|すべてのファイル (*.*)|*.*"
        };
        if (dlg.ShowDialog(this) != true)
        {
            return;
        }

        try
        {
            UserDictStore.Export(_current, dlg.FileName);
            Status.Text = $"エクスポートしました — {dlg.FileName}";
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "エラー", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void DeleteDict_Click(object sender, RoutedEventArgs e)
    {
        if (_current == null)
        {
            return;
        }

        var r = MessageBox.Show(this,
            $"辞書「{_current.DisplayName}」を削除しますか？この操作は元に戻せません。",
            "辞書を削除", MessageBoxButton.OKCancel, MessageBoxImage.Warning);
        if (r != MessageBoxResult.OK)
        {
            return;
        }

        try
        {
            UserDictStore.Delete(_current);
            _dirty = false;
            ReloadDicts(true);
            if (_dicts.Count == 0)
            {
                LoadSelected();
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "エラー", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (_dirty && !PromptDiscard())
        {
            e.Cancel = true;
            return;
        }

        base.OnClosing(e);
    }
}