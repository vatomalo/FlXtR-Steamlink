using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace FlXtR.Gamez;

public sealed class Bridge(Catalog catalog) : IDisposable
{
    private readonly HttpListener listener = new();
    private GameJob? current;
    private Control? dispatcher;
    private GameShell? shell;
    private string? gameId,sessionId;
    private string state="idle";
    private bool viewer;
    private long phaseAt,lastPoll,lastShellCheck;
    public string Status { get; private set; } = "Ready";

    internal void AttachShell(Control control,GameShell gameShell) { dispatcher=control;shell=gameShell; _=control.Handle; }
    private T OnUI<T>(Func<T> operation) => dispatcher is {IsDisposed:false,InvokeRequired:true}
        ? (T)dispatcher.Invoke(operation) : operation();

    public bool Launch(string id) => OnUI(() => LaunchCore(id));
    private bool LaunchCore(string id)
    {
        lock (catalog)
        {
            var game = catalog.Games.SingleOrDefault(item => item.Id == id);
            if (game == null || !game.Approved) throw new InvalidDataException("Unknown or unapproved game");
            if (!Catalog.Supported(game.Exe) || Catalog.Fingerprint(game.Exe) != game.Hash) throw new InvalidDataException("Executable changed; review again");
            if (current is not null && current.Running)
            {
                if(gameId!=id)return false;
                shell?.Prepare();current.Activate();return true; // Resume, never duplicate.
            }
            var directory = string.IsNullOrEmpty(game.Cwd) ? Path.GetDirectoryName(game.Exe)! : game.Cwd;
            if (!Path.IsPathFullyQualified(directory) || !Directory.Exists(directory)) throw new InvalidDataException("Working directory missing");
            current?.Dispose();
            current = null;
            var job=new GameJob();
            try
            {
                shell?.Prepare(); // Black first, Explorer stopped before CreateProcess.
                job.Start(game.Exe,directory,game.Args);
                current=job;gameId=id;sessionId=Guid.NewGuid().ToString("N");
                state="running";viewer=false;phaseAt=Environment.TickCount64;
            }
            catch { job.Dispose();shell?.Restore();throw; }
            Status = "Launched " + game.Title;
            return true;
        }
    }

    public void CloseGame()
    {
        OnUI(() =>
        {
            if (current is not null && current.Running)
            {
                current.RequestClose();
                Status = "Requested game exit; save your progress";
            }
            return true;
        });
    }

    internal void PollSession()
    {
        var now=Environment.TickCount64;
        if(now-lastPoll<200)return;lastPoll=now;
        try
        {
            if(state=="running"&&current is not null)
            {
                if(now-lastShellCheck>1000) { shell?.KeepExplorerStopped();lastShellCheck=now; }
                current.ShowNewWindows();
            }
            if(state=="running" && current is not null && !current.Running)
            {
                state="ended";phaseAt=now;Status="Game ended / returning to FlXtR";
                shell?.CoverExit();
                if(!viewer)shell?.Restore();
            }
            if((state=="running"&&!viewer&&now-phaseAt>30000)||(state=="ended"&&now-phaseAt>15000))
                shell?.Restore(); // Startup/host failure recovery, never kill the game.
        }
        catch(Exception error) when(error is System.ComponentModel.Win32Exception or IOException or InvalidOperationException)
        { state="failed";Status=error.Message;shell?.Restore(); }
    }

    public object SessionStatus() => OnUI<object>(() => {
        PollSession();return new {state,game_id=gameId,session_id=sessionId};
    });
    public bool PrepareViewer() => OnUI(() => {
        PollSession();if(state!="running")return false;
        shell?.Prepare();current!.Activate();viewer=true;return true;
    });
    public void RestoreSession() => OnUI(() => { viewer=false;shell?.Restore();return true; });

    public void Start()
    {
        if (catalog.Token.Length < 32 || !IPAddress.TryParse(catalog.Bind, out _) || catalog.AllowedClients.Length == 0)
            throw new InvalidDataException("Configure a bind IP, allowed clients and a token of at least 32 characters");
        listener.Prefixes.Add($"http://{catalog.Bind}:{catalog.Port}/");
        listener.Start();
        _ = Task.Run(async () =>
        {
            while (listener.IsListening)
            {
                try { var context = await listener.GetContextAsync(); _ = Task.Run(() => Handle(context)); }
                catch (Exception error) when (error is HttpListenerException or ObjectDisposedException) { break; }
            }
        });
    }

    private async Task Handle(HttpListenerContext context)
    {
        var request = context.Request;
        var code = 200;
        object payload = new { status = "ok" };
        try
        {
            var remote = request.RemoteEndPoint?.Address.ToString();
            var supplied = Encoding.UTF8.GetBytes(request.Headers["Authorization"] ?? "");
            var expected = Encoding.UTF8.GetBytes("Bearer " + catalog.Token);
            if (!catalog.AllowedClients.Contains(remote) || !CryptographicOperations.FixedTimeEquals(supplied, expected)) code = 401;
            else if (request.HttpMethod == "GET" && request.Url?.AbsolutePath == "/v1/games")
            {
                lock (catalog) payload = new { games = catalog.Games.Where(game => game.Approved).Select(game => new { id = game.Id, title = game.Title }).ToArray() };
            }
            else if(request.HttpMethod=="GET" && request.Url?.AbsolutePath=="/v1/session") payload=SessionStatus();
            else if(request.HttpMethod=="POST" && request.Url?.AbsolutePath=="/v1/session/prepare") code=PrepareViewer()?200:409;
            else if(request.HttpMethod=="POST" && request.Url?.AbsolutePath=="/v1/session/restore") RestoreSession();
            else if (request.HttpMethod == "POST" && request.Url?.AbsolutePath == "/v1/launch")
            {
                if (request.ContentLength64 is < 1 or > 32) code = 400;
                else
                {
                    var bytes = new byte[(int)request.ContentLength64];
                    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                    await request.InputStream.ReadExactlyAsync(bytes, timeout.Token);
                    var id = Encoding.ASCII.GetString(bytes).Trim();
                    if (!id.All(char.IsAsciiLetterOrDigit)) code = 400;
                    else { code = Launch(id) ? 200 : 409; payload = new { status = code == 200 ? "launched" : "busy", gameId = id }; }
                }
            }
            else code = 404;
        }
        catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or InvalidOperationException or System.ComponentModel.Win32Exception or OperationCanceledException or ArgumentException)
        { code = 422; Status = error.Message; }
        try
        {
            if (code != 200) payload = new { status = "rejected", code };
            var bytes = JsonSerializer.SerializeToUtf8Bytes(payload);
            context.Response.StatusCode = code;
            context.Response.ContentType = "application/json";
            context.Response.ContentLength64 = bytes.Length;
            await context.Response.OutputStream.WriteAsync(bytes);
        }
        catch (Exception error) when (error is IOException or HttpListenerException or ObjectDisposedException) { }
        finally { context.Response.Close(); }
    }

    public void Dispose() { listener.Close(); current?.Dispose(); }
}
