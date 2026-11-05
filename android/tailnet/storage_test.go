package flxtrnet

import (
    "os"
    "path/filepath"
    "testing"
    "tailscale.com/logpolicy"
)

func TestMobileStorageWithoutHomeOrSystemTemp(t *testing.T){
    root:=t.TempDir()
    t.Setenv("HOME","");t.Setenv("XDG_CACHE_HOME","")
    t.Setenv("TS_LOGS_DIR","");t.Setenv("TMPDIR",filepath.Join(root,"missing-system-temp"))
    if err:=configurePrivateStorage(root);err!=nil{t.Fatal(err)}
    if got:=logpolicy.LogsDir(func(string,...any){});got!=filepath.Join(root,"logs"){t.Fatal("backend selected non-private logs",got)}
    file,err:=os.CreateTemp("","tailscale-test-");if err!=nil{t.Fatal(err)}
    name:=file.Name();file.Close();defer os.Remove(name)
    if filepath.Dir(name)!=filepath.Join(root,"tmp"){t.Fatal("temporary data escaped private storage")}
    for _,name:=range []string{"logs","tmp"}{info,err:=os.Stat(filepath.Join(root,name));if err!=nil||info.Mode().Perm()!=0700{t.Fatal("private directory permissions",name,err)}}
}
