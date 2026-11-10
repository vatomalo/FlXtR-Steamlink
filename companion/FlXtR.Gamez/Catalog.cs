using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace FlXtR.Gamez;

public sealed record Game
{
    public string Id { get; set; } = "";
    public string Title { get; set; } = "";
    public string Exe { get; set; } = "";
    public string Hash { get; set; } = "";
    public string Cwd { get; set; } = "";
    public bool Approved { get; set; }
    public List<string> Args { get; set; } = [];
    public long ReviewedLength { get; set; }
    public long ReviewedWriteTime { get; set; }
    public void Rename(string title)
    {
        title=title.Trim();
        if(title.Length==0||Encoding.UTF8.GetByteCount(title)>79||title.Any(char.IsControl))
            throw new InvalidDataException("Enter a game title without line breaks; use a shorter title if it is too long.");
        Title=title;
    }
}

public sealed class Catalog
{
    public static readonly string[] Roots = [@"A:\Gamez", @"B:\Gamez", @"D:\Gamez", @"E:\Gamez"];
    public static readonly string DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FlXtR", "Gamez");
    public static readonly string FilePath = Path.Combine(DirectoryPath, "catalog.json");
    public static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    public List<Game> Games { get; set; } = [];
    public string Token { get; set; } = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));
    public string Bind { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 47777;
    public string[] AllowedClients { get; set; } = ["127.0.0.1"];
    [System.Text.Json.Serialization.JsonIgnore]
    internal string StoragePath {get;set;}=FilePath;

    public static Catalog Load()
    {
        var catalog = File.Exists(FilePath)
            ? JsonSerializer.Deserialize<Catalog>(File.ReadAllText(FilePath)) ?? throw new InvalidDataException("Invalid catalog") : new();
        if (catalog.Games.Any(game => game.Id.Length is < 1 or > 32 || !game.Id.All(char.IsAsciiLetterOrDigit)) ||
            catalog.Games.Select(game => game.Id).Distinct().Count() != catalog.Games.Count)
            throw new InvalidDataException("Game IDs must be unique ASCII letters/digits, 1–32 characters");
        return catalog;
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(StoragePath)!);
        File.WriteAllText(StoragePath + ".tmp", JsonSerializer.Serialize(this, Json));
        File.Move(StoragePath + ".tmp", StoragePath, true);
    }

    public void Rename(string id,string title)
    {
        lock(this)
        {
            var game=Games.SingleOrDefault(game=>game.Id==id)??throw new InvalidDataException("Game is no longer in the catalog");
            game.Rename(title);
            Save();
        }
    }

    public void Unapprove(string id)
    {
        lock(this)
        {
            var game=Games.SingleOrDefault(game=>game.Id==id)??throw new InvalidDataException("Unknown game");
            var previous=game.Approved;
            game.Approved=false;
            try{Save();}catch{game.Approved=previous;throw;}
        }
    }

    public static bool Supported(string path)
    {
        var full = Path.GetFullPath(path);
        if (!Roots.Any(root => full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) return false;
        for (var item = new FileInfo(full) as FileSystemInfo; item != null; item = item is FileInfo file ? file.Directory : ((DirectoryInfo)item).Parent)
            if ((item.Attributes & FileAttributes.ReparsePoint) != 0) return false;
        return Path.GetExtension(full).Equals(".exe", StringComparison.OrdinalIgnoreCase) && File.Exists(full);
    }

    public static bool Candidate(string path)
    {
        var name = Path.GetFileNameWithoutExtension(path).ToLowerInvariant();
        string[] blocked = ["setup", "install", "unins", "redist", "crash", "report", "update", "patch", "helper", "service", "config", "benchmark", "dxweb", "vcredist", "unitycrash", "cheat"];
        if (blocked.Any(name.Contains)) return false;
        return !path.Split(Path.DirectorySeparatorChar).Any(part => new[] { "_commonredist", "redist", "support", "installer", "directx" }.Contains(part.ToLowerInvariant()));
    }

    public static string Fingerprint(string path)
    {
        using var stream = File.OpenRead(path);
        ValidateExecutable(stream);
        stream.Position = 0;
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    public static void ValidateExecutable(Stream stream)
    {
        using var reader = new BinaryReader(stream, Encoding.UTF8, true);
        if (stream.Length < 64 || reader.ReadUInt16() != 0x5a4d) throw new InvalidDataException("Missing executable header");
        stream.Position = 60;
        var offset = reader.ReadInt32();
        if (offset < 64 || offset > stream.Length - 24) throw new InvalidDataException("Invalid executable header");
        stream.Position = offset;
        if (reader.ReadUInt32() != 0x4550) throw new InvalidDataException("Missing PE header");
        stream.Position = offset + 22;
        var flags = reader.ReadUInt16();
        if ((flags & 2) == 0 || (flags & 0x2000) != 0) throw new InvalidDataException("Not an executable application");
    }

    public void Scan(IProgress<string>? progress = null, CancellationToken cancellation = default)
    {
        var checkedFiles = 0;
        long lastProgress = 0;
        try
        {
            foreach (var root in Roots)
            {
                cancellation.ThrowIfCancellationRequested();
                progress?.Report($"Checking {root} …");
                if (!Directory.Exists(root)) continue;
                var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true, AttributesToSkip = FileAttributes.ReparsePoint, MaxRecursionDepth = 12 };
                foreach (var path in Directory.EnumerateFiles(root, "*.exe", options))
                {
                    cancellation.ThrowIfCancellationRequested();
                    checkedFiles++;
                    if (Environment.TickCount64 - lastProgress >= 150)
                    {
                        progress?.Report($"Scanning {root} — {checkedFiles} executables checked\n{path}");
                        lastProgress = Environment.TickCount64;
                    }
                    try
                    {
                        if (!Candidate(path) || !Supported(path)) continue;
                        using (var stream = File.OpenRead(path)) ValidateExecutable(stream);
                        var info = new FileInfo(path);
                        lock (this)
                        {
                            var existing = Games.Find(game => game.Exe.Equals(path, StringComparison.OrdinalIgnoreCase));
                            if (existing != null)
                            {
                                if (existing.Approved && (existing.ReviewedLength != info.Length || existing.ReviewedWriteTime != info.LastWriteTimeUtc.Ticks)) existing.Approved = false;
                                continue;
                            }
                            var id = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(path.ToUpperInvariant())))[..24];
                            Games.Add(new Game { Id = id, Title = info.Directory!.Name, Exe = path });
                        }
                    }
                    catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException) { }
                }
            }
        }
        finally { lock (this) Save(); }
    }
}
