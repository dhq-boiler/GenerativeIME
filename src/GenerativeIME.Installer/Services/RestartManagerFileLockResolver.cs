using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using GenerativeIME.Installer.Models;

namespace GenerativeIME.Installer.Services;

public interface IFileLockResolver
{
    IReadOnlyList<LockingProcessInfo> GetLockingProcesses(IReadOnlyList<string> filePaths);
    void KillProcesses(IEnumerable<int> pids, TimeSpan waitForExit);
}

public sealed class RestartManagerFileLockResolver : IFileLockResolver
{
    private const int CCH_RM_MAX_APP_NAME = 255;
    private const int CCH_RM_MAX_SVC_NAME = 63;
    private const int ERROR_SUCCESS = 0;
    private const int ERROR_MORE_DATA = 234;

    public IReadOnlyList<LockingProcessInfo> GetLockingProcesses(IReadOnlyList<string> filePaths)
    {
        if (filePaths is null || filePaths.Count == 0)
        {
            return Array.Empty<LockingProcessInfo>();
        }

        var existing = filePaths.Where(File.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (existing.Length == 0)
        {
            return Array.Empty<LockingProcessInfo>();
        }

        var sessionKey = new StringBuilder(new string('\0', 32), 32);
        var rc = RmStartSession(out var sessionHandle, 0, sessionKey);
        if (rc != ERROR_SUCCESS)
        {
            return Array.Empty<LockingProcessInfo>();
        }

        try
        {
            rc = RmRegisterResources(sessionHandle, (uint)existing.Length, existing, 0, null, 0, null);
            if (rc != ERROR_SUCCESS)
            {
                return Array.Empty<LockingProcessInfo>();
            }

            uint procInfoNeeded = 0;
            uint procInfo = 0;
            uint rebootReasons = 0;

            rc = RmGetList(sessionHandle, out procInfoNeeded, ref procInfo, null, ref rebootReasons);
            if (rc == ERROR_SUCCESS)
            {
                return Array.Empty<LockingProcessInfo>();
            }

            if (rc != ERROR_MORE_DATA)
            {
                return Array.Empty<LockingProcessInfo>();
            }

            if (procInfoNeeded == 0)
            {
                return Array.Empty<LockingProcessInfo>();
            }

            var apps = new RM_PROCESS_INFO[procInfoNeeded];
            procInfo = procInfoNeeded;
            rc = RmGetList(sessionHandle, out procInfoNeeded, ref procInfo, apps, ref rebootReasons);
            if (rc != ERROR_SUCCESS)
            {
                return Array.Empty<LockingProcessInfo>();
            }

            var result = new List<LockingProcessInfo>((int)procInfo);
            var self = Process.GetCurrentProcess().Id;
            for (var i = 0; i < procInfo; i++)
            {
                var pid = apps[i].Process.dwProcessId;
                if (pid == self)
                {
                    continue;
                }

                string? exePath = null;
                try
                {
                    using var p = Process.GetProcessById(pid);
                    exePath = SafeGetMainModuleFileName(p);
                }
                catch
                {
                }

                result.Add(new LockingProcessInfo(pid, apps[i].strAppName ?? $"pid:{pid}", exePath));
            }

            return result;
        }
        finally
        {
            RmEndSession(sessionHandle);
        }
    }

    public void KillProcesses(IEnumerable<int> pids, TimeSpan waitForExit)
    {
        foreach (var pid in pids.Distinct())
        {
            try
            {
                using var p = Process.GetProcessById(pid);
                if (!p.HasExited)
                {
                    p.Kill(true);
                    p.WaitForExit((int)waitForExit.TotalMilliseconds);
                }
            }
            catch
            {
            }
        }
    }

    private static string? SafeGetMainModuleFileName(Process p)
    {
        try
        {
            return p.MainModule?.FileName;
        }
        catch
        {
            return null;
        }
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmEndSession(uint pSessionHandle);

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmRegisterResources(
        uint pSessionHandle, uint nFiles, string[]? rgsFilenames,
        uint nApplications, RM_UNIQUE_PROCESS[]? rgApplications,
        uint nServices, string[]? rgsServiceNames);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmGetList(
        uint dwSessionHandle, out uint pnProcInfoNeeded, ref uint pnProcInfo,
        [In] [Out] RM_PROCESS_INFO[]? rgAffectedApps, ref uint lpdwRebootReasons);

    [StructLayout(LayoutKind.Sequential)]
    private struct RM_UNIQUE_PROCESS
    {
        public int dwProcessId;
        public FILETIME ProcessStartTime;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_APP_NAME + 1)]
        public string strAppName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_SVC_NAME + 1)]
        public string strServiceShortName;

        public uint ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)] public bool bRestartable;
    }
}