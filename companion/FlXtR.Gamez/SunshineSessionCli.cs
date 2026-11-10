using System.Diagnostics;
using System.Net.Http.Headers;
using System.Text.Json;

namespace FlXtR.Gamez;

internal static class SunshineSessionCli
{
    public static bool IsCommand(string[] args) => args.Any(a => a is "--wait-session" or "--prepare-session" or "--restore-shell" or "--restore-delayed" or "--hold-job");
    public static int Run(string[] args)
    {
        if(args.Length==2&&args[0]=="--hold-job"){
            try {using var job=GameJob.Open(args[1]);while(job.Running)Thread.Sleep(250);return 0;}
            catch(System.ComponentModel.Win32Exception){return 1;}
        }
        if(args.Contains("--restore-delayed")) { Thread.Sleep(3000);GameShell.RestoreExplorer();return 0; }
        try
        {
            var catalog=Catalog.Load();
            using var client=new HttpClient { BaseAddress=new Uri($"http://{catalog.Bind}:{catalog.Port}"),Timeout=TimeSpan.FromSeconds(3) };
            client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer",catalog.Token);
            if(args.Contains("--prepare-session"))
                return client.PostAsync("/v1/session/prepare",new ByteArrayContent([])).GetAwaiter().GetResult().IsSuccessStatusCode?0:1;
            if(args.Contains("--restore-shell"))
            {
                var response=client.PostAsync("/v1/session/restore",new ByteArrayContent([])).GetAwaiter().GetResult();
                if(!response.IsSuccessStatusCode)throw new IOException("Shell restore rejected");
                return 0;
            }
            string? session=null;
            var failures=0;var displayChecks=0;var connected=Stopwatch.StartNew();
            while(true)
            {
                try
                {
                    using var data=JsonDocument.Parse(client.GetStringAsync("/v1/session").GetAwaiter().GetResult());
                    var root=data.RootElement;
                    var state=root.GetProperty("state").GetString();
                    var id=root.GetProperty("session_id").GetString();
                    session ??= id;
                    if(state is not ("running" or "starting") || id!=session)return 0;
                    // Sunshine/display changes can settle after prep. Re-check only
                    // during startup; never keep stealing focus during gameplay.
                    if(displayChecks<2&&connected.ElapsedMilliseconds>=(displayChecks==0?1000:3000)){
                        using var prepared=client.PostAsync("/v1/session/prepare",new ByteArrayContent([])).GetAwaiter().GetResult();
                        if(prepared.IsSuccessStatusCode)displayChecks++;
                    }
                    failures=0;
                }
                catch(Exception error) when(error is HttpRequestException or TaskCanceledException or JsonException)
                { if(++failures>=3)return 1; }
                Thread.Sleep(250);
            }
        }
        catch(Exception error)
        {
            try { File.AppendAllText(Path.Combine(Catalog.DirectoryPath,"session-errors.log"),DateTime.UtcNow+" "+error.Message+Environment.NewLine); } catch { }
            if(args.Contains("--restore-shell"))
            {
                // Undo commands are outside Sunshine's game process group. Delay
                // restoration so the stream can close even if the tray app died.
                Process.Start(new ProcessStartInfo(Environment.ProcessPath!) {
                    ArgumentList={"--restore-delayed"},UseShellExecute=false,CreateNoWindow=true });
                return 0;
            }
            return 1;
        }
    }
}
