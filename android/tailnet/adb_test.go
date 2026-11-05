package flxtrnet

import (
    "io"
    "net"
    "net/netip"
    "testing"
    "time"
)

func TestADBBoundaries(t *testing.T){
    peer:=netip.MustParseAddr("100.69.7.81")
    if !adbPeer(&net.TCPAddr{IP:net.ParseIP("100.69.7.81"),Port:1234},peer){t.Fatal("build server denied")}
    for _,ip:=range []string{"100.69.7.82","192.168.10.133","127.0.0.1","::1"}{
        if adbPeer(&net.TCPAddr{IP:net.ParseIP(ip),Port:1234},peer){t.Fatal("accepted unexpected peer",ip)}
    }
    if !validADBPorts(37123,40123)||!validADBPorts(37123,0){t.Fatal("valid wireless debugging ports rejected")}
    for _,ports:=range [][2]int{{0,0},{555,0},{65536,0},{37123,37123},{37123,-1}}{
        if validADBPorts(ports[0],ports[1]){t.Fatal("accepted invalid ports",ports)}
    }
}

func TestADBProxyIsDuplexAndExpires(t *testing.T){
    local,err:=net.Listen("tcp","127.0.0.1:0");if err!=nil{t.Fatal(err)};defer local.Close()
    go func(){conn,err:=local.Accept();if err==nil{defer conn.Close();_,_=io.Copy(conn,conn)}}()
    port:=local.Addr().(*net.TCPAddr).Port
    bridge,err:=newADBBridge(func(string,string)(net.Listener,error){return net.Listen("tcp","127.0.0.1:0")},netip.MustParseAddr("127.0.0.1"),port,0,300*time.Millisecond)
    if err!=nil{t.Fatal(err)};defer bridge.close()
    conn,err:=net.Dial("tcp",bridge.listeners[0].Addr().String());if err!=nil{t.Fatal(err)};defer conn.Close()
    conn.SetDeadline(time.Now().Add(2*time.Second))
    payload:=[]byte("end-to-end TLS bytes");if _,err:=conn.Write(payload);err!=nil{t.Fatal(err)}
    got:=make([]byte,len(payload));if _,err:=io.ReadFull(conn,got);err!=nil||string(got)!=string(payload){t.Fatal("duplex forwarding failed",err)}
    if _,err:=conn.Read(make([]byte,1));err==nil{t.Fatal("expired session remained open")}
    if late,err:=net.DialTimeout("tcp",bridge.listeners[0].Addr().String(),time.Second);err==nil{late.Close();t.Fatal("expired listener remained open")}
}

func TestADBProxyRejectsOtherPeersBeforeDialingLocalService(t *testing.T){
    bridge,err:=newADBBridge(func(string,string)(net.Listener,error){return net.Listen("tcp","127.0.0.1:0")},netip.MustParseAddr("100.69.7.81"),37123,0,time.Minute)
    if err!=nil{t.Fatal(err)};defer bridge.close()
    conn,err:=net.Dial("tcp",bridge.listeners[0].Addr().String());if err!=nil{t.Fatal(err)};defer conn.Close()
    conn.SetReadDeadline(time.Now().Add(time.Second))
    if _,err:=conn.Read(make([]byte,1));err==nil{t.Fatal("unexpected peer retained a connection")}
}
