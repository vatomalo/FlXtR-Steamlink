using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace FlXtR.Gamez;

/// <summary>Tracks the bootstrap executable and all normally inherited children.</summary>
internal sealed class GameJob : IDisposable
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public int Size; public string? Reserved, Desktop, Title;
        public uint X,Y,Width,Height,CharsX,CharsY,Fill,Flags;
        public short Show,ReservedSize; public IntPtr ReservedBytes,Input,Output,Error;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo { public IntPtr Process,Thread; public uint Pid,Tid; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Accounting
    {
        public long User,Kernel,PeriodUser,PeriodKernel;
        public uint Faults,Total,Active,Terminated;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern SafeFileHandle CreateJobObject(IntPtr security, string? name);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    private static extern SafeFileHandle OpenJobObject(uint access,bool inherit,string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool AssignProcessToJobObject(SafeFileHandle job, IntPtr process);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool CreateProcess(string application, StringBuilder command, IntPtr processSecurity,
        IntPtr threadSecurity, bool inherit, uint flags, IntPtr environment, string directory,
        ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] private static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll", EntryPoint="QueryInformationJobObject", SetLastError=true)]
    private static extern bool QueryAccounting(SafeFileHandle job, int type, out Accounting data, int length, IntPtr resultLength);
    [DllImport("kernel32.dll", EntryPoint="QueryInformationJobObject", SetLastError=true)]
    private static extern bool QueryProcesses(SafeFileHandle job, int type, IntPtr data, int length, out int resultLength);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool IsProcessInJob(IntPtr process, SafeFileHandle job, out bool member);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [StructLayout(LayoutKind.Sequential)] private struct WindowRect {public int Left,Top,Right,Bottom;}
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window,out WindowRect rectangle);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr window,int command);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] private static extern IntPtr WindowStyle(IntPtr window,int index);
    internal sealed record WindowInfo(int Pid,string Process,int X,int Y,int Width,int Height,bool Minimized);
    private bool fullscreenWindows;
    private readonly HashSet<IntPtr> visibleWindows = [];
    private readonly SafeFileHandle handle;
    public string? Name {get;}
    public GameJob(string? name=null){Name=name;handle=CreateJobObject(IntPtr.Zero,name);}
    private GameJob(string name,bool existing){Name=name;handle=OpenJobObject(4,false,name);if(handle.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());}
    public static GameJob Open(string name)=>new(name,true);

    public static string Quote(string value)
    {
        var result = new StringBuilder("\""); var slashes = 0;
        foreach (var c in value)
        {
            if (c == '\\') { slashes++; continue; }
            if (c == '"') result.Append('\\', slashes * 2 + 1).Append(c);
            else result.Append('\\', slashes).Append(c);
            slashes = 0;
        }
        return result.Append('\\', slashes * 2).Append('"').ToString();
    }

    public void Start(string executable, string directory, IReadOnlyList<string> arguments)
    {
        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        var startup = new StartupInfo { Size=Marshal.SizeOf<StartupInfo>() };
        var command = new StringBuilder(string.Join(" ", new[] { executable }.Concat(arguments).Select(Quote)));
        // Assign while suspended so even a very short bootstrap cannot escape tracking.
        if (!CreateProcess(executable, command, IntPtr.Zero, IntPtr.Zero, false, 0x4 | 0x200,
                           IntPtr.Zero, directory, ref startup, out var process))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            if (!AssignProcessToJobObject(handle, process.Process)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (ResumeThread(process.Thread) == uint.MaxValue) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        catch { TerminateProcess(process.Process, 1); throw; } // Only our new, suspended process.
        finally { CloseHandle(process.Thread); CloseHandle(process.Process); }
    }

    public bool Running
    {
        get
        {
            if (!QueryAccounting(handle, 1, out var info, Marshal.SizeOf<Accounting>(), IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return info.Active != 0;
        }
    }

    public void RequestClose() => VisitProcesses(process => process.CloseMainWindow());
    public bool Activate(bool fullscreen=false)
    {
        fullscreenWindows|=fullscreen;
        var windows=new List<(IntPtr Handle,long Area)>();
        VisitProcesses(process=>{
            var window=process.MainWindowHandle;
            if(window!=IntPtr.Zero&&GetWindowRect(window,out var bounds))
                windows.Add((window,(long)Math.Max(0,bounds.Right-bounds.Left)*Math.Max(0,bounds.Bottom-bounds.Top)));
        });
        // Launchers/splash windows must not win over the main game when resuming.
        var ordered=windows.DistinctBy(item=>item.Handle).OrderBy(item=>item.Area).ToArray();
        for(int i=0;i<ordered.Length;i++)ShowGameWindow(ordered[i].Handle,i==ordered.Length-1);
        return ordered.Length>0;
    }
    internal static int FitAction(long style,long extendedStyle,int width,int height,int screenWidth,int screenHeight)
    {
        // Do not stretch dialogs, tool windows or tiny launch splashes.
        if((extendedStyle&0x80)!=0||width<640||height<360||screenWidth<=0||screenHeight<=0)return 0;
        if(Math.Abs(width-screenWidth)<=32&&Math.Abs(height-screenHeight)<=32)return 0;
        if((style&(0x40000|0x10000))!=0)return 3; // SW_MAXIMIZE for ordinary sizable games.
        return (style&0xC00000)==0?1:0; // Fit a borderless game; leave fixed-size captioned dialogs.
    }
    private void ShowGameWindow(IntPtr window,bool focus=true)
    {
        if(IsIconic(window))ShowWindowAsync(window,9); // SW_RESTORE; never leave a game minimized behind black.
        if(fullscreenWindows&&GetWindowRect(window,out var rectangle)&&Screen.PrimaryScreen is {} screen){
            var bounds=screen.Bounds;
            int action=FitAction(WindowStyle(window,-16).ToInt64(),WindowStyle(window,-20).ToInt64(),
                rectangle.Right-rectangle.Left,rectangle.Bottom-rectangle.Top,bounds.Width,bounds.Height);
            if(action==3)ShowWindowAsync(window,3);
            else if(action==1)SetWindowPos(window,new IntPtr(-1),bounds.X,bounds.Y,bounds.Width,bounds.Height,0x10|0x4000);
        }
        SetWindowPos(window,new IntPtr(-1),0,0,0,0,0x1|0x2|0x10|0x4000);
        if(focus)_=Task.Run(()=>SetForegroundWindow(window));
        visibleWindows.Add(window);
    }
    internal WindowInfo[] WindowDiagnostics(){
        var result=new List<WindowInfo>();
        VisitProcesses(process=>{var window=process.MainWindowHandle;if(window!=IntPtr.Zero&&GetWindowRect(window,out var rect))
            result.Add(new WindowInfo(process.Id,process.ProcessName,rect.Left,rect.Top,rect.Right-rect.Left,rect.Bottom-rect.Top,IsIconic(window)));});
        return result.ToArray();
    }
    public void ShowNewWindows() => VisitProcesses(process => {
        var window=process.MainWindowHandle;
        if(window!=IntPtr.Zero&&!visibleWindows.Contains(window))ShowGameWindow(window);
    });
    private void VisitProcesses(Action<Process> operation)
    {
        var size = 65536;
        var memory = Marshal.AllocHGlobal(size);
        try
        {
            if (!QueryProcesses(handle, 3, memory, size, out _)) throw new Win32Exception(Marshal.GetLastWin32Error());
            var count = Marshal.ReadInt32(memory, 4);
            if (count < 0 || count > (size-8)/IntPtr.Size) throw new IOException("Invalid job process count");
            for (var i=0;i<count;i++)
            {
                var id = Marshal.ReadIntPtr(memory, 8+i*IntPtr.Size).ToInt64();
                try
                {
                    using var process = Process.GetProcessById(checked((int)id));
                    if (!process.HasExited && IsProcessInJob(process.Handle, handle, out var member) && member)
                        operation(process);
                }
                catch (Exception error) when (error is ArgumentException or InvalidOperationException or Win32Exception) { }
            }
        }
        finally { Marshal.FreeHGlobal(memory); }
    }

    // No KILL_ON_JOB_CLOSE: a companion failure must not kill unsaved gameplay.
    public void Dispose() => handle.Dispose();
}
