using System.Diagnostics;
using System.Text.Json;

namespace FlXtR.Gamez;

internal static class SessionSelfTest
{
    public static bool Handle(string[] args)
    {
        if(args.Length==0)return false;
        if(args[0]=="--watchdog-test-worker"){
            var workerDirectory=args[1];var counter=Path.Combine(workerDirectory,"attempts.txt");
            var attempt=File.Exists(counter)?int.Parse(File.ReadAllText(counter))+1:1;
            File.WriteAllText(counter,attempt.ToString());File.WriteAllText(Path.Combine(workerDirectory,$"pid-{attempt}.txt"),Environment.ProcessId.ToString());
            if(attempt==1)Environment.ExitCode=19;else Thread.Sleep(60000);
            return true;
        }
        if(args[0]=="--job-test-sleep") { Thread.Sleep(int.Parse(args[1]));return true; }
        if(args[0]=="--job-test-parent")
        {
            Process.Start(new ProcessStartInfo(Environment.ProcessPath!) {
                ArgumentList={"--job-test-sleep","1500"},UseShellExecute=false,CreateNoWindow=true })!.Dispose();
            File.WriteAllText(args[1],"child started");Thread.Sleep(100);return true;
        }
        if(args[0]=="--job-test-args") { File.WriteAllText(args[1],JsonSerializer.Serialize(args.Skip(2)));return true; }
        if(args[0]!="--session-self-test")return false;
        var result=args[1];var count=0;
        void Check(bool condition,string text) { if(!condition)throw new IOException(text);count++; }
        var directory=Path.Combine(Path.GetTempPath(),"FlXtR-session-test-"+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var renamed=new Game {Id="ABC123",Title="Win64",Approved=true,Exe=@"D:\Gamez\Sifu\game.exe",Hash="original",Args=["--example"]};
            renamed.Rename("  Sifu  ");
            Check(renamed.Title=="Sifu","Rename failed to trim title");
            Check(renamed.Id=="ABC123"&&renamed.Approved&&renamed.Hash=="original"&&renamed.Exe.EndsWith("game.exe")&&renamed.Args.SequenceEqual(new[]{"--example"}),"Rename changed launch configuration or approval");
            foreach(var invalid in new[]{" ","bad\nname",new string('x',80)})
            {
                var rejected=false;try { renamed.Rename(invalid); }catch(InvalidDataException) { rejected=true; }
                Check(rejected&&renamed.Title=="Sifu","Invalid rename changed the title");
            }
            var marker=Path.Combine(directory,"parent.txt");
            var gameFile=Path.Combine(directory,"installed-game.exe");File.WriteAllText(gameFile,"installed game data");
            var store=new Catalog {StoragePath=Path.Combine(directory,"catalog.json")};
            store.Games.Add(new Game{Id="Revoke1",Title="Installed game",Exe=gameFile,Hash="preserved",Args=["--original"],Approved=true});
            store.Save();store.Unapprove("Revoke1");
            var saved=JsonSerializer.Deserialize<Catalog>(File.ReadAllText(store.StoragePath))!;
            Check(!saved.Games[0].Approved,"Unapproval was not persisted");
            Check(saved.Games[0].Id=="Revoke1"&&saved.Games[0].Hash=="preserved"&&saved.Games[0].Args.SequenceEqual(new[]{"--original"}),"Unapproval changed launch configuration");
            Check(File.ReadAllText(gameFile)=="installed game data","Unapproval changed or removed game files");
            store.Unapprove("Revoke1");Check(!store.Games[0].Approved,"Repeated unapproval reapproved a game");
            var unknown=false;try{store.Unapprove("Unknown");}catch(InvalidDataException){unknown=true;}
            Check(unknown,"Unknown unapproval changed the catalog");
            using(var job=new GameJob(@"Local\FlXtR.Gamez.Test-"+Guid.NewGuid().ToString("N")))
            {
                job.Start(Environment.ProcessPath!,AppContext.BaseDirectory,["--job-test-parent",marker]);
                var timer=Stopwatch.StartNew();
                while(!File.Exists(marker)&&timer.ElapsedMilliseconds<10000)Thread.Sleep(20);
                Check(File.Exists(marker),"Test bootstrap did not start");
                Thread.Sleep(250);
                Check(job.Running,"Bootstrap exit lost the still-running game child");
                using(var recovered=GameJob.Open(job.Name!))Check(recovered.Running,"Named game job could not be recovered");
                while(job.Running&&timer.ElapsedMilliseconds<15000)Thread.Sleep(20);
                Check(!job.Running,"Game job failed to finish after its child exited");
            }
            var heldName=@"Local\FlXtR.Gamez.Test-"+Guid.NewGuid().ToString("N");
            using(var job=new GameJob(heldName))
            {
                job.Start(Environment.ProcessPath!,AppContext.BaseDirectory,["--job-test-sleep","3000"]);
                using var held=GameJob.Open(heldName);
                job.Dispose();
                Check(held.Running,"Closing the launcher handle terminated the game");
                using var recovered=GameJob.Open(heldName);
                Check(recovered.Running,"Recovery lost the running game after launcher handle closed");
                var timer=Stopwatch.StartNew();while(recovered.Running&&timer.ElapsedMilliseconds<10000)Thread.Sleep(20);
                Check(!recovered.Running,"Recovered job failed to detect game exit");
            }
            var argumentFile=Path.Combine(directory,"arguments.json");
            string[] expected=["", "a b", "quote\"inside", "C:\\folder with space\\", "literal;$(command)"];
            using(var job=new GameJob())
            {
                job.Start(Environment.ProcessPath!,AppContext.BaseDirectory,["--job-test-args",argumentFile,..expected]);
                var timer=Stopwatch.StartNew();
                while(job.Running&&timer.ElapsedMilliseconds<10000)Thread.Sleep(20);
                Check(!job.Running,"Argument test process did not finish");
                Check(JsonSerializer.Deserialize<string[]>(File.ReadAllText(argumentFile))!.SequenceEqual(expected),"Windows argument quoting changed arguments");
            }
            File.WriteAllText(result,JsonSerializer.Serialize(new {ok=true,checks=count}));
        }
        catch(Exception error) { File.WriteAllText(result,JsonSerializer.Serialize(new {ok=false,error=error.Message,checks=count}));Environment.ExitCode=1; }
        finally
        {
            foreach(var file in Directory.EnumerateFiles(directory))File.Delete(file);
            Directory.Delete(directory);
        }
        return true;
    }
}
