namespace FlXtR.Gamez;
internal sealed class AddGameDialog : Form
{
    private readonly TextBox executable=new(){Left=16,Top=38,Width=490};
    private readonly TextBox title=new(){Left=16,Top=106,Width=590,MaxLength=79};
    private readonly CheckBox approve=new(){Left=16,Top=144,Width=590,Text="Approve this executable for FlXtR",Checked=true};
    private readonly Label message=new(){Left=16,Top=183,Width=590,Height=42};
    private readonly Button browse=new(){Left=516,Top=36,Width=90,Text="Browse…"};
    private readonly Button add=new(){Left=416,Top=238,Width=90,Text="Add game"};
    private readonly Button cancel=new(){Left=516,Top=238,Width=90,Text="Cancel",DialogResult=DialogResult.Cancel};
    private bool busy;
    public Game? AddedGame {get;private set;}
    public AddGameDialog(Catalog catalog,string? initial=null)
    {
        Text="Add a game to FlXtR";ClientSize=new Size(625,285);FormBorderStyle=FormBorderStyle.FixedDialog;
        StartPosition=FormStartPosition.CenterParent;MinimizeBox=MaximizeBox=false;ShowInTaskbar=false;
        Controls.AddRange([new Label{Left=16,Top=15,Width=590,Text="Game executable"},executable,browse,
            new Label{Left=16,Top=83,Width=590,Text="Display name"},title,approve,message,add,cancel]);
        AcceptButton=add;CancelButton=cancel;
        void Choose(string path){executable.Text=path;title.Text=GameNames.Guess(path);}
        if(initial is not null)Choose(initial);
        browse.Click+=(_,_)=>{
            using var picker=new OpenFileDialog{Title="Choose the game's main executable",Filter="Windows games (*.exe)|*.exe",CheckFileExists=true};
            if(picker.ShowDialog(this)==DialogResult.OK)Choose(picker.FileName);
        };
        FormClosing+=(_,eventArgs)=>{if(busy)eventArgs.Cancel=true;};
        add.Click+=async(_,_)=>{
            busy=true;add.Enabled=browse.Enabled=cancel.Enabled=executable.Enabled=title.Enabled=approve.Enabled=false;
            message.Text="Checking executable…";
            try {
                var path=executable.Text;var name=string.IsNullOrWhiteSpace(title.Text)?GameNames.Guess(path):title.Text;var approved=approve.Checked;
                AddedGame=await Task.Run(()=>catalog.AddGame(path,name,approved));
                busy=false;DialogResult=DialogResult.OK;
            }catch(Exception error){message.Text=error.Message;}
            finally {busy=false;if(!IsDisposed)add.Enabled=browse.Enabled=cancel.Enabled=executable.Enabled=title.Enabled=approve.Enabled=true;}
        };
    }
}
