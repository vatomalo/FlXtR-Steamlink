package org.flxtr.android;

import com.limelight.flxtr.GamezBridge;
import org.junit.Test;
import java.util.List;
import static org.junit.Assert.*;

public class GamezBridgeTest {
    @Test public void validatesPrivateTokenAndSteamLinkCatalogShape()throws Exception{
        assertTrue(GamezBridge.validToken("A1234567890123456789012345678901"));
        assertFalse(GamezBridge.validToken("short"));
        assertFalse(GamezBridge.validToken("A123456789012345678901234567890!"));
        List<GamezBridge.Game> games=GamezBridge.parse("{\"games\":[{\"id\":\"ABC123\",\"title\":\"Final Fantasy VII Remake\"}]}");
        assertEquals(1,games.size());assertEquals("ABC123",games.get(0).id);assertEquals("Final Fantasy VII Remake",games.get(0).title);
    }
    @Test(expected=java.io.IOException.class) public void rejectsDuplicateCatalogIds()throws Exception{GamezBridge.parse("{\"games\":[{\"id\":\"1\",\"title\":\"One\"},{\"id\":\"1\",\"title\":\"Two\"}]}");}
}
