package flxtrnet

import (
    "os"
    "path/filepath"
)

// Android's app process has no HOME or writable system temp directory. tsnet's
// backend uses logpolicy independently of Server.Dir, even with quiet Logf.
func configurePrivateStorage(directory string) error {
    for variable,subdir:=range map[string]string{"TS_LOGS_DIR":"logs","TMPDIR":"tmp"}{
        path:=filepath.Join(directory,subdir)
        if err:=os.MkdirAll(path,0700);err!=nil{return err}
        if err:=os.Chmod(path,0700);err!=nil{return err}
        if err:=os.Setenv(variable,path);err!=nil{return err}
    }
    return nil
}
