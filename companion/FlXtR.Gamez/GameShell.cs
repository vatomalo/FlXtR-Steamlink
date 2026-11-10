using System.Diagnostics;

namespace FlXtR.Gamez;

internal sealed class BlackBackdrop : Form
{
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams
    {
        get { var value=base.CreateParams; value.ExStyle |= 0x08000000 | 0x80; return value; }
    }
    public BlackBackdrop()
    {
        BackColor=Color.Black; FormBorderStyle=FormBorderStyle.None;
        ShowInTaskbar=false; StartPosition=FormStartPosition.Manual;
        Bounds=Screen.PrimaryScreen!.Bounds;
    }
}

internal sealed class GameShell : IDisposable
{
    private BlackBackdrop? backdrop;
    private readonly System.Windows.Forms.Timer restoreTimer = new() { Interval=2500 };
    private bool hidden;
    public GameShell() { restoreTimer.Tick += (_,_) => { restoreTimer.Stop(); backdrop?.Hide(); }; }

    private static IEnumerable<Process> Explorers()
    {
        var session=Process.GetCurrentProcess().SessionId;
        var expected=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"explorer.exe");
        foreach(var process in Process.GetProcessesByName("explorer"))
        {
            bool ours;
            try { ours=process.SessionId==session && string.Equals(process.MainModule?.FileName,expected,StringComparison.OrdinalIgnoreCase); }
            catch { process.Dispose(); continue; }
            if(ours) yield return process; else process.Dispose();
        }
    }

    public void Prepare()
    {
        restoreTimer.Stop();
        if(!hidden)
        {
            backdrop ??= new BlackBackdrop();
            backdrop.Bounds=Screen.PrimaryScreen!.Bounds;
            backdrop.TopMost=true;
            backdrop.Show(); backdrop.BringToFront(); backdrop.Refresh();
        }
        hidden=true;
        try
        {
            foreach(var process in Explorers())
                using(process) { if(!process.HasExited)process.Kill(); if(!process.WaitForExit(3000)) throw new IOException("Explorer did not stop"); }
        }
        catch { Restore(); throw; }
    }

    public void CoverExit()
    {
        if(backdrop is {IsDisposed:false}) { backdrop.TopMost=true; backdrop.Show(); backdrop.BringToFront(); }
    }

    public void KeepExplorerStopped()
    {
        if(!hidden)return;
        foreach(var process in Explorers())
            using(process) { try { if(!process.HasExited)process.Kill(); } catch(InvalidOperationException) { } }
    }

    public static void RestoreExplorer()
    {
        var processes=Explorers().ToArray();
        var exists=processes.Length!=0;
        foreach(var process in processes) process.Dispose();
        if(!exists) Process.Start(new ProcessStartInfo(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.Windows),"explorer.exe")) { UseShellExecute=true });
    }

    public void Restore()
    {
        if(!hidden) return;
        CoverExit();
        RestoreExplorer();
        hidden=false;
        // Sunshine finishes its undo command before Moonlight returns to FlXtR.
        // Keep Explorer behind black during that short teardown interval.
        restoreTimer.Stop(); restoreTimer.Start();
    }

    public void Dispose()
    {
        if(hidden) RestoreExplorer();
        restoreTimer.Dispose(); backdrop?.Dispose();
    }
}
