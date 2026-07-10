using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;

namespace GenerativeIME.DictManager;

// One (reading -> candidates) row of an SKK dictionary. Candidates keeps the
// SKK surfaces joined by '/', e.g. "風呂った" or "雨/飴". Editable in the grid.
public sealed class DictEntry : INotifyPropertyChanged
{
    private string _candidates = "";
    private string _reading = "";

    public string Reading
    {
        get => _reading;
        set
        {
            _reading = value ?? "";
            OnPropertyChanged();
        }
    }

    public string Candidates
    {
        get => _candidates;
        set
        {
            _candidates = value ?? "";
            OnPropertyChanged();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? n = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
    }
}

// One dictionary file under the user-dict folder. Enabled maps to the file
// extension: "<name>.utf8" is loaded by the IME, "<name>.utf8.off" is ignored.
// Toggling Enabled renames the file on disk.
public sealed class DictFile : INotifyPropertyChanged
{
    public const string Ext = ".utf8";
    public const string DisabledSuffix = ".off";
    private string _displayName = "";

    private bool _enabled;

    private int _entryCount;
    private string _fullPath = "";

    public string DisplayName
    {
        get => _displayName;
        private set
        {
            _displayName = value;
            OnPropertyChanged();
        }
    }

    public string FullPath
    {
        get => _fullPath;
        private set
        {
            _fullPath = value;
            OnPropertyChanged();
        }
    }

    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (_enabled == value)
            {
                return;
            }

            try
            {
                SetEnabledOnDisk(value);
                _enabled = value;
            }
            catch
            {
                /* leave state unchanged; OnPropertyChanged snaps the UI back */
            }

            OnPropertyChanged();
        }
    }

    public int EntryCount
    {
        get => _entryCount;
        set
        {
            _entryCount = value;
            OnPropertyChanged();
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public static DictFile FromPath(string path)
    {
        var name = Path.GetFileName(path);
        var enabled = name.EndsWith(Ext, StringComparison.OrdinalIgnoreCase);
        var display = name;
        if (name.EndsWith(Ext + DisabledSuffix, StringComparison.OrdinalIgnoreCase))
        {
            display = name[..^(Ext.Length + DisabledSuffix.Length)];
        }
        else if (name.EndsWith(Ext, StringComparison.OrdinalIgnoreCase))
        {
            display = name[..^Ext.Length];
        }

        return new DictFile
        {
            _fullPath = path,
            _enabled = enabled,
            DisplayName = display,
            EntryCount = UserDictStore.CountEntries(path)
        };
    }

    // Rename between "<name>.utf8" and "<name>.utf8.off". Silently no-ops if the
    // target already matches (idempotent) or the source is missing.
    private void SetEnabledOnDisk(bool enable)
    {
        var dir = Path.GetDirectoryName(FullPath)!;
        var target = Path.Combine(dir, DisplayName + Ext + (enable ? "" : DisabledSuffix));
        if (string.Equals(target, FullPath, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        if (File.Exists(target))
        {
            File.Delete(target);
        }

        if (File.Exists(FullPath))
        {
            File.Move(FullPath, target);
        }

        FullPath = target;
    }

    // Rename the dictionary (its file), preserving the enabled/disabled state.
    // Throws IOException if another dictionary already uses the name. No-ops if
    // the sanitized name is unchanged.
    public void Rename(string newName)
    {
        var dir = Path.GetDirectoryName(FullPath)!;
        var safe = UserDictStore.SanitizeName(newName);
        if (safe.Length == 0)
        {
            throw new ArgumentException("名前を入力してください。");
        }

        if (string.Equals(safe, DisplayName, StringComparison.Ordinal))
        {
            return;
        }

        var enabledPath = Path.Combine(dir, safe + Ext);
        var disabledPath = Path.Combine(dir, safe + Ext + DisabledSuffix);
        if (File.Exists(enabledPath) || File.Exists(disabledPath))
        {
            throw new IOException($"「{safe}」は既に存在します。");
        }

        var target = _enabled ? enabledPath : disabledPath;
        File.Move(FullPath, target);
        FullPath = target;
        DisplayName = safe;
    }

    private void OnPropertyChanged([CallerMemberName] string? n = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
    }
}

// File-system operations over %APPDATA%\GenerativeIME\dict\. All SKK I/O is
// UTF-8 without BOM (what the IME's SkkDictionary::ParseFile expects).
public static class UserDictStore
{
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    public static string DictDir()
    {
        var dir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "GenerativeIME", "dict");
        Directory.CreateDirectory(dir);
        return dir;
    }

    public static ObservableCollection<DictFile> ListDicts()
    {
        var dir = DictDir();
        var files = Directory.EnumerateFiles(dir)
            .Where(p => p.EndsWith(DictFile.Ext, StringComparison.OrdinalIgnoreCase)
                        || p.EndsWith(DictFile.Ext + DictFile.DisabledSuffix, StringComparison.OrdinalIgnoreCase))
            .OrderBy(p => Path.GetFileName(p), StringComparer.OrdinalIgnoreCase);

        var list = new ObservableCollection<DictFile>();
        foreach (var f in files)
        {
            list.Add(DictFile.FromPath(f));
        }

        return list;
    }

    public static int CountEntries(string path)
    {
        try
        {
            var n = 0;
            foreach (var raw in File.ReadLines(path))
            {
                var line = raw.TrimEnd('\r');
                if (line.Length == 0 || line[0] == ';')
                {
                    continue;
                }

                if (line.IndexOf(' ') > 0)
                {
                    n++;
                }
            }

            return n;
        }
        catch
        {
            return 0;
        }
    }

    public static (string header, ObservableCollection<DictEntry> entries) Load(string path)
    {
        var entries = new ObservableCollection<DictEntry>();
        var header = new StringBuilder();
        var seenEntry = false;

        foreach (var raw in File.ReadAllLines(path, Utf8NoBom))
        {
            var line = raw.TrimEnd('\r');
            if (line.Length == 0 || line[0] == ';')
            {
                if (!seenEntry)
                {
                    header.AppendLine(line); // keep the leading comment block
                }

                continue;
            }

            var sp = line.IndexOf(' ');
            if (sp <= 0)
            {
                continue;
            }

            seenEntry = true;
            var reading = line[..sp];
            var rest = line[(sp + 1)..].Trim();
            // "/c1/c2/" -> "c1/c2"
            rest = rest.Trim('/');
            entries.Add(new DictEntry { Reading = reading, Candidates = rest });
        }

        return (header.ToString().TrimEnd('\r', '\n'), entries);
    }

    public static void Save(string path, string header, IEnumerable<DictEntry> entries)
    {
        var sb = new StringBuilder();
        if (!string.IsNullOrWhiteSpace(header))
        {
            sb.Append(header.TrimEnd('\r', '\n')).Append('\n');
        }
        else
        {
            sb.Append(";; GenerativeIME user dictionary\n");
        }

        foreach (var e in entries)
        {
            var r = e.Reading.Trim();
            var c = e.Candidates.Trim().Trim('/');
            if (r.Length == 0 || c.Length == 0)
            {
                continue; // skip incomplete rows
            }

            sb.Append(r).Append(" /").Append(c).Append("/\n");
        }

        File.WriteAllText(path, sb.ToString(), Utf8NoBom);
    }

    // Create a new empty enabled dictionary; returns its DictFile. Ensures a
    // unique filename if `name` collides.
    public static DictFile Create(string name)
    {
        var dir = DictDir();
        var safe = SanitizeName(name);
        if (safe.Length == 0)
        {
            safe = "dictionary";
        }

        var path = Path.Combine(dir, safe + DictFile.Ext);
        var i = 2;
        while (File.Exists(path) || File.Exists(path + DictFile.DisabledSuffix))
        {
            path = Path.Combine(dir, $"{safe}-{i++}{DictFile.Ext}");
        }

        File.WriteAllText(path, ";; GenerativeIME user dictionary\n", Utf8NoBom);
        return DictFile.FromPath(path);
    }

    // Copy an external SKK .utf8 file into the folder (import). Returns the new
    // DictFile. De-duplicates the target name.
    public static DictFile Import(string srcPath)
    {
        var dir = DictDir();
        var baseName = SanitizeName(Path.GetFileNameWithoutExtension(srcPath));
        if (baseName.Length == 0)
        {
            baseName = "imported";
        }

        var path = Path.Combine(dir, baseName + DictFile.Ext);
        var i = 2;
        while (File.Exists(path) || File.Exists(path + DictFile.DisabledSuffix))
        {
            path = Path.Combine(dir, $"{baseName}-{i++}{DictFile.Ext}");
        }

        File.Copy(srcPath, path);
        return DictFile.FromPath(path);
    }

    public static void Export(DictFile file, string destPath)
    {
        File.Copy(file.FullPath, destPath, true);
    }

    public static void Delete(DictFile file)
    {
        if (File.Exists(file.FullPath))
        {
            File.Delete(file.FullPath);
        }
    }

    internal static string SanitizeName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var cleaned = new string(name.Where(c => Array.IndexOf(invalid, c) < 0).ToArray()).Trim();
        // Drop a trailing ".utf8" if the user typed it.
        if (cleaned.EndsWith(DictFile.Ext, StringComparison.OrdinalIgnoreCase))
        {
            cleaned = cleaned[..^DictFile.Ext.Length];
        }

        return cleaned;
    }
}