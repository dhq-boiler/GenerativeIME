namespace GenerativeIME.Installer.Models;

public sealed record InstallProgress(string Message, double Fraction);

public sealed record LockingProcessInfo(int Pid, string ProcessName, string? ExecutablePath);