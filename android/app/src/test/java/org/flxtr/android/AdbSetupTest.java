package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;

public class AdbSetupTest {
    @Test public void separatePortsAreValidated(){
        assertEquals(37123,AdbSetup.port(" 37123 ",false));assertEquals(0,AdbSetup.port("",true));
        for(String value:new String[]{"192.168.10.133","192.168.10.133:37123","22","65536","-1234",""}){
            try{AdbSetup.port(value,false);fail("Accepted invalid port: "+value);}catch(IllegalArgumentException expected){}
        }
    }
}
