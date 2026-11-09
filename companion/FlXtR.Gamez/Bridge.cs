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
    private Process? current;
    public string Status { get; private set; } = "Ready";

    public bool Launch(string id)
    {
        lock (catalog)
        {
            var game = catalog.Games.SingleOrDefault(item => item.Id == id);
            if (game == null || !game.Approved) throw new InvalidDataException("Unknown or unapproved game");
            if (!Catalog.Supported(game.Exe) || Catalog.Fingerprint(game.Exe) != game.Hash) throw new InvalidDataException("Executable changed; review again");
            if (current is { HasExited: false }) return false;
            var directory = string.IsNullOrEmpty(game.Cwd) ? Path.GetDirectoryName(game.Exe)! : game.Cwd;
            if (!Path.IsPathFullyQualified(directory) || !Directory.Exists(directory)) throw new InvalidDataException("Working directory missing");
            var start = new ProcessStartInfo(game.Exe) { WorkingDirectory = directory, UseShellExecute = false };
            foreach (var argument in game.Args) start.ArgumentList.Add(argument);
            current?.Dispose();
            current = Process.Start(start) ?? throw new IOException("Could not start game");
            Status = "Launched " + game.Title;
            return true;
        }
    }

    public void CloseGame()
    {
        lock (catalog)
        {
            if (current is { HasExited: false })
            {
                current.CloseMainWindow();
                Status = "Requested game exit; save your progress";
            }
        }
    }

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
