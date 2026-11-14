// Package flxtrnet embeds a private Tailscale node for FlXtR traffic only.
package flxtrnet

import (
    "context"
    "crypto/rand"
    "encoding/hex"
    "encoding/json"
    "errors"
    "io"
    "net"
    "net/http"
    "net/netip"
    "net/url"
    "os"
    "path/filepath"
    "regexp"
    "runtime/debug"
    "strings"
    "sync"
    "time"

    "tailscale.com/ipn"
    "tailscale.com/net/netmon"
    "tailscale.com/tsnet"
)

// Platform supplies interfaces through Android's supported Java APIs.
type Platform interface {
    Interfaces() string
    DefaultInterface() string
}

type Client struct {
    server *tsnet.Server
    platform Platform
    identity string
    httpServer *http.Server
    mu sync.Mutex
    closed bool
    adbMu sync.Mutex
    adb *adbBridge
    rom romReceiver
}

var platformOnce sync.Once
var platformMu sync.Mutex
var currentPlatform Platform
var tailRange = netip.MustParsePrefix("100.64.0.0/10")
var routes = regexp.MustCompile(`^/(\.well-known/flxtr|[0-9a-f]{32}/(capabilities|handoff|roms|status/[0-9a-f]{32})?)$`)

func NewClient(directory, hostname string, platform Platform) (*Client, error) {
    if directory=="" || hostname=="" { return nil,errors.New("missing private state directory or hostname") }
    if err:=os.MkdirAll(directory,0700); err!=nil{return nil,err}
    if err:=os.Chmod(directory,0700); err!=nil{return nil,err}
    if err:=configurePrivateStorage(directory);err!=nil{return nil,err}
    identityPath:=filepath.Join(directory,"flxtr-device-id")
    data,_:=os.ReadFile(identityPath)
    identity:=strings.TrimSpace(string(data))
    if decoded,err:=hex.DecodeString(identity);err!=nil || len(decoded)!=16 {
        raw:=make([]byte,16);if _,err:=rand.Read(raw);err!=nil{return nil,err}
        identity=hex.EncodeToString(raw)
        if err:=os.WriteFile(identityPath,[]byte(identity+"\n"),0600);err!=nil{return nil,err}
    }
    debug.SetMemoryLimit(128*1024*1024)
    if platform!=nil {
        platformMu.Lock();currentPlatform=platform;platformMu.Unlock()
        platformOnce.Do(func(){netmon.RegisterInterfaceGetter(platformInterfaces)})
        updateDefaultInterface(platform.DefaultInterface())
    }
    quiet:=func(string,...any){}
    server:=&tsnet.Server{Dir:directory,Hostname:hostname,Logf:quiet,UserLogf:quiet}
    if err:=server.Start();err!=nil{return nil,err}
    c:=&Client{server:server,platform:platform,identity:identity}
    c.rom.name=hostname
    go c.announce(hostname)
    return c,nil
}

func (c *Client) announce(name string) {
    listener,err:=c.server.Listen("tcp",":43927");if err!=nil{return}
    srv:=newROMHTTPServer(c.romHTTP)
    c.mu.Lock();if c.closed{c.mu.Unlock();listener.Close();return};c.httpServer=srv;c.mu.Unlock()
    _=srv.Serve(listener)
}

func (c *Client) Status() (string,error) {
    lc,err:=c.server.LocalClient();if err!=nil{return "",err}
    ctx,cancel:=context.WithTimeout(context.Background(),5*time.Second);defer cancel()
    status,err:=lc.Status(ctx);if err!=nil{return "",err}
    peers:=make([]map[string]any,0,len(status.Peer))
    for _,p:=range status.Peer {
        for _,ip:=range p.TailscaleIPs {if ip.Is4()&&tailRange.Contains(ip){peers=append(peers,map[string]any{"id":string(p.ID),"name":p.HostName,"ip":ip.String(),"online":p.Online});break}}
    }
    ips:=[]string{}
    if status.Self!=nil{for _,ip:=range status.Self.TailscaleIPs{if ip.Is4(){ips=append(ips,ip.String())}}}
    out,err:=json.Marshal(map[string]any{"state":status.BackendState,"authUrl":status.AuthURL,"peers":peers,"id":c.identity,"ips":ips,"adb":c.adbStatus()})
    return string(out),err
}

func (c *Client) Connect() error {
    lc,err:=c.server.LocalClient();if err!=nil{return err}
    ctx,cancel:=context.WithTimeout(context.Background(),5*time.Second);defer cancel()
    _,err=lc.EditPrefs(ctx,&ipn.MaskedPrefs{Prefs:ipn.Prefs{WantRunning:true},WantRunningSet:true})
    return err
}

func (c *Client) Login() error {
    if err:=c.Connect();err!=nil{return err}
    lc,err:=c.server.LocalClient();if err!=nil{return err}
    ctx,cancel:=context.WithTimeout(context.Background(),5*time.Second);defer cancel()
    return lc.StartLoginInteractive(ctx)
}

func (c *Client) Disconnect() error {
    c.StopADB()
    lc,err:=c.server.LocalClient();if err!=nil{return err}
    ctx,cancel:=context.WithTimeout(context.Background(),5*time.Second);defer cancel()
    _,err=lc.EditPrefs(ctx,&ipn.MaskedPrefs{Prefs:ipn.Prefs{WantRunning:false},WantRunningSet:true})
    return err
}

func (c *Client) Logout() error {
    c.StopADB()
    lc,err:=c.server.LocalClient();if err!=nil{return err}
    ctx,cancel:=context.WithTimeout(context.Background(),5*time.Second);defer cancel()
    return lc.Logout(ctx)
}

func (c *Client) NetworkChanged() {
    if c.platform!=nil {updateDefaultInterface(c.platform.DefaultInterface())}
    c.server.Sys().NetMon.Get().InjectEvent()
}

func validRequest(method,address string)(*url.URL,error){
    u,err:=url.Parse(address);if err!=nil{return nil,err}
    ip,iperr:=netip.ParseAddr(u.Hostname())
    ai:=u.Port()=="43928"&&u.Path=="/som-ai/v1/step"&&method=="POST"
    peer:=u.Port()=="43927"&&routes.MatchString(u.Path)
    if iperr!=nil||!ip.Is4()||!tailRange.Contains(ip)||u.Scheme!="http"||u.User!=nil||u.RawQuery!=""||u.Fragment!=""||(!peer&&!ai)||(method!="GET"&&method!="POST") {
        return nil,errors.New("request is not a private FlXtR peer endpoint")
    }
    if method=="POST"&&!ai&&!(strings.HasSuffix(u.Path,"/handoff")||strings.HasSuffix(u.Path,"/roms")){return nil,errors.New("unsupported upload route")}
    return u,nil
}

func (c *Client) RequestBytes(address string,upload []byte)(string,error){
    u,err:=validRequest("POST",address);if err!=nil{return "",err}
    if len(upload)<0x20000||len(upload)>0x21000{return "",errors.New("invalid AI snapshot size")}
    ctx,cancel:=context.WithTimeout(context.Background(),15*time.Second);defer cancel()
    request,err:=http.NewRequestWithContext(ctx,"POST",u.String(),strings.NewReader(string(upload)));if err!=nil{return "",err}
    request.ContentLength=int64(len(upload));request.Header.Set("Content-Type","application/octet-stream")
    transport:=&http.Transport{DialContext:c.server.Dial,DisableKeepAlives:true};defer transport.CloseIdleConnections()
    response,err:=(&http.Client{Transport:transport,CheckRedirect:func(*http.Request,[]*http.Request)error{return errors.New("AI redirects are unsupported")}}).Do(request);if err!=nil{return "",err};defer response.Body.Close()
    raw,err:=io.ReadAll(io.LimitReader(response.Body,4097));if err!=nil{return "",err}
    if len(raw)>4096{return "",errors.New("AI response is too large")}
    if response.StatusCode<200||response.StatusCode>=300{return "",errors.New("AI service rejected the snapshot")}
    if !json.Valid(raw){return "",errors.New("AI response is not JSON")}
    return string(raw),nil
}

// Request streams uploads through the embedded node, never the device's global VPN.
func (c *Client) Request(method,address,upload string)(string,error){
    u,err:=validRequest(method,address);if err!=nil{return "",err}
    timeout:=120*time.Second;if strings.HasSuffix(u.Path,"/roms"){timeout=5*time.Minute};if method=="GET"{timeout=5*time.Second}
    ctx,cancel:=context.WithTimeout(context.Background(),timeout);defer cancel()
    var body io.Reader
    var size int64
    if upload!="" {
        if method!="POST"{return "",errors.New("unexpected upload")}
        file,err:=os.Open(upload);if err!=nil{return "",err};defer file.Close()
        info,err:=file.Stat();if err!=nil{return "",err}
        if !info.Mode().IsRegular()||info.Size()<=0||info.Size()>128*1024*1024+65536{return "",errors.New("invalid upload size")}
        body=file;size=info.Size()
    }
    request,err:=http.NewRequestWithContext(ctx,method,u.String(),body);if err!=nil{return "",err}
    if upload!="" {request.ContentLength=size;request.Header.Set("Content-Type","application/octet-stream")}
    transport:=&http.Transport{DialContext:c.server.Dial,DisableKeepAlives:true}
    defer transport.CloseIdleConnections()
    client:=&http.Client{Transport:transport,CheckRedirect:func(*http.Request,[]*http.Request)error{return errors.New("peer redirects are unsupported")}}
    response,err:=client.Do(request);if err!=nil{return "",err};defer response.Body.Close()
    raw,err:=io.ReadAll(io.LimitReader(response.Body,32769));if err!=nil{return "",err}
    if len(raw)>32768{return "",errors.New("peer response is too large")}
    if response.StatusCode<200||response.StatusCode>=300{
        var details struct{Error string `json:"error"`};_ = json.Unmarshal(raw,&details)
        if details.Error!=""{return "",errors.New(details.Error)}
        return "",errors.New("FlXtR peer rejected the request")
    }
    if !json.Valid(raw){return "",errors.New("peer response is not JSON")}
    return string(raw),nil
}

func (c *Client) Close() error {
    c.mu.Lock();c.closed=true;srv:=c.httpServer;c.mu.Unlock()
    c.StopADB()
    c.rom.mu.Lock();lan:=c.rom.server;c.rom.server=nil;c.rom.mu.Unlock();if lan!=nil{_ = lan.Close()}
    if srv!=nil{_ = srv.Close()}
    return c.server.Close()
}

type interfaceDescription struct {
    Name string `json:"name"`;Index int `json:"index"`;MTU int `json:"mtu"`;Flags int `json:"flags"`;Addresses []string `json:"addresses"`
}

func parseInterfaces(raw string)([]netmon.Interface,error){
    var descriptions []interfaceDescription
    if err:=json.Unmarshal([]byte(raw),&descriptions);err!=nil{return nil,err}
    interfaces:=make([]netmon.Interface,0,len(descriptions))
    for _,d:=range descriptions {
        if d.Name==""{continue}
        iface:=netmon.Interface{Interface:&net.Interface{Name:d.Name,Index:d.Index,MTU:d.MTU,Flags:net.Flags(d.Flags)},AltAddrs:[]net.Addr{}}
        for _,text:=range d.Addresses {p,err:=netip.ParsePrefix(text);if err==nil{iface.AltAddrs=append(iface.AltAddrs,&net.IPNet{IP:net.IP(p.Addr().AsSlice()),Mask:net.CIDRMask(p.Bits(),p.Addr().BitLen())})}}
        interfaces=append(interfaces,iface)
    }
    return interfaces,nil
}

func platformInterfaces()([]netmon.Interface,error){
    platformMu.Lock();p:=currentPlatform;platformMu.Unlock()
    if p==nil{return nil,errors.New("Android network interfaces unavailable")}
    return parseInterfaces(p.Interfaces())
}
