package flxtrnet

import (
    "context"
    "encoding/json"
    "errors"
    "io"
    "net"
    "net/netip"
    "strconv"
    "sync"
    "time"
)

const adbConnectPort = 43930
const adbPairPort = 43931
const adbLifetime = 15*time.Minute

type adbBridge struct {
    peer netip.Addr
    expires time.Time
    pairing bool
    listeners []net.Listener
    cancel context.CancelFunc
    once sync.Once
    mu sync.Mutex
    connections map[net.Conn]bool
}

func adbPeer(address net.Addr, allowed netip.Addr) bool {
    host,_,err:=net.SplitHostPort(address.String())
    if err!=nil{return false}
    ip,err:=netip.ParseAddr(host)
    return err==nil&&ip.Unmap()==allowed
}

func (b *adbBridge) close(){
    b.once.Do(func(){
        b.cancel()
        for _,listener:=range b.listeners{_ = listener.Close()}
        b.mu.Lock();defer b.mu.Unlock()
        for conn:=range b.connections{_ = conn.Close()}
    })
}

func newADBBridge(listen func(string,string)(net.Listener,error),peer netip.Addr,connect,pair int,lifetime time.Duration)(*adbBridge,error){
    ctx,cancel:=context.WithCancel(context.Background())
    b:=&adbBridge{peer:peer,expires:time.Now().Add(lifetime),pairing:pair!=0,cancel:cancel,connections:make(map[net.Conn]bool)}
    ports:=[][2]int{{adbConnectPort,connect}}
    if pair!=0{ports=append(ports,[2]int{adbPairPort,pair})}
    for _,ports:=range ports{
        listener,err:=listen("tcp",":"+strconv.Itoa(ports[0]));if err!=nil{b.close();return nil,err}
        b.listeners=append(b.listeners,listener)
    }
    for i,listener:=range b.listeners{
        target:="127.0.0.1:"+strconv.Itoa(ports[i][1])
        go b.serve(ctx,listener,target)
    }
    go func(){timer:=time.NewTimer(lifetime);defer timer.Stop();select{case <-timer.C:b.close();case <-ctx.Done():}}()
    return b,nil
}

func (b *adbBridge) serve(ctx context.Context,listener net.Listener,target string){
    for {
        conn,err:=listener.Accept();if err!=nil{return}
        b.mu.Lock()
        if !adbPeer(conn.RemoteAddr(),b.peer)||ctx.Err()!=nil||len(b.connections)>=8{b.mu.Unlock();conn.Close();continue}
        b.connections[conn]=true;b.mu.Unlock()
        go func(){
            defer func(){conn.Close();b.mu.Lock();delete(b.connections,conn);b.mu.Unlock()}()
            local,err:=(&net.Dialer{Timeout:5*time.Second}).DialContext(ctx,"tcp",target);if err!=nil{return}
            b.mu.Lock()
            if ctx.Err()!=nil{b.mu.Unlock();local.Close();return}
            b.connections[local]=true;b.mu.Unlock()
            defer func(){local.Close();b.mu.Lock();delete(b.connections,local);b.mu.Unlock()}()
            // Raw TLS bytes stay end-to-end between adb and Android's adbd.
            _ = conn.SetDeadline(b.expires);_ = local.SetDeadline(b.expires)
            done:=make(chan struct{},1)
            go func(){_,_=io.Copy(local,conn);if tcp,ok:=local.(*net.TCPConn);ok{_ = tcp.CloseWrite()};done<-struct{}{}}()
            _,_=io.Copy(conn,local);_ = conn.Close();_ = local.Close();<-done
        }()
    }
}

func validADBPorts(connect,pair int) bool{return connect>=1024&&connect<=65535&&(pair==0||pair>=1024&&pair<=65535&&pair!=connect)}

// StartADB exposes only Android's two local debugging ports to one tailnet peer.
// It never persists or automatically restarts debugging access.
func (c *Client) StartADB(connectPort,pairPort int,allowedPeer string) error {
    if !validADBPorts(connectPort,pairPort){return errors.New("enter the separate connection and pairing ports from Android Wireless debugging")}
    peer,err:=netip.ParseAddr(allowedPeer)
    if err!=nil||!peer.Is4()||!tailRange.Contains(peer){return errors.New("ADB requires a single allowed Tailscale IPv4 peer")}
    c.adbMu.Lock();defer c.adbMu.Unlock()
    c.mu.Lock();closed:=c.closed;c.mu.Unlock();if closed{return errors.New("Tailscale is closed")}
    if c.adb!=nil{c.adb.close();c.adb=nil}
    c.adb,err=newADBBridge(c.server.Listen,peer,connectPort,pairPort,adbLifetime)
    return err
}

func (c *Client) StopADB(){c.adbMu.Lock();defer c.adbMu.Unlock();if c.adb!=nil{c.adb.close();c.adb=nil}}

func (c *Client) adbStatus() map[string]any {
    c.adbMu.Lock();defer c.adbMu.Unlock()
    if c.adb==nil||time.Now().After(c.adb.expires){return map[string]any{"active":false}}
    return map[string]any{"active":true,"allowedPeer":c.adb.peer.String(),"expires":c.adb.expires.Unix(),"connectPort":adbConnectPort,"pairPort":adbPairPort,"pairing":c.adb.pairing}
}

func (c *Client) ADBStatus() string{raw,_:=json.Marshal(c.adbStatus());return string(raw)}
