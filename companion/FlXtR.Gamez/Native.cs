using System.Runtime.InteropServices;

namespace FlXtR.Gamez;

internal static class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct Pad { public ushort Buttons; public byte LT, RT; public short LX, LY, RX, RY; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Controller { public uint Packet; public Pad Pad; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Input { public uint Type; public InputData Data; }
    [StructLayout(LayoutKind.Explicit)]
    private struct InputData
    {
        [FieldOffset(0)] public MouseInput Mouse;
        [FieldOffset(0)] public KeyboardInput Keyboard;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput { public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput { public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra; }
    [DllImport("xinput1_4.dll")] private static extern uint XInputGetState(uint index, out Controller state);
    [DllImport("user32.dll")] private static extern uint SendInput(uint count, Input[] inputs, int size);
    internal static bool State(out Pad pad)
    {
        for (uint index = 0; index < 4; index++)
            if (XInputGetState(index, out var state) == 0) { pad = state.Pad; return true; }
        pad = default;
        return false;
    }
    internal static void Key(ushort key, bool up = false, bool downOnly = false)
    {
        var inputs = new List<Input> { new() { Type = 1, Data = new InputData { Keyboard = new KeyboardInput { Key = key, Flags = up ? 2u : 0u } } } };
        if (!up && !downOnly) inputs.Add(new Input { Type = 1, Data = new InputData { Keyboard = new KeyboardInput { Key = key, Flags = 2 } } });
        SendInput((uint)inputs.Count, inputs.ToArray(), Marshal.SizeOf<Input>());
    }
    internal static void Mouse(bool right)
    {
        Input[] inputs = [new() { Data = new InputData { Mouse = new MouseInput { Flags = right ? 8u : 2u } } },
            new() { Data = new InputData { Mouse = new MouseInput { Flags = right ? 16u : 4u } } }];
        SendInput(2, inputs, Marshal.SizeOf<Input>());
    }
}

internal sealed class KeyboardWindow : Form
{
    private readonly string[][] rows = [
        ["Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "Del"],
        ["`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Bksp"],
        ["Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\"],
        ["Caps", "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "Enter"],
        ["Shift", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"],
        ["Ctrl", "Alt", "Win", "Space", "Left", "Down", "Up", "Right", "Home", "End", "PgUp", "PgDn"]];
    private readonly HashSet<ushort> modifiers = [];
    private int row, column;
    protected override bool ShowWithoutActivation => true;
    protected override CreateParams CreateParams { get { var parameters = base.CreateParams; parameters.ExStyle |= 0x08000000 | 0x80; return parameters; } }
    public KeyboardWindow() { TopMost = true; FormBorderStyle = FormBorderStyle.None; Size = new Size(1000, 340); BackColor = Color.FromArgb(28, 28, 32); DoubleBuffered = true; }
    public void MoveSelection(int horizontal, int vertical) { row = Math.Clamp(row + vertical, 0, rows.Length - 1); column = Math.Clamp(column + horizontal, 0, rows[row].Length - 1); Invalidate(); }
    public void Press()
    {
        var label = rows[row][column];
        ushort code = label switch { "Esc" => 27, "Del" => 46, "Bksp" => 8, "Tab" => 9, "Caps" => 20, "Enter" => 13, "Shift" => 16, "Ctrl" => 17, "Alt" => 18, "Win" => 91, "Space" => 32, "Left" => 37, "Up" => 38, "Right" => 39, "Down" => 40, "Home" => 36, "End" => 35, "PgUp" => 33, "PgDn" => 34, "`" => 192, "-" => 189, "=" => 187, "[" => 219, "]" => 221, "\\" => 220, ";" => 186, "'" => 222, "," => 188, "." => 190, "/" => 191, _ => label.StartsWith('F') && label.Length > 1 ? (ushort)(111 + int.Parse(label[1..])) : label[0] };
        if (code is 16 or 17 or 18 or 91)
        {
            if (modifiers.Remove(code)) Native.Key(code, up: true);
            else { modifiers.Add(code); Native.Key(code, downOnly: true); }
        }
        else { Native.Key(code); Release(); }
        Invalidate();
    }
    private void Release() { foreach (var modifier in modifiers) Native.Key(modifier, up: true); modifiers.Clear(); }
    protected override void OnVisibleChanged(EventArgs args) { if (!Visible) Release(); base.OnVisibleChanged(args); }
    protected override void Dispose(bool disposing) { if (disposing) Release(); base.Dispose(disposing); }
    protected override void OnPaint(PaintEventArgs args)
    {
        base.OnPaint(args);
        var width = ClientSize.Width / 14;
        var height = ClientSize.Height / 6;
        for (var rowIndex = 0; rowIndex < rows.Length; rowIndex++)
            for (var columnIndex = 0; columnIndex < rows[rowIndex].Length; columnIndex++)
            {
                var rectangle = new Rectangle(columnIndex * width + 2, rowIndex * height + 2, width - 4, height - 4);
                args.Graphics.FillRectangle(rowIndex == row && columnIndex == column ? Brushes.RoyalBlue : Brushes.DimGray, rectangle);
                TextRenderer.DrawText(args.Graphics, rows[rowIndex][columnIndex], Font, rectangle, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
    }
}
