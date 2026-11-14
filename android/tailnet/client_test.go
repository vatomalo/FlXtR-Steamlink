package flxtrnet

import "testing"

func TestPrivatePeerBoundary(t *testing.T){
    good:="http://100.70.1.2:43927/0123456789abcdef0123456789abcdef/capabilities"
    if _,err:=validRequest("GET",good);err!=nil{t.Fatal(err)}
    for _,bad:=range []string{"http://192.168.10.2:43927/.well-known/flxtr","http://100.70.1.2:22/.well-known/flxtr","http://8.8.8.8:43927/.well-known/flxtr","http://user@100.70.1.2:43927/.well-known/flxtr","http://100.70.1.2:43927/localapi/v0/status","http://100.70.1.2:43927/.well-known/flxtr?redirect=1"}{if _,err:=validRequest("GET",bad);err==nil{t.Fatalf("accepted %s",bad)}}
    if _,err:=validRequest("POST",good);err==nil{t.Fatal("allowed upload outside handoff")}
    if _,err:=validRequest("POST","http://100.69.7.81:43928/som-ai/v1/step");err!=nil{t.Fatal(err)}
    if _,err:=validRequest("GET","http://100.69.7.81:43928/som-ai/v1/step");err==nil{t.Fatal("allowed AI snapshot over GET")}
}

func TestAndroidInterfaceAddresses(t *testing.T){
    got,err:=parseInterfaces(`[{"name":"wlan0","index":3,"mtu":1500,"flags":19,"addresses":["192.168.10.20/24","bad"]}]`)
    if err!=nil||len(got)!=1{t.Fatalf("interfaces: %v %v",got,err)}
    addrs,err:=got[0].Addrs();if err!=nil||len(addrs)!=1||addrs[0].String()!="192.168.10.20/24"{t.Fatalf("addresses: %v %v",addrs,err)}
}
