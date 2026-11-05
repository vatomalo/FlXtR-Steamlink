package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;
public class TvHandoffTest {
 @Test public void discoveryCannotRedirectToAnotherDeviceOrLan()throws Exception {
   String path="/0123456789abcdef0123456789abcdef/";
   org.json.JSONObject response=new org.json.JSONObject().put("protocol",1).put("endpoint","http://100.70.1.2:43927"+path);
   assertEquals("100.70.1.2",TvHandoff.discoveredEndpoint(response,"100.70.1.2").getHost());
   for(String address:new String[]{"http://100.70.1.3:43927","http://192.168.10.2:43927","http://100.70.1.2:5000","http://8.8.8.8:43927"}){
     response.put("endpoint",address+path);
     try{TvHandoff.discoveredEndpoint(response,"100.70.1.2");fail(address);}catch(java.io.IOException expected){}
   }
   response.put("endpoint","http://100.70.1.2:43927"+path).put("protocol",2);
   try{TvHandoff.discoveredEndpoint(response,"100.70.1.2");fail("Unsupported discovery protocol");}catch(java.io.IOException expected){}
 }
 @Test public void pairingOnlyAcceptsPrivateLiteralAddresses()throws Exception {
   for(String host:new String[]{"192.168.1.20","10.0.0.4","172.16.1.2","100.100.1.3"})assertEquals(host,TvHandoff.endpoint("http://"+host+":5000/0123456789abcdef0123456789abcdef/").getHost());
   for(String url:new String[]{"http://8.8.8.8:5000/0123456789abcdef0123456789abcdef/","http://evil.example:5000/0123456789abcdef0123456789abcdef/","http://192.168.1.2:5000/../","http://user@192.168.1.2:5000/0123456789abcdef0123456789abcdef/","http://192.168.1.2:5000/0123456789abcdef0123456789abcdef/?x=1"}){try{TvHandoff.endpoint(url);fail(url);}catch(java.io.IOException expected){}}
 }
}
