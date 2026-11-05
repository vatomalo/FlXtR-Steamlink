package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;
import org.json.JSONObject;

public class FlxtrDeviceTest {
    @Test public void discoveryCannotRedirectToAnotherHost()throws Exception{
        JSONObject reply=new JSONObject("{\"app\":\"FlXtR\",\"protocol\":1,\"endpoint\":\"http://100.70.1.2:43927/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/\"}");
        assertTrue(FlxtrDevice.announcement(reply,"100.70.1.2",true).receiver);
        try{FlxtrDevice.announcement(reply,"100.70.1.3",true);fail("Cross-host redirect accepted");}catch(java.io.IOException expected){}
        reply.put("app","Other app");
        try{FlxtrDevice.announcement(reply,"100.70.1.2",true);fail("Unrelated app accepted");}catch(java.io.IOException expected){}
    }
    @Test public void arpHintsRequireCompletePrivateNeighbors(){
        assertEquals(java.util.Arrays.asList("192.168.10.2"),FlxtrDevice.arpNeighbors("192.168.10.2 0x1 0x2 aa:bb:cc:dd:ee:ff * wlan0\n8.8.8.8 0x1 0x2 aa:bb:cc:dd:ee:ff * wlan0\n10.0.0.2 0x1 0x0 00:00:00:00:00:00 * wlan0"));
    }
}
