using FlXtR.Gamez;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;

var count = 0;
void Check(bool condition, string name) { if (!condition) throw new Exception(name); Console.WriteLine("PASS " + name); count++; }
Check(!Catalog.Candidate(@"D:\Gamez\Title\setup.exe"), "installer rejected");
Check(!Catalog.Candidate(@"D:\Gamez\Title\UnityCrashHandler64.exe"), "crash handler rejected");
Check(!Catalog.Candidate(@"D:\Gamez\Title\_CommonRedist\game.exe"), "support directory rejected");
Check(Catalog.Candidate(@"D:\Gamez\RPG\Title\Binaries\Win64\Title-Win64-Shipping.exe"), "nested game candidate");
Check(!Catalog.Supported(Environment.ProcessPath!), "executable outside roots rejected");
Check(Catalog.Fingerprint(Environment.ProcessPath!).Length == 64, "real PE fingerprint");
using (var stream = File.OpenRead(Environment.ProcessPath!))
{
    Catalog.ValidateExecutable(stream);
    Check(stream.Position < stream.Length, "discovery stops after PE header without hashing full executable");
    Check(stream.CanRead, "header validation leaves stream open");
}
var temporary = Path.GetTempFileName();
try
{
    File.WriteAllText(temporary, "MZinvalid");
    var rejected = false;
    try { Catalog.Fingerprint(temporary); } catch (InvalidDataException) { rejected = true; }
    Check(rejected, "fake executable rejected");
}
finally { File.Delete(temporary); }
var probe = new TcpListener(IPAddress.Loopback, 0);
probe.Start();
var port = ((IPEndPoint)probe.LocalEndpoint).Port;
probe.Stop();
var catalog = new Catalog { Port = port };
catalog.Games.Add(new Game { Id = "1", Exe = Environment.ProcessPath!, Approved = false });
using var bridge = new Bridge(catalog);
bridge.Start();
using var client = new HttpClient { BaseAddress = new Uri($"http://127.0.0.1:{port}") };
Check((await client.PostAsync("/v1/launch", new StringContent("1"))).StatusCode == HttpStatusCode.Unauthorized, "authentication required");
client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", catalog.Token);
Check((await client.PostAsync("/v1/launch", new StringContent("1"))).StatusCode == HttpStatusCode.UnprocessableEntity, "unreviewed game rejected");
Check((await client.PostAsync("/v1/launch", new StringContent("999"))).StatusCode == HttpStatusCode.UnprocessableEntity, "unknown ID rejected");
Check((await client.PostAsync("/v1/launch", new StringContent("../cmd.exe"))).StatusCode == HttpStatusCode.BadRequest, "path injection rejected");
Check((await client.PostAsync("/v1/launch", new StringContent(new string('a', 33)))).StatusCode == HttpStatusCode.BadRequest, "oversize body rejected");
Check(!(await client.GetStringAsync("/v1/games")).Contains("exe"), "catalog hides local paths");
catalog.Games[0].Approved = true;
Check((await client.PostAsync("/v1/launch", new StringContent("1"))).StatusCode == HttpStatusCode.UnprocessableEntity, "approved outside-root executable rejected");
catalog.AllowedClients = ["192.0.2.1"];
Check((await client.GetAsync("/v1/games")).StatusCode == HttpStatusCode.Unauthorized, "client IP restriction enforced");
Console.WriteLine($"{count} checks passed");
