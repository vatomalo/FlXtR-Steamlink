package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;
public class TvHandoffTest {
 @Test public void pairingOnlyAcceptsPrivateLiteralAddresses()throws Exception {
   for(String host:new String[]{"192.168.1.20","10.0.0.4","172.16.1.2","100.100.1.3"})assertEquals(host,TvHandoff.endpoint("http://"+host+":5000/0123456789abcdef0123456789abcdef/").getHost());
   for(String url:new String[]{"http://8.8.8.8:5000/0123456789abcdef0123456789abcdef/","http://evil.example:5000/0123456789abcdef0123456789abcdef/","http://192.168.1.2:5000/../","http://user@192.168.1.2:5000/0123456789abcdef0123456789abcdef/","http://192.168.1.2:5000/0123456789abcdef0123456789abcdef/?x=1"}){try{TvHandoff.endpoint(url);fail(url);}catch(java.io.IOException expected){}}
 }
}
