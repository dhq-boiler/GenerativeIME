using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace GenerativeIME.Installer.Services;

public interface ITsfService
{
    /// <summary>Register HKLM entries for GenerativeIME.Tsf.dll. Returns true on success.</summary>
    bool Register(string tsfDllPath);

    /// <summary>Unregister HKLM entries. Returns true even if the DLL wasn't registered.</summary>
    bool Unregister(string tsfDllPath);

    /// <summary>Seed HKCU (CTF Assemblies + Preload) for the current user via SeedHkcu.ps1.</summary>
    bool SeedCurrentUser(string seedHkcuPs1Path);

    /// <summary>Kill ctfmon.exe so Windows respawns it with a fresh CTF cache.</summary>
    void RestartCtfmon();
}

/// <summary>
///     GenerativeIME TSF text service registration. Uses regsvr32.exe as a
///     separate process so the TSF DLL is NEVER loaded into the installer
///     process — LoadLibrary-in-process pinned the DLL and made the subsequent
///     wipe / extract fail with SharingViolation on GenerativeIME.Tsf.dll.
///     regsvr32 with /s suppresses success/error dialogs but its exit code is
///     only loosely correlated with DllRegisterServer's HRESULT — we've seen
///     exit=0 without HKLM keys getting written on some elevated-child paths.
///     We verify HKLM after the call and treat "exit 0 + missing key" as
///     failure so the user sees a real error page instead of a silent no-op.
/// </summary>
public sealed class TsfService : ITsfService
{
    private const string TsfClsid = "{D256C881-4B4F-4B8E-BBD6-E490BEDC85D9}";
    private const int RegsvrTimeoutMs = 15_000;

    public bool Register(string tsfDllPath)
    {
        if (!File.Exists(tsfDllPath))
        {
            return false;
        }

        RunRegsvr32("/s", tsfDllPath);
        // Verification: regsvr32 sometimes exits 0 without actually running
        // DllRegisterServer to completion when spawned from an elevated
        // child. The HKLM key check is the only reliable "did it work" test.
        return Registry.LocalMachine
            .OpenSubKey($@"SOFTWARE\Classes\CLSID\{TsfClsid}") is not null;
    }

    public bool Unregister(string tsfDllPath)
    {
        if (!File.Exists(tsfDllPath))
        {
            return true;
        }

        RunRegsvr32("/s /u", tsfDllPath);
        return true;
    }

    public bool SeedCurrentUser(string seedHkcuPs1Path)
    {
        if (!File.Exists(seedHkcuPs1Path))
        {
            return false;
        }

        var psi = new ProcessStartInfo("powershell.exe",
            $"-NoProfile -ExecutionPolicy Bypass -File \"{seedHkcuPs1Path}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        try
        {
            using var p = Process.Start(psi);
            if (p is null)
            {
                return false;
            }

            p.WaitForExit(10_000);
            return p.HasExited && p.ExitCode == 0;
        }
        catch
        {
            return false;
        }
    }

    public void RestartCtfmon()
    {
        // Kill only — Windows auto-respawns ctfmon on the next input-focus
        // event, and the auto-spawned instance runs in the correct (non-
        // elevated interactive-user) context. Spawning it ourselves from
        // an elevated installer would give the new ctfmon our admin token,
        // which is not what the CTF service expects.
        foreach (var p in Process.GetProcessesByName("ctfmon"))
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

    private static void RunRegsvr32(string flags, string tsfDllPath)
    {
        var psi = new ProcessStartInfo("regsvr32.exe", $"{flags} \"{tsfDllPath}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        try
        {
            using var p = Process.Start(psi);
            p?.WaitForExit(RegsvrTimeoutMs);
        }
        catch
        {
            // Verification in Register() decides success/failure — this
            // silence is intentional: a regsvr32 launch failure and a
            // "regsvr32 ran but HKLM is still empty" both surface as a
            // missing HKLM key downstream, which returns false to the
            // caller and reaches the user via the Done page's error text.
        }
    }
}