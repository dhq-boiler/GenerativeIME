using System;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using GenerativeIME.Installer.Models;

namespace GenerativeIME.Installer.Services;

public interface IUninstallationService
{
    bool IsInstalled();
    InstallationManifest? ReadManifest();
    Task<UninstallResult> UninstallAsync(IProgress<InstallProgress>? progress, CancellationToken ct);
}

public sealed record UninstallResult(bool Success, string? ErrorMessage);

public sealed class UninstallationService : IUninstallationService
{
    private readonly IPathService _paths;
    private readonly IArpService _arp;
    private readonly ITsfService _tsf;

    public UninstallationService(IPathService paths, IArpService arp, ITsfService tsf)
    {
        _paths = paths;
        _arp = arp;
        _tsf = tsf;
    }

    public bool IsInstalled()
    {
        // Path 1: our own installer wrote install-manifest.json — canonical.
        if (ReadManifest() is not null) return true;
        // Path 2: an old MSI install lives at the default root. Detect via
        // the TSF DLL presence so an existing v0.2.x MSI shows the
        // Maintenance page instead of Welcome (upgrade / uninstall flow).
        return File.Exists(_paths.TsfDllPath(_paths.DefaultInstallRoot));
    }

    public InstallationManifest? ReadManifest()
    {
        // Prefer the ARP entry (single source of truth for install location),
        // then read the manifest file inside that folder. Falls through to
        // the default root for MSI-installed builds (no ARP entry under
        // our "GenerativeIME" key, no manifest — returns null and the
        // caller falls back to the file-presence heuristic in IsInstalled).
        var installRoot = _arp.ReadInstallLocation() ?? _paths.DefaultInstallRoot;
        var manifestPath = _paths.ManifestPath(installRoot);
        if (!File.Exists(manifestPath)) return null;
        try
        {
            var json = File.ReadAllText(manifestPath);
            return JsonSerializer.Deserialize<InstallationManifest>(json);
        }
        catch
        {
            return null;
        }
    }

    public async Task<UninstallResult> UninstallAsync(IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        try
        {
            var manifest = ReadManifest();
            var installRoot = manifest?.InstallRoot ?? _paths.DefaultInstallRoot;

            progress?.Report(new InstallProgress("実行中のプロセスを終了しています…", 0.05));
            InstallationService_KillIfRunning("GenerativeIME.DictManager");
            InstallationService_KillIfRunning("GenerativeImeSetup");
            InstallationService_KillIfRunning("ctfmon");
            await Task.Delay(200, ct); // give ctfmon a beat to release its lock

            progress?.Report(new InstallProgress("TSF テキストサービスを登録解除しています…", 0.20));
            var tsfDll = _paths.TsfDllPath(installRoot);
            if (File.Exists(tsfDll)) _tsf.Unregister(tsfDll);

            progress?.Report(new InstallProgress("ファイルを削除しています…", 0.55));
            DeleteFolderTree(installRoot);

            progress?.Report(new InstallProgress("プログラムの追加と削除エントリを削除しています…", 0.90));
            _arp.Unregister();

            progress?.Report(new InstallProgress("完了", 1.0));
            return new UninstallResult(true, null);
        }
        catch (OperationCanceledException)
        {
            return new UninstallResult(false, "キャンセルされました。");
        }
        catch (Exception ex)
        {
            return new UninstallResult(false, ex.Message);
        }
    }

    private static void InstallationService_KillIfRunning(string name)
    {
        foreach (var p in System.Diagnostics.Process.GetProcessesByName(name))
        {
            try
            {
                p.Kill(entireProcessTree: true);
                p.WaitForExit(3000);
            }
            catch { }
            finally { p.Dispose(); }
        }
    }

    private static void DeleteFolderTree(string folder)
    {
        if (!Directory.Exists(folder)) return;
        // We may be running from InstallLocation — schedule self-delete on
        // reboot for the exe under us. Everything else can go now.
        foreach (var f in Directory.EnumerateFiles(folder))
        {
            try { File.SetAttributes(f, FileAttributes.Normal); File.Delete(f); } catch { }
        }
        foreach (var d in Directory.EnumerateDirectories(folder))
        {
            try { Directory.Delete(d, recursive: true); } catch { }
        }
        try { Directory.Delete(folder); } catch { /* self-exe may still be here */ }
    }
}
