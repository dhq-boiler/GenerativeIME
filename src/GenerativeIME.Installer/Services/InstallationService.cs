using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using GenerativeIME.Installer.Models;

namespace GenerativeIME.Installer.Services;

public interface IInstallationService
{
    Task<InstallResult> InstallAsync(
        InstallationContext context,
        IProgress<InstallProgress>? progress,
        CancellationToken ct);
}

public sealed record InstallResult(bool Success, InstallationManifest? Manifest, string? ErrorMessage);

public sealed class InstallationService : IInstallationService
{
    private const string PayloadResourceName = "GenerativeIME.Installer.Embedded.payload.zip";

    private const string StashSuffix = ".gimeoldstash-";
    private const int MOVEFILE_REPLACE_EXISTING = 0x1;
    private const int MOVEFILE_DELAY_UNTIL_REBOOT = 0x4;
    private static readonly TimeSpan KillWait = TimeSpan.FromSeconds(5);
    private readonly IArpService _arp;
    private readonly IFileLockResolver _fileLocks;

    private readonly IPathService _paths;
    private readonly ITsfService _tsf;

    public InstallationService(IPathService paths, IArpService arp, ITsfService tsf, IFileLockResolver fileLocks)
    {
        _paths = paths;
        _arp = arp;
        _tsf = tsf;
        _fileLocks = fileLocks;
    }

    public async Task<InstallResult> InstallAsync(
        InstallationContext context,
        IProgress<InstallProgress>? progress,
        CancellationToken ct)
    {
        try
        {
            var installRoot = context.InstallRoot;
            progress?.Report(new InstallProgress("準備しています…", 0.02));
            Directory.CreateDirectory(installRoot);

            // Sequence matters: HKLM MUST be cleared BEFORE ctfmon is killed.
            // If we kill ctfmon first, Windows may auto-respawn it while our
            // regsvr32 /u subprocess is still running, and the new ctfmon
            // will read the old (still-registered) HKLM entry and re-map
            // GenerativeIME.Tsf.dll before we get to wipe — which then fails
            // with SharingViolation.
            var oldDll = _paths.TsfDllPath(installRoot);
            if (File.Exists(oldDll))
            {
                progress?.Report(new InstallProgress("既存の TSF DLL を登録解除しています…", 0.06));
                _tsf.Unregister(oldDll);
            }

            // Aggressive mode: NOW kill anything holding the target files.
            // Any respawned ctfmon after this point re-reads HKLM and finds
            // no GenerativeIME entry, so it will not remap the DLL.
            if (context.Mode == UpgradeMode.Aggressive)
            {
                progress?.Report(new InstallProgress("実行中のプロセスを終了しています…", 0.10));
                KillIfRunning("GenerativeIME.DictManager");
                KillIfRunning("GenerativeImeSetup");
                KillIfRunning("ctfmon");
                // Small settle so the OS actually releases the memory-mapped
                // sections the killed processes had on GenerativeIME.Tsf.dll
                // before we try to overwrite it. Without this, File.Create
                // on the DLL raced ctfmon's teardown ~10% of the time.
                await Task.Delay(500, ct);
            }

            // If files are still locked in safe mode, tell the user which
            // process to close and stop cleanly. Aggressive mode already
            // killed them above, so this check is defense-in-depth for the
            // corner case where a non-ctfmon consumer (a text-service-using
            // app that had focus at kill time) is still holding the DLL.
            var probe = new[]
            {
                oldDll,
                _paths.DictManagerExePath(installRoot)
            }.Where(File.Exists).ToArray();
            if (probe.Length > 0)
            {
                var lockers = _fileLocks.GetLockingProcesses(probe);
                if (lockers.Count > 0)
                {
                    if (context.Mode == UpgradeMode.Aggressive)
                    {
                        _fileLocks.KillProcesses(lockers.Select(p => p.Pid), KillWait);
                        await Task.Delay(300, ct);
                    }
                    else
                    {
                        var names = string.Join(", ", lockers.Select(p => p.ProcessName).Distinct());
                        return new InstallResult(false, null,
                            $"次のプログラムがファイルを使用中です。閉じてから再試行してください: {names}");
                    }
                }
            }

            // Nuke the install folder (differences from old install don't
            // survive) then re-create. Only wipes untracked artefacts too.
            progress?.Report(new InstallProgress("インストール先を初期化しています…", 0.18));
            WipeFolderContents(installRoot);

            progress?.Report(new InstallProgress("ファイルを展開しています…", 0.30));
            await ExtractPayloadAsync(installRoot, progress, ct);

            progress?.Report(new InstallProgress("TSF テキストサービスを登録しています…", 0.75));
            var newDll = _paths.TsfDllPath(installRoot);
            if (!_tsf.Register(newDll))
            {
                return new InstallResult(false, null,
                    "GenerativeIME.Tsf.dll の登録に失敗しました (regsvr32 が非ゼロ終了)。");
            }

            progress?.Report(new InstallProgress("ユーザープロファイルにテキスト入力を追加しています…", 0.85));
            // SeedHkcu.ps1 writes the CTF Assemblies + Preload entries the
            // input picker needs before it shows GenerativeIME in Win+Space.
            // The old MSI's GenerativeImeSetup.exe (input.dll's
            // InstallLayoutOrTip wrapper) proved unreliable when invoked as
            // a child of an elevated installer process; the SKK-style .ps1
            // seed writes the same keys directly and is idempotent.
            _tsf.SeedCurrentUser(_paths.SeedHkcuPs1Path(installRoot));

            progress?.Report(new InstallProgress("入力サービスを再起動しています…", 0.88));
            // ctfmon caches the TIP list; without a restart, Win+Space still
            // shows the pre-install picker even though HKCU is now seeded.
            _tsf.RestartCtfmon();

            progress?.Report(new InstallProgress("インストーラー自身をコピーしています…", 0.90));
            var installerCopy = CopySelfTo(installRoot);

            progress?.Report(new InstallProgress("マニフェストを書き込んでいます…", 0.94));
            var version = Assembly.GetExecutingAssembly().GetName().Version;
            var versionString = version is null ? "0.0.0" : $"{version.Major}.{version.Minor}.{version.Build}";
            var manifest = new InstallationManifest(
                versionString,
                installRoot,
                DateTime.UtcNow);
            await File.WriteAllTextAsync(_paths.ManifestPath(installRoot),
                JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }),
                ct);

            progress?.Report(new InstallProgress("プログラムの追加と削除に登録しています…", 0.97));
            _arp.Register(manifest, installerCopy ?? GetSelfPath());

            // Final stash sweep: RestartCtfmon just replaced the old
            // ctfmon (which was still holding the previous DLL under
            // its stash name) with a fresh instance that maps the new
            // DLL. That releases the last common source of "still locked
            // even after Kill" — so any stash created earlier in THIS
            // install often deletes cleanly now. Anything still held by
            // a foreign app (Explorer / a browser) stays for reboot via
            // the MOVEFILE_DELAY_UNTIL_REBOOT hint already registered.
            progress?.Report(new InstallProgress("残置ファイルを整理しています…", 0.99));
            CleanupStashes(installRoot);

            progress?.Report(new InstallProgress("完了", 1.0));
            return new InstallResult(true, manifest, null);
        }
        catch (OperationCanceledException)
        {
            return new InstallResult(false, null, "キャンセルされました。");
        }
        catch (Exception ex)
        {
            return new InstallResult(false, null, ex.Message);
        }
    }

    private static void KillIfRunning(string processName)
    {
        foreach (var p in Process.GetProcessesByName(processName))
        {
            try
            {
                p.Kill(true);
                p.WaitForExit(3000);
            }
            catch
            {
            }
            finally
            {
                p.Dispose();
            }
        }
    }

    // True iff `relativePath` sits inside the unidic-lite/ subtree (case-
    // insensitive, both `/` and `\` separators). Payload entries use `/`;
    // GetRelativePath on Windows uses `\`. Callers use this to decide
    // whether the same-size skip in ExtractPayloadAsync / CopyDirectoryAsync
    // is allowed — see the fuller comments there.
    private static bool IsUnidicPath(string relativePath)
    {
        if (string.IsNullOrEmpty(relativePath))
        {
            return false;
        }

        var normalized = relativePath.Replace('\\', '/');
        return normalized.StartsWith("unidic-lite/", StringComparison.OrdinalIgnoreCase);
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool MoveFileExW(string lpExistingFileName, string? lpNewFileName, int dwFlags);

    // If `dst` exists and cannot be deleted (a process still has it memory-
    // mapped — the classic case is GenerativeIME.Tsf.dll being pulled in by
    // every ctfmon/explorer/browser instance the moment HKLM registration
    // was in place, and staying mapped through our kill+respawn dance),
    // rename it out of the way so the fresh write can land at the original
    // path. NTFS rename works on locked files — it's only the directory
    // entry that changes; the open handles keep pointing at the same MFT
    // record. Schedule the stashed copy for delete-on-reboot as a hint to
    // Windows to clean it up eventually.
    //
    // Garbage-accumulation contract:
    //   - Before creating a new stash we sweep any EXISTING stashes for
    //     the same base filename. If the process that had them mapped is
    //     now gone (typical after ctfmon respawns fresh), those delete
    //     cleanly and we're left with exactly one stash per base file.
    //   - The just-created stash is registered for delete-on-reboot via
    //     MoveFileEx. Windows honors this for elevated writers, so a
    //     reboot is guaranteed to reclaim the disk space even if every
    //     subsequent install fails to clean up.
    //   - CleanupStashes is also called on install completion, on the
    //     next install's Wipe pass, and on uninstall. Combined worst
    //     case: one 1 MB stash per install session until reboot.
    //
    // Silent no-op when `dst` doesn't exist. Silent fallback to a pure
    // Delete attempt when the rename itself fails (some antivirus filter
    // drivers block MoveFile even in-directory); the caller's overwrite
    // then throws a clean SharingViolation if that path also fails.
    private static void RelocateIfLocked(string dst)
    {
        if (!File.Exists(dst))
        {
            return;
        }

        try
        {
            File.SetAttributes(dst, FileAttributes.Normal);
        }
        catch
        {
        }

        try
        {
            File.Delete(dst);
            return;
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }

        var dir = Path.GetDirectoryName(dst);
        if (string.IsNullOrEmpty(dir))
        {
            return;
        }

        var name = Path.GetFileName(dst);

        // Prune any prior stashes of THIS specific file first. Only stashes
        // that are still locked survive; the rest go away NOW instead of
        // waiting until reboot / next install.
        try
        {
            foreach (var prior in Directory.EnumerateFiles(dir, name + StashSuffix + "*"))
            {
                try
                {
                    File.SetAttributes(prior, FileAttributes.Normal);
                    File.Delete(prior);
                }
                catch
                {
                }
            }
        }
        catch
        {
        }

        var stashName = name + StashSuffix + DateTime.UtcNow.Ticks.ToString("X");
        var stash = Path.Combine(dir, stashName);
        try
        {
            File.Move(dst, stash);
            // Hint the OS to nuke the stash at next reboot in case no
            // future install ever runs. Failure here is fine — the next
            // RelocateIfLocked / CleanupStashes call cleans it up if
            // the mapping was released in the meantime.
            MoveFileExW(stash, null, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        catch
        {
            // Rename failed too. Leave `dst` in place; the caller's
            // overwrite will surface the real lock error to the user.
        }
    }

    // Sweep away stashed copies from previous installs. Locked stashes
    // stay behind for the next pass; the MOVEFILE_DELAY_UNTIL_REBOOT
    // registration from RelocateIfLocked catches them at reboot.
    // Callable from UninstallationService too — same suffix, same rules.
    internal static void CleanupStashes(string folder)
    {
        if (!Directory.Exists(folder))
        {
            return;
        }

        try
        {
            foreach (var f in Directory.EnumerateFiles(folder, "*" + StashSuffix + "*", SearchOption.AllDirectories))
            {
                try
                {
                    File.SetAttributes(f, FileAttributes.Normal);
                    File.Delete(f);
                }
                catch
                {
                }
            }
        }
        catch
        {
        }
    }

    private static void WipeFolderContents(string folder)
    {
        if (!Directory.Exists(folder))
        {
            return;
        }

        // First drain any leftover stashes from previous installs. If the
        // process that had them mapped has since exited, this finally
        // reclaims the disk space.
        CleanupStashes(folder);
        foreach (var f in Directory.EnumerateFiles(folder))
        {
            // Files that fail to delete (still locked) get relocated to a
            // stash name so the fresh write can land at the original path.
            // We can't rely on the extract's own retry for this because two
            // Delete calls in a row against a mapped file just fail twice —
            // the mapping doesn't release from repeated poking.
            try
            {
                File.SetAttributes(f, FileAttributes.Normal);
            }
            catch
            {
            }

            try
            {
                File.Delete(f);
            }
            catch (IOException)
            {
                RelocateIfLocked(f);
            }
            catch (UnauthorizedAccessException)
            {
                RelocateIfLocked(f);
            }
        }

        foreach (var d in Directory.EnumerateDirectories(folder))
        {
            // Skip unidic-lite: static reference data (~200MB) that never
            // changes between GenerativeIME versions. It stays memory-mapped
            // by any process that ever loaded GenerativeIME.Tsf.dll (which
            // pulls in mecab.dll → char.bin), and even after we kill ctfmon
            // Windows respawns it and every focused app re-loads it — so
            // the delete would fail with SharingViolation. Compare-by-size
            // in the extract path skips overwrite when identical, so we
            // never need to release the memory-map at all.
            var name = Path.GetFileName(d);
            if (string.Equals(name, "unidic-lite", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            try
            {
                Directory.Delete(d, true);
            }
            catch
            {
            }
        }
    }

    private async Task ExtractPayloadAsync(string installRoot, IProgress<InstallProgress>? progress,
        CancellationToken ct)
    {
        // Two sources, checked in order:
        //   1. Embedded resource GenerativeIME.Installer.Embedded.payload.zip
        //      (produced by build_installer.ps1 — the release-build path).
        //   2. A `payload\` folder next to the installer exe (dev iteration
        //      without re-baking the exe every time).
        var asm = typeof(InstallationService).Assembly;
        using var embedded = asm.GetManifestResourceStream(PayloadResourceName);
        if (embedded is not null)
        {
            using var zip = new ZipArchive(embedded, ZipArchiveMode.Read, false);
            var total = zip.Entries.Count;
            var done = 0;
            foreach (var entry in zip.Entries)
            {
                ct.ThrowIfCancellationRequested();
                if (string.IsNullOrEmpty(entry.Name))
                {
                    done++;
                    continue;
                }

                var dst = Path.GetFullPath(Path.Combine(installRoot, entry.FullName));
                if (!dst.StartsWith(Path.GetFullPath(installRoot), StringComparison.OrdinalIgnoreCase))
                {
                    throw new IOException($"ZIP entry escapes destination: {entry.FullName}");
                }

                Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                // Compare-by-size skip is limited to unidic-lite/*, which is
                // static reference data (~200MB) that stays memory-mapped in
                // every process that loaded mecab.dll. Skipping it there is
                // safe because same size means same content in practice.
                //
                // For every OTHER file (TSF DLL, DictManager, SKK dicts) the
                // skip was a real bug: two builds of GenerativeIME.Tsf.dll
                // frequently share the same size (linker rounds up section
                // padding), so a same-size-different-content DLL survived
                // the install and the user kept running the old code. The
                // Wipe step earlier already tries to delete these files, but
                // if the delete failed silently (file lock), the size skip
                // then kept the stale copy. Always overwrite for non-unidic
                // paths — the extract itself will throw a clear SharingViolation
                // if the file is actually locked, which is a much better
                // failure mode than a silent skip.
                if (IsUnidicPath(entry.FullName)
                    && File.Exists(dst) && new FileInfo(dst).Length == entry.Length)
                {
                    done++;
                    continue;
                }

                RelocateIfLocked(dst);
                entry.ExtractToFile(dst, true);
                done++;
                if (progress is not null && total > 0)
                {
                    var pct = 0.30 + 0.45 * done / total;
                    progress.Report(new InstallProgress($"展開中… ({done}/{total})", pct));
                }
            }

            return;
        }

        var loose = Path.Combine(AppContext.BaseDirectory, "payload");
        if (Directory.Exists(loose))
        {
            await CopyDirectoryAsync(loose, installRoot, progress, ct);
            return;
        }

        throw new FileNotFoundException(
            "ペイロードが見つかりません (embedded resource / payload フォルダの両方が不在)。");
    }

    private static async Task CopyDirectoryAsync(string src, string dst, IProgress<InstallProgress>? progress,
        CancellationToken ct)
    {
        Directory.CreateDirectory(dst);
        var files = Directory.EnumerateFiles(src, "*", SearchOption.AllDirectories).ToArray();
        var total = files.Length;
        var done = 0;
        foreach (var srcFile in files)
        {
            ct.ThrowIfCancellationRequested();
            var rel = Path.GetRelativePath(src, srcFile);
            var dstFile = Path.Combine(dst, rel);
            Directory.CreateDirectory(Path.GetDirectoryName(dstFile)!);

            // Compare-by-size skip is limited to unidic-lite/*, which is
            // static reference data (~200MB) that stays memory-mapped in
            // every process that loaded mecab.dll via GenerativeIME.Tsf.
            // For every other file (TSF DLL, DictManager, SKK dicts) we
            // ALWAYS overwrite — see the matching comment in the zip path
            // above for why a size-only compare would ship a stale build.
            var srcInfo = new FileInfo(srcFile);
            if (IsUnidicPath(rel) && File.Exists(dstFile))
            {
                var dstInfo = new FileInfo(dstFile);
                if (dstInfo.Length == srcInfo.Length)
                {
                    done++;
                    if (progress is not null && total > 0)
                    {
                        var pctSkip = 0.30 + 0.45 * done / total;
                        progress.Report(new InstallProgress($"スキップ (同一)… ({done}/{total})", pctSkip));
                    }

                    continue;
                }
            }

            RelocateIfLocked(dstFile);
            await using (var s = File.OpenRead(srcFile))
            await using (var d = File.Create(dstFile))
            {
                await s.CopyToAsync(d, ct);
            }

            done++;
            if (progress is not null && total > 0)
            {
                var pct = 0.30 + 0.45 * done / total;
                progress.Report(new InstallProgress($"コピー中… ({done}/{total})", pct));
            }
        }
    }

    private string? CopySelfTo(string installRoot)
    {
        try
        {
            var src = GetSelfPath();
            if (string.IsNullOrEmpty(src) || !File.Exists(src))
            {
                return null;
            }

            var dst = _paths.InstallerSelfCopyPath(installRoot);
            // Skip if we're already at the destination (running from InstallRoot).
            if (string.Equals(Path.GetFullPath(src), Path.GetFullPath(dst), StringComparison.OrdinalIgnoreCase))
            {
                return dst;
            }

            File.Copy(src, dst, true);
            return dst;
        }
        catch
        {
            return null;
        }
    }

    private static string GetSelfPath()
    {
        var module = Process.GetCurrentProcess().MainModule;
        return module?.FileName ?? string.Empty;
    }
}