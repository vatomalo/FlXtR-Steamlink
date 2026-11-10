using System.Diagnostics;
using Microsoft.Win32;

namespace FlXtR.Gamez;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        if(SessionSelfTest.Handle(args))return;
        if(SunshineSessionCli.IsCommand(args)) { Environment.ExitCode=SunshineSessionCli.Run(args);return; }
        ApplicationConfiguration.Initialize();
        using var mutex = new Mutex(true, @"Local\FlXtR.Gamez", out var first);
        if (!first) return;
        try
        {
            var catalog = Catalog.Load();
            catalog.Save();
            if (args.Contains("--startup"))
            {
                using var key = Registry.CurrentUser.CreateSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
                key.SetValue("FlXtR Gamez", $"\"{Environment.ProcessPath}\" --background");
            }
            using var shell = new GameShell();
            using var bridge = new Bridge(catalog);
            using var window = new CompanionWindow(catalog, bridge);
            bridge.AttachShell(window,shell);
            try { bridge.Start(); }
            catch (Exception error) { MessageBox.Show("Gamez network integration unavailable: " + error.Message); }
            Application.Run(window);
        }
        catch (Exception error) { MessageBox.Show(error.Message, "FlXtR Gamez"); }
    }
}

public sealed class CompanionWindow : Form
{
    private readonly Catalog catalog;
    private readonly Bridge bridge;
    private readonly ListBox games = new() { Dock = DockStyle.Fill, Font = new Font("Segoe UI", 13) };
    private readonly List<Game> shownGames=[];
    private readonly TextBox gameFilter=new(){Width=530,PlaceholderText="Find a game by name or executable path…"};
    private readonly ComboBox approvalFilter=new(){Width=190,DropDownStyle=ComboBoxStyle.DropDownList};
    private readonly Label status = new() { Dock = DockStyle.Bottom, Height = 52 };
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 40 };
    private readonly NotifyIcon tray;
    private readonly KeyboardWindow keyboard = new();
    private ushort previous;
    private bool navigation;
    private long lastMove;
    private readonly Button scan;
    private readonly FlowLayoutPanel actions;
    private long lastScan = Environment.TickCount64;
    private bool scanning;
    private bool reviewing;
    private CancellationTokenSource? scanCancellation;
    private readonly Label scanStatus = new() { Dock = DockStyle.Bottom, Height = 48, AutoEllipsis = true };
    private readonly Button cancelScan;

    public CompanionWindow(Catalog catalog, Bridge bridge)
    {
        this.catalog = catalog;
        this.bridge = bridge;
        Text = "FlXtR Gamez — review discovered games";
        Size = new Size(1050, 650);
        actions = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 80, AutoSize = true };
        Add(actions,"Add game…",()=>AddGame());
        Add(actions,"Add library folder…",AddLibrary);
        scan = Add(actions, "Scan libraries", async () => await Scan());
        AddTask(actions, "Approve selected", () => Review(true));
        AddTask(actions, "Disable selected", () => Review(false));
        Add(actions, "Rename selected", RenameSelected);
        Add(actions, "Launch selected", Launch);
        Add(actions, "Exit game", bridge.CloseGame);
        Add(actions, "Open settings / overrides", () => Process.Start(new ProcessStartInfo("notepad.exe") { ArgumentList = { Catalog.FilePath } }));
        cancelScan = new Button { Text = "Stop scan", Dock = DockStyle.Bottom, Height = 30, Visible = false };
        cancelScan.Click += (_, _) => { scanCancellation?.Cancel(); scanStatus.Text = "Stopping scan after the current file…"; };
        Controls.Add(games);
        Controls.Add(status);
        Controls.Add(scanStatus);
        Controls.Add(cancelScan);
        var find=new FlowLayoutPanel{Dock=DockStyle.Top,Height=38};
        approvalFilter.Items.AddRange(["All games","Needs review","Approved"]);approvalFilter.SelectedIndex=0;
        find.Controls.AddRange([gameFilter,approvalFilter]);Controls.Add(find);
        gameFilter.TextChanged+=(_,_)=>RefreshGames();approvalFilter.SelectedIndexChanged+=(_,_)=>RefreshGames();
        Controls.Add(actions);
        AllowDrop=true;
        DragEnter+=(_,eventArgs)=>{if(eventArgs.Data?.GetDataPresent(DataFormats.FileDrop)==true)eventArgs.Effect=DragDropEffects.Copy;};
        DragDrop+=(_,eventArgs)=>{
            if(eventArgs.Data?.GetData(DataFormats.FileDrop) is not string[] paths)return;
            try{bool folders=false;foreach(var path in paths){if(File.Exists(path)&&Path.GetExtension(path).Equals(".exe",StringComparison.OrdinalIgnoreCase))AddGame(path);else if(Directory.Exists(path)){catalog.AddLibrary(path);folders=true;}}if(folders)_ = Scan();}
            catch(Exception error){MessageBox.Show(this,error.Message,"Add game library");}
        };
        tray = new NotifyIcon { Icon = SystemIcons.Application, Text = "FlXtR Gamez", Visible = true };
        tray.DoubleClick += (_, _) => { Show(); Activate(); };
        var menu = new ContextMenuStrip();
        menu.Items.Add("Open Gamez", null, (_, _) => { Show(); Activate(); });
        menu.Items.Add("Quit companion", null, (_, _) => { tray.Visible = false; Application.Exit(); });
        tray.ContextMenuStrip = menu;
        FormClosing += (_, eventArgs) => { if (eventArgs.CloseReason == CloseReason.UserClosing) { eventArgs.Cancel = true; Hide(); } };
        timer.Tick += (_, _) => Poll();
        timer.Start();
        Shown += async (_, _) => { if (Environment.GetCommandLineArgs().Contains("--background")) Hide(); await Scan(); };
        RefreshGames();
    }

    private static Button Add(Control panel, string text, Action action)
    {
        var button = new Button { Text = text, AutoSize = true, Height = 36 };
        button.Click += (_, _) => { try { action(); } catch (Exception error) { MessageBox.Show(error.Message, "FlXtR Gamez"); } };
        panel.Controls.Add(button);
        return button;
    }

    private async Task Scan()
    {
        if (scanning||reviewing) return;
        scanning = true;
        scanCancellation = new CancellationTokenSource();
        actions.Enabled = games.Enabled = false;
        cancelScan.Visible = true;
        scanStatus.Text = "Scanning library roots; executables remain disabled until approved.";
        var progress = new Progress<string>(message => { if (!IsDisposed && scanning && scanCancellation is { IsCancellationRequested: false }) scanStatus.Text = message; });
        try
        {
            await Task.Run(() => catalog.Scan(progress, scanCancellation.Token));
            scanStatus.Text = $"Scan complete — {catalog.Games.Count} candidates. Select and approve the game executable.";
        }
        catch (OperationCanceledException) { scanStatus.Text = "Scan stopped. Candidates already found are saved."; }
        catch (Exception error) { MessageBox.Show(error.Message); }
        finally
        {
            if (!IsDisposed) { RefreshGames(); actions.Enabled = games.Enabled = true; cancelScan.Visible = false; }
            scanning = false;
            lastScan = Environment.TickCount64;
            scanCancellation.Dispose();
            scanCancellation = null;
        }
    }

    private void RefreshGames()
    {
        var selected=SelectedGame()?.Id;
        Game[] snapshot;lock(catalog)snapshot=catalog.Games.Where(game=>(approvalFilter.SelectedIndex!=1||!game.Approved)&&(approvalFilter.SelectedIndex!=2||game.Approved)&&
            (game.Title.Contains(gameFilter.Text,StringComparison.OrdinalIgnoreCase)||game.Exe.Contains(gameFilter.Text,StringComparison.OrdinalIgnoreCase)))
            .OrderBy(game=>game.Title,StringComparer.OrdinalIgnoreCase).ToArray();
        games.BeginUpdate();
        games.Items.Clear();
        shownGames.Clear();shownGames.AddRange(snapshot);
        foreach (var game in shownGames) games.Items.Add($"{(game.Approved ? "READY" : "REVIEW")} | {game.Title} | {game.Exe}");
        if(games.Items.Count>0)games.SelectedIndex=Math.Max(0,shownGames.FindIndex(game=>game.Id==selected));
        games.EndUpdate();
    }
    private Game? SelectedGame()=>games.SelectedIndex>=0&&games.SelectedIndex<shownGames.Count?shownGames[games.SelectedIndex]:null;
    private void AddGame(string? path=null){
        using var dialog=new AddGameDialog(catalog,path);
        if(dialog.ShowDialog(this)!=DialogResult.OK||dialog.AddedGame is null)return;
        gameFilter.Text="";approvalFilter.SelectedIndex=0;RefreshGames();
        games.SelectedIndex=shownGames.FindIndex(game=>game.Id==dialog.AddedGame.Id);
    }
    private void AddLibrary(){
        using var picker=new FolderBrowserDialog{Description="Choose a game library folder to scan",UseDescriptionForTitle=true,ShowNewFolderButton=false};
        if(picker.ShowDialog(this)==DialogResult.OK){catalog.AddLibrary(picker.SelectedPath);_ = Scan();}
    }
    internal void RefreshCatalog() => RefreshGames();

    private static Button AddTask(Control panel,string text,Func<Task> action){
        var button=new Button{Text=text,AutoSize=true,Height=36};
        button.Click+=async(_,_)=>{try{await action();}catch(Exception error){MessageBox.Show(error.Message,"FlXtR Gamez");}};
        panel.Controls.Add(button);return button;
    }
    private async Task Review(bool approved)
    {
        if (games.SelectedIndex < 0) return;
        var game = SelectedGame();if(game is null)return;
        if (approved)
        {
            if (MessageBox.Show($"Allow this executable to launch?\n\n{game.Exe}\n\nID: {game.Id}", "Review game", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
        }
        reviewing=true;actions.Enabled=games.Enabled=false;
        try{
            string hash="";long length=0,modified=0;
            if(approved){
                var check=await Task.Run(()=>{
                    if(!catalog.Supports(game.Exe))throw new InvalidDataException("Choose an executable in a configured library or use Add game");
                    var info=new FileInfo(game.Exe);var size=info.Length;var time=info.LastWriteTimeUtc.Ticks;
                    var fingerprint=Catalog.Fingerprint(game.Exe);info.Refresh();
                    if(info.Length!=size||info.LastWriteTimeUtc.Ticks!=time)throw new IOException("Executable changed while being checked; try again");
                    return (fingerprint,size,time);
                });
                (hash,length,modified)=check;
            }
            lock(catalog){if(approved){game.Hash=hash;game.ReviewedLength=length;game.ReviewedWriteTime=modified;}game.Approved=approved;catalog.Save();}
            RefreshGames();
        }finally{reviewing=false;if(!IsDisposed)actions.Enabled=games.Enabled=true;}
    }

    private void Launch()
    {
        var game=SelectedGame();if(game is not null)bridge.Launch(game.Id);
    }

    private void RenameSelected()
    {
        if(games.SelectedIndex<0)return;
        var game=SelectedGame();if(game is null)return;
        using var dialog=new Form { Text="Rename game",FormBorderStyle=FormBorderStyle.FixedDialog,
            StartPosition=FormStartPosition.CenterParent,ClientSize=new Size(460,115),MinimizeBox=false,MaximizeBox=false,ShowInTaskbar=false };
        var name=new TextBox { Text=game.Title,Left=16,Top=16,Width=428,MaxLength=79 };
        var save=new Button {Text="Save",Left=264,Top=62,Width=85,DialogResult=DialogResult.None};
        var cancel=new Button {Text="Cancel",Left=359,Top=62,Width=85,DialogResult=DialogResult.Cancel};
        save.Click+=(_,_)=>{
            try { catalog.Rename(game.Id,name.Text);dialog.DialogResult=DialogResult.OK; }
            catch(InvalidDataException error) { MessageBox.Show(dialog,error.Message,"Rename game"); }
        };
        dialog.Controls.AddRange([name,save,cancel]);dialog.AcceptButton=save;dialog.CancelButton=cancel;
        dialog.Shown+=(_,_)=>{name.Focus();name.SelectAll();};
        if(dialog.ShowDialog(this)==DialogResult.OK)RefreshGames();
    }

    private void Poll()
    {
        bridge.PollSession();
        if (!scanning && Environment.TickCount64 - lastScan > 300000) _ = Scan();
        if (!Native.State(out var pad)) { previous = 0; return; }
        var pressed = (ushort)(pad.Buttons & ~previous);
        var chord = (pad.Buttons & 0x30) == 0x30;
        if (chord && (previous & 0x30) != 0x30)
        {
            navigation = !navigation;
            if (navigation) keyboard.Show(); else keyboard.Hide();
        }
        previous = pad.Buttons;
        status.Text = bridge.Status + "\nStart + Back: keyboard/mouse mode; LB + Y: Gamez; LB + RB + Back: request game exit";
        if ((pad.Buttons & 0x310) == 0x310 && (pressed & 0x310) != 0) { bridge.CloseGame(); return; }
        if ((pad.Buttons & 0x100) != 0 && (pressed & 0x8000) != 0) { Show(); Activate(); }
        if (!navigation || chord) return;
        if ((pad.Buttons & 0x100) != 0)
        {
            var cursor = Cursor.Position;
            Cursor.Position = new Point(cursor.X + Axis(pad.LX), cursor.Y - Axis(pad.LY));
            if ((pressed & 0x4000) != 0) Native.Mouse(false);
            if ((pressed & 0x2000) != 0) Native.Mouse(true);
            return;
        }
        var now = Environment.TickCount64;
        if (now - lastMove > 160 && (pad.Buttons & 15) != 0)
        {
            keyboard.MoveSelection((pad.Buttons & 8) != 0 ? 1 : (pad.Buttons & 4) != 0 ? -1 : 0,
                (pad.Buttons & 2) != 0 ? 1 : (pad.Buttons & 1) != 0 ? -1 : 0);
            lastMove = now;
        }
        if ((pressed & 0x1000) != 0) keyboard.Press();
        if ((pressed & 0x4000) != 0) Native.Key(8);
        if ((pressed & 0x2000) != 0) Native.Key(27);
        if ((pressed & 0x8000) != 0) keyboard.Size = keyboard.Width > 1000 ? new Size(900, 310) : new Size(1200, 420);
        if (pad.LT > 30 && pad.RT > 30) keyboard.Location = new Point(keyboard.Left + Axis(pad.LX), keyboard.Top - Axis(pad.LY));
    }

    private static int Axis(short value) => Math.Abs((int)value) < 8000 ? 0 : value / 2000;
    protected override void Dispose(bool disposing)
    {
        if (disposing) { scanCancellation?.Cancel(); timer.Dispose(); tray.Dispose(); keyboard.Dispose(); }
        base.Dispose(disposing);
    }
}
