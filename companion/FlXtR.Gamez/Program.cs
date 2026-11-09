using System.Diagnostics;
using Microsoft.Win32;

namespace FlXtR.Gamez;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
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
            using var bridge = new Bridge(catalog);
            using var window = new CompanionWindow(catalog, bridge);
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
        scan = Add(actions, "Scan libraries", async () => await Scan());
        Add(actions, "Approve selected", () => Review(true));
        Add(actions, "Disable selected", () => Review(false));
        Add(actions, "Launch selected", Launch);
        Add(actions, "Exit game", bridge.CloseGame);
        Add(actions, "Open settings / overrides", () => Process.Start(new ProcessStartInfo("notepad.exe") { ArgumentList = { Catalog.FilePath } }));
        cancelScan = new Button { Text = "Stop scan", Dock = DockStyle.Bottom, Height = 30, Visible = false };
        cancelScan.Click += (_, _) => { scanCancellation?.Cancel(); scanStatus.Text = "Stopping scan after the current file…"; };
        Controls.Add(games);
        Controls.Add(status);
        Controls.Add(scanStatus);
        Controls.Add(cancelScan);
        Controls.Add(actions);
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
        if (scanning) return;
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
        var selected = games.SelectedIndex;
        games.Items.Clear();
        foreach (var game in catalog.Games) games.Items.Add($"{(game.Approved ? "READY" : "REVIEW")} | {game.Title} | {game.Id} | {game.Exe}");
        if (games.Items.Count > 0) games.SelectedIndex = Math.Clamp(selected, 0, games.Items.Count - 1);
    }

    private void Review(bool approved)
    {
        if (games.SelectedIndex < 0) return;
        var game = catalog.Games[games.SelectedIndex];
        if (approved)
        {
            if (!Catalog.Supported(game.Exe)) throw new InvalidDataException("Choose an executable inside a Gamez root, without directory links");
            if (MessageBox.Show($"Allow this executable to launch?\n\n{game.Exe}\n\nID: {game.Id}", "Review game", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
            game.Hash = Catalog.Fingerprint(game.Exe);
            var info = new FileInfo(game.Exe);
            game.ReviewedLength = info.Length;
            game.ReviewedWriteTime = info.LastWriteTimeUtc.Ticks;
        }
        game.Approved = approved;
        catalog.Save();
        RefreshGames();
    }

    private void Launch()
    {
        if (games.SelectedIndex >= 0) bridge.Launch(catalog.Games[games.SelectedIndex].Id);
    }

    private void Poll()
    {
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
