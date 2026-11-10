package flxtrnet

import (
    "archive/zip"
    "bytes"
    "crypto/sha256"
    "encoding/hex"
    "encoding/json"
    "net/http/httptest"
    "os"
    "path/filepath"
    "strings"
    "testing"
)

func romBundle(t *testing.T,meta romMetadata,data []byte) string{
    t.Helper();var buffer bytes.Buffer;bundle:=zip.NewWriter(&buffer);entry,_:=bundle.Create("rom.json");raw,_:=json.Marshal(meta);entry.Write(raw);entry,_=bundle.Create("rom.bin");entry.Write(data);bundle.Close()
    path:=filepath.Join(t.TempDir(),"rom.zip");if err:=os.WriteFile(path,buffer.Bytes(),0600);err!=nil{t.Fatal(err)};return path
}
func romMeta(system,name string,data []byte)romMetadata{sum:=sha256.Sum256(data);return romMetadata{Protocol:1,System:system,Name:name,Size:int64(len(data)),SHA256:hex.EncodeToString(sum[:])}}
func TestROMSystemStorageAndDuplicateProtection(t *testing.T){
    root:=t.TempDir();data:=[]byte("original test ROM")
    for system,extensions:=range romSystems{
        meta:=romMeta(system,"Fixture"+extensions[0],data);archive:=romBundle(t,meta,data)
        receipt,err:=receiveROM(archive,root);if err!=nil{t.Fatal(err)}
        if receipt.Path!=filepath.Join(root,system,meta.Name){t.Fatal("wrong system folder",receipt.Path)}
        second,err:=receiveROM(archive,root);if err!=nil||!second.AlreadyPresent{t.Fatal("retry was not idempotent",err)}
        changed:=[]byte("changed");_,err=receiveROM(romBundle(t,romMeta(system,meta.Name,changed),changed),root);if err==nil{t.Fatal("overwrote an existing ROM")}
        actual,_:=os.ReadFile(receipt.Path);if !bytes.Equal(actual,data){t.Fatal("existing ROM changed")}
    }
}
func TestROMTraversalAndChecksumFailures(t *testing.T){
    root:=t.TempDir();data:=[]byte("test");meta:=romMeta("NES","Fixture.nes",data)
    for _,name:=range []string{"../outside.nes","NES/inside.nes",`..\outside.nes`,".hidden.nes","wrong.sfc"}{bad:=meta;bad.Name=name;if validateROM(bad)==nil{t.Fatal("accepted invalid name",name)}}
    meta.SHA256=strings.Repeat("0",64)
    if _,err:=receiveROM(romBundle(t,meta,data),root);err==nil{t.Fatal("accepted wrong checksum")}
    entries,_:=os.ReadDir(filepath.Join(root,"NES"));if len(entries)!=0{t.Fatal("failed transfer left partial files")}
}
func TestROMServiceRequiresPrivateEndpointAndRejectsBrowserOrigins(t *testing.T){
    client:=&Client{identity:"test-device"};client.rom.root=t.TempDir();client.rom.token=strings.Repeat("a",32);client.rom.name="Android"
    for _,origin:=range []string{"","https://unrelated.example"}{
        req:=httptest.NewRequest("GET","http://100.70.1.2:43927/.well-known/flxtr",nil);req.RemoteAddr="100.69.7.81:12345";if origin!=""{req.Header.Set("Origin",origin)}
        response:=httptest.NewRecorder();client.romHTTP(response,req)
        if origin!=""&&response.Code!=403{t.Fatal("accepted unrelated origin")};if origin==""&&(response.Code!=200||!strings.Contains(response.Body.String(),"rom-receiver")){t.Fatal("discovery failed",response.Body.String())}
    }
    if _,err:=validRequest("POST","http://100.70.1.2:43927/"+strings.Repeat("a",32)+"/roms");err!=nil{t.Fatal("ROM route rejected",err)}
}
