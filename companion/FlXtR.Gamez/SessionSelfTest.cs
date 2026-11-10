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
        if(args[0]=="--job-test-window"){
            ApplicationConfiguration.Initialize();
            using var window=new Form{Text="FlXtR isolated game window test",Bounds=new Rectangle(80,80,800,450),WindowState=FormWindowState.Minimized};
            using var timer=new System.Windows.Forms.Timer{Interval=15000};timer.Tick+=(_,_)=>window.Close();timer.Start();
            window.Shown+=(_,_)=>File.WriteAllText(args[1],"ready");Application.Run(window);return true;
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
            Check(GameJob.FitAction(0x40000,0,800,450,1920,1080)==3,"Resizable game did not request maximization");
            Check(GameJob.FitAction(0x80000000,0,960,540,1920,1080)==1,"Quarter-screen borderless game did not request fitting");
            Check(GameJob.FitAction(0xC00000,0,800,450,1920,1080)==0,"Fixed-size dialog was stretched");
            Check(GameJob.FitAction(0x40000,0x80,800,450,1920,1080)==0,"Tool window was stretched");
            Check(GameJob.FitAction(0x40000,0,1920,1080,1920,1080)==0,"Already fullscreen window was resized");
            var readyFile=Path.Combine(directory,"window-ready.txt");
            using(var windowJob=new GameJob()){
                windowJob.Start(Environment.ProcessPath!,AppContext.BaseDirectory,["--job-test-window",readyFile]);
                var deadline=Stopwatch.StartNew();while(!File.Exists(readyFile)&&deadline.ElapsedMilliseconds<10000)Thread.Sleep(20);
                Check(File.Exists(readyFile),"Test game window did not appear");
                Check(windowJob.Activate(true),"Minimized game window was not found");
                Thread.Sleep(500);windowJob.Activate(true);Thread.Sleep(500);
                var windows=windowJob.WindowDiagnostics();var screen=Screen.PrimaryScreen!.Bounds;
                Check(windows.Any(window=>!window.Minimized&&window.Width>=screen.Width-32&&window.Height>=screen.Height-64),"Game retry did not restore and maximize the game window");
                windowJob.RequestClose();var close=Stopwatch.StartNew();while(windowJob.Running&&close.ElapsedMilliseconds<10000)Thread.Sleep(20);
                Check(!windowJob.Running,"Test game window did not close normally");
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
            var library=Path.Combine(directory,"Fresh.Library");var nested=Path.Combine(library,"Example.Game-v20240101-P2P","Binaries","Win64");Directory.CreateDirectory(nested);
            var importedExe=Path.Combine(nested,"Example-Win64-Shipping.exe");File.Copy(Environment.ProcessPath!,importedExe);
            var imports=new Catalog{StoragePath=Path.Combine(directory,"imports.json")};
            Check(GameNames.Guess(importedExe)=="Example Game","Generic executable folder was used as the display name");
            var imported=imports.AddGame(importedExe,"Example's Game",true);
            Check(imported.Approved&&imported.Imported&&imports.Supports(importedExe),"Explicit executable could not be approved outside default libraries");
            var importedId=imported.Id;imports.AddGame(importedExe,"Renamed example",true);
            Check(imports.Games.Count==1&&imports.Games[0].Id==importedId,"Readding an executable duplicated or changed its ID");
            var loadedImports=JsonSerializer.Deserialize<Catalog>(File.ReadAllText(imports.StoragePath))!;
            Check(loadedImports.Supports(importedExe)&&loadedImports.Games[0].Title=="Renamed example","Explicit import did not survive persistence");
            Check(!imports.LibraryRoots.Contains(nested),"Adding one executable unexpectedly enrolled the whole folder for scanning");
            imports.AddLibrary(library);Check(imports.LibraryRoots.Contains(library),"Custom library folder was not saved");
            File.Delete(importedExe);Directory.Delete(nested);Directory.Delete(Path.GetDirectoryName(nested)!);Directory.Delete(Path.GetDirectoryName(Path.GetDirectoryName(nested)!)!);Directory.Delete(library);
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
