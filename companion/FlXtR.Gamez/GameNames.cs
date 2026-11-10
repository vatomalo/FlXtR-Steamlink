using System.Text.RegularExpressions;
namespace FlXtR.Gamez;
internal static class GameNames
{
    private static readonly string[] Generic=["win64","win32","x64","x86","binaries","bin","release","shipping","windows","engine"];
    public static string Clean(string title)
    {
        title=Regex.Replace(title,@"(?i)[._\s-]+(?:v\d{3,}[\w.]*|build[._\s-]*\d+).*$","");
        title=Regex.Replace(title,@"(?<=[a-z])(?=[A-Z])|(?<=[A-Za-z])(?=[0-9])"," ");
        title=Regex.Replace(title,@"[._-]+"," ");
        title=Regex.Replace(title,@"(?i)\s+(?:v\d{3,}\w*|build\s*\d+).*$","");
        return Regex.Replace(title,@"\s+"," ").Trim();
    }
    public static string Guess(string executable)
    {
        var directory=new FileInfo(executable).Directory;
        while(directory?.Parent is not null&&Generic.Contains(directory.Name.ToLowerInvariant()))directory=directory.Parent;
        var title=Clean(directory?.Name??Path.GetFileNameWithoutExtension(executable));
        return title.Length==0?Path.GetFileNameWithoutExtension(executable):title;
    }
}
