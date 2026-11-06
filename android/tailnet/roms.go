package flxtrnet

import (
    "archive/zip"
    "crypto/rand"
    "crypto/sha256"
    "encoding/hex"
    "encoding/json"
    "errors"
    "fmt"
    "io"
    "net"
    "net/http"
    "os"
    "path/filepath"
    "strconv"
    "strings"
    "sync"
    "time"
    "unicode/utf8"
)

const maxROM = 64*1024*1024
var romSystems=map[string][]string{"NES":{ ".nes",".unf",".unif" },"SNES":{ ".sfc",".smc" },"GB":{ ".gb" },"GBC":{ ".gbc" },"GBA":{ ".gba" }}
var romSystemNames=[]string{"NES","SNES","GB","GBC","GBA"}
type romMetadata struct{Protocol int `json:"protocol"`;System string `json:"system"`;Name string `json:"name"`;Size int64 `json:"size"`;SHA256 string `json:"sha256"`}
type romReceipt struct{Status string `json:"status"`;System string `json:"system"`;Name string `json:"name"`;Path string `json:"path"`;Size int64 `json:"size"`;SHA256 string `json:"sha256"`;AlreadyPresent bool `json:"alreadyPresent"`}

func validateROM(meta romMetadata) error{
    extensions,ok:=romSystems[meta.System];if !ok{return errors.New("ROM sending supports NES, SNES, GB, GBC and GBA")}
    if !utf8.ValidString(meta.Name)||len(meta.Name)==0||len(meta.Name)>180||strings.HasPrefix(meta.Name,".")||strings.HasSuffix(meta.Name,".")||strings.HasSuffix(meta.Name," "){return errors.New("invalid ROM filename")}
    for _,r:=range meta.Name{if r<32||strings.ContainsRune(`/\:<>"|?*`,r){return errors.New("invalid ROM filename")}}
    match:=false;for _,ext:=range extensions{if strings.ToLower(filepath.Ext(meta.Name))==ext{match=true}}
    if !match{return errors.New("ROM extension does not match its system folder")}
    if meta.Protocol!=1||meta.Size<=0||meta.Size>maxROM{return errors.New("invalid ROM metadata or size (maximum 64 MiB)")}
    raw,err:=hex.DecodeString(meta.SHA256);if err!=nil||len(raw)!=32||strings.ToLower(meta.SHA256)!=meta.SHA256{return errors.New("invalid ROM checksum")}
    return nil
}

func fileSHA(path string)(string,error){file,err:=os.Open(path);if err!=nil{return "",err};defer file.Close();hash:=sha256.New();_,err=io.Copy(hash,file);return hex.EncodeToString(hash.Sum(nil)),err}

func receiveROM(archive,root string)(receipt romReceipt,err error){
    bundle,err:=zip.OpenReader(archive);if err!=nil{return receipt,err};defer bundle.Close()
    if len(bundle.File)!=2{return receipt,errors.New("invalid ROM archive")}
    var info,data *zip.File
    for _,entry:=range bundle.File{switch entry.Name{case "rom.json":if info!=nil{return receipt,errors.New("duplicate ROM metadata")};info=entry;case "rom.bin":if data!=nil{return receipt,errors.New("duplicate ROM data")};data=entry;default:return receipt,errors.New("invalid ROM archive member")}}
    if info==nil||data==nil||info.UncompressedSize64>8192{return receipt,errors.New("invalid ROM archive")}
    input,err:=info.Open();if err!=nil{return receipt,err};raw,err:=io.ReadAll(io.LimitReader(input,8193));input.Close();if err!=nil{return receipt,err}
    var meta romMetadata;if err=json.Unmarshal(raw,&meta);err!=nil{return receipt,err};if err=validateROM(meta);err!=nil{return receipt,err}
    if data.UncompressedSize64!=uint64(meta.Size){return receipt,errors.New("ROM size mismatch")}
    folder:=filepath.Join(root,meta.System)
    if stat,e:=os.Lstat(folder);e==nil&&stat.Mode()&os.ModeSymlink!=0{return receipt,errors.New("ROM folder is a symbolic link")}
    if err=os.MkdirAll(folder,0700);err!=nil{return receipt,err}
    target:=filepath.Join(folder,meta.Name)
    if stat,e:=os.Lstat(target);e==nil&&stat.Mode()&os.ModeSymlink!=0{return receipt,errors.New("existing ROM is a symbolic link")}
    receipt=romReceipt{Status:"stored",System:meta.System,Name:meta.Name,Path:target,Size:meta.Size,SHA256:meta.SHA256}
    if stat,e:=os.Stat(target);e==nil{
        if stat.Size()!=meta.Size{return receipt,errors.New("a different ROM with this filename already exists and was kept")}
        checksum,e:=fileSHA(target);if e!=nil||checksum!=meta.SHA256{return receipt,errors.New("a different ROM with this filename already exists and was kept")}
        receipt.AlreadyPresent=true;return receipt,nil
    }
    output,err:=os.CreateTemp(folder,".flxtr-rom-");if err!=nil{return receipt,err};temporary:=output.Name();defer os.Remove(temporary)
    input,err=data.Open();if err!=nil{output.Close();return receipt,err}
    hash:=sha256.New();copied,err:=io.Copy(io.MultiWriter(output,hash),io.LimitReader(input,meta.Size+1));input.Close()
    if err==nil&&(copied!=meta.Size||hex.EncodeToString(hash.Sum(nil))!=meta.SHA256){err=errors.New("ROM checksum mismatch")}
    if err==nil{err=output.Sync()};closeErr:=output.Close();if err==nil{err=closeErr};if err!=nil{return receipt,err}
    // O_EXCL also works on removable FAT storage. Never truncate a user's ROM.
    output,err=os.OpenFile(target,os.O_WRONLY|os.O_CREATE|os.O_EXCL,0600);if err!=nil{return receipt,errors.New("a ROM with this filename already exists and was kept")}
    ours,_:=output.Stat();committed:=false
    defer func(){output.Close();if !committed{if stat,e:=os.Stat(target);e==nil&&ours!=nil&&os.SameFile(ours,stat){_ = os.Remove(target)}}}()
    inputFile,err:=os.Open(temporary);if err!=nil{return receipt,err};_,err=io.Copy(output,inputFile);inputFile.Close()
    if err==nil{err=output.Sync()};if err!=nil{return receipt,err};if err=output.Close();err!=nil{return receipt,err};committed=true
    return receipt,nil
}

type romReceiver struct{mu sync.Mutex;transfer sync.Mutex;root,token,name string;port int;server *http.Server}

func (c *Client) SetROMDirectory(directory string) error{
    if !filepath.IsAbs(directory)||filepath.Clean(directory)==string(filepath.Separator){return errors.New("ROM storage requires an app-private directory")}
    if err:=os.MkdirAll(directory,0700);err!=nil{return err}
    c.rom.mu.Lock();defer c.rom.mu.Unlock()
    c.rom.root=directory
    if c.rom.token==""{
        path:=filepath.Join(c.server.Dir,"rom-token");raw,_:=os.ReadFile(path);token:=strings.TrimSpace(string(raw));decoded,e:=hex.DecodeString(token)
        if e!=nil||len(decoded)!=16{raw=make([]byte,16);if _,e=rand.Read(raw);e!=nil{return e};token=hex.EncodeToString(raw);if e=os.WriteFile(path,[]byte(token+"\n"),0600);e!=nil{return e}}
        c.rom.token=token
    }
    if c.rom.server==nil{
        listener,err:=net.Listen("tcp4","0.0.0.0:0");if err!=nil{return err}
        c.rom.port=listener.Addr().(*net.TCPAddr).Port
        c.rom.server=newROMHTTPServer(c.romHTTP)
        go c.rom.server.Serve(listener)
    }
    return nil
}

func newROMHTTPServer(handler http.HandlerFunc)*http.Server{return &http.Server{Handler:handler,ReadHeaderTimeout:5*time.Second,ReadTimeout:10*time.Minute,WriteTimeout:10*time.Minute,IdleTimeout:10*time.Second,MaxHeaderBytes:8192}}
func localIPv4(host string)bool{ip:=net.ParseIP(host).To4();if ip==nil{return false};return ip[0]==10||ip[0]==192&&ip[1]==168||ip[0]==172&&ip[1]>=16&&ip[1]<=31||ip[0]==169&&ip[1]==254}

// Announcement is shared by UDP LAN discovery and the private tailnet service.
func (c *Client) Announcement(host string)(string,error){
    ip:=net.ParseIP(host).To4();if ip==nil{return "",errors.New("discovery requires a private IPv4 address")}
    tail:=ip[0]==100&&ip[1]>=64&&ip[1]<=127;if !tail&&!localIPv4(host){return "",errors.New("discovery requires a private IPv4 address")}
    c.rom.mu.Lock();defer c.rom.mu.Unlock()
    roles:=[]string{"sender"};reply:=map[string]any{"protocol":1,"app":"FlXtR","id":c.identity,"name":c.rom.name,"roles":roles,"platform":"android"}
    if c.rom.root!=""&&c.rom.token!=""{port:=c.rom.port;if tail{port=43927};reply["roles"]=append(roles,"rom-receiver");reply["endpoint"]=fmt.Sprintf("http://%s:%d/%s/",host,port,c.rom.token)}
    raw,err:=json.Marshal(reply);return string(raw),err
}

func (c *Client) romHTTP(w http.ResponseWriter,r *http.Request){
    w.Header().Set("Content-Type","application/json");w.Header().Set("Cache-Control","no-store");w.Header().Set("Referrer-Policy","no-referrer")
    fail:=func(code int,err string){w.WriteHeader(code);_ = json.NewEncoder(w).Encode(map[string]string{"error":err})}
    host,port,err:=net.SplitHostPort(r.Host);if err!=nil{fail(403,"invalid device address");return}
    peer,_,err:=net.SplitHostPort(r.RemoteAddr);if err!=nil{fail(403,"invalid peer");return}
    ip:=net.ParseIP(peer).To4();if ip==nil||!(localIPv4(peer)||ip[0]==100&&ip[1]>=64&&ip[1]<=127){fail(403,"private peers only");return}
    c.rom.mu.Lock();root,token,lanPort:=c.rom.root,c.rom.token,c.rom.port;c.rom.mu.Unlock()
    address:=net.ParseIP(host).To4();tail:=address!=nil&&address[0]==100&&address[1]>=64&&address[1]<=127
    if !(tail&&port=="43927"||localIPv4(host)&&port==strconv.Itoa(lanPort))||(r.Header.Get("Origin")!=""&&r.Header.Get("Origin")!="http://"+r.Host){fail(403,"invalid device origin");return}
    if r.Method=="GET"&&r.URL.Path=="/.well-known/flxtr"{raw,e:=c.Announcement(host);if e!=nil{fail(400,e.Error());return};_,_=io.WriteString(w,raw);return}
    if token==""||root==""||!strings.HasPrefix(r.URL.Path,"/"+token+"/")||r.URL.RawQuery!=""{fail(403,"invalid device endpoint");return}
    route:=strings.TrimPrefix(r.URL.Path,"/"+token+"/")
    if r.Method=="GET"&&route=="capabilities"{_ = json.NewEncoder(w).Encode(map[string]any{"protocol":1,"armed":true,"sources":[]string{},"romTransfer":1,"romSystems":romSystemNames,"maxRomBytes":maxROM});return}
    if r.Method!="POST"||route!="roms"{fail(404,"not found");return}
    if !c.rom.transfer.TryLock(){fail(409,"another transfer is active");return};defer c.rom.transfer.Unlock()
    if r.ContentLength<=0||r.ContentLength>maxROM+65536{fail(400,"ROM transfer exceeds limit");return}
    file,err:=os.CreateTemp("","flxtr-rom-upload-");if err!=nil{fail(400,err.Error());return};path:=file.Name();defer os.Remove(path)
    copied,err:=io.Copy(file,http.MaxBytesReader(w,r.Body,maxROM+65536));file.Close()
    if err!=nil||copied!=r.ContentLength{fail(400,"ROM transfer interrupted");return}
    receipt,err:=receiveROM(path,root);if err!=nil{fail(400,err.Error());return};_ = json.NewEncoder(w).Encode(receipt)
}
