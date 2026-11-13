package org.flxtr.android;
import org.junit.Test;
import java.io.IOException;
import java.util.*;
import static org.junit.Assert.*;
public class GameFormatsTest {
    @Test public void detectsEmbeddedEngines(){assertEquals("ppsspp",GameFormats.core("homebrew.cso"));assertEquals("dolphin",GameFormats.core("game.RVZ"));assertEquals("scummvm",GameFormats.core("game.scummvm"));assertTrue(GameFormats.compatible("game.iso","ppsspp"));assertTrue(GameFormats.compatible("game.iso","dolphin"));assertFalse(GameFormats.compatible("game.nes","ppsspp"));assertEquals("pcsx_rearmed",GameFormats.core("game.iso"));}
    @Test public void somTurboOffersOptInProxyOnly(){
        assertEquals("snes9x",GameFormats.core("Secret of Mana, Turbo.smc"));
        assertEquals(Arrays.asList("snes9x","som_ai"),
                     Arrays.asList(GameFormats.cores("Secret of Mana, Turbo.smc")));
        assertTrue(GameFormats.compatible("Secret of Mana, Turbo.smc","som_ai"));
        assertFalse(GameFormats.compatible("Secret of Mana.smc","som_ai"));
        assertFalse(GameFormats.compatible("Secret of Mana, Turbo.sfc","som_ai"));
        assertEquals("Secret of Mana AI",GameFormats.label("som_ai"));
        assertEquals("snes9x",GameFormats.nextCore("Secret of Mana, Turbo.smc","",1));
        assertEquals("som_ai",GameFormats.nextCore("Secret of Mana, Turbo.smc","snes9x",1));
        assertEquals("",GameFormats.nextCore("Secret of Mana, Turbo.smc","som_ai",1));
        assertEquals("som_ai",GameFormats.nextCore("Secret of Mana, Turbo.smc","",-1));
    }
    @Test public void detectsSupportedSystems(){assertEquals("fceumm",GameFormats.core("Mario.NES"));assertEquals("snes9x",GameFormats.core("game.sfc"));assertEquals("mgba",GameFormats.core("game.gba"));assertEquals("pcsx_rearmed",GameFormats.core("disc.chd"));assertEquals("gambatte",GameFormats.core("game.gbc"));assertEquals("",GameFormats.core("track.bin"));assertEquals("",GameFormats.core("movie.mp4"));}
    @Test public void cuePreservesTracksAndRelativeDirectories()throws Exception {assertEquals(Arrays.asList("PS/Game/Track 01.bin","PS/Audio/02.bin"),GameFormats.references("PS/Game/game.cue","FILE \"Track 01.bin\" BINARY\nFILE \"../Audio/02.bin\" BINARY"));}
    @Test public void playlistHandlesCommentsAndWindowsSeparators()throws Exception {assertEquals(Arrays.asList("FF7/Disc 1/game.cue","FF7/disc2.chd"),GameFormats.references("FF7/game.m3u","\ufeff#EXTM3U\r\nDisc 1\\game.cue\r\ndisc2.chd\n"));}
    @Test(expected=IOException.class) public void cueCannotEscapeSelectedTree()throws Exception {GameFormats.references("game.cue","FILE \"../private.bin\" BINARY");}
    @Test(expected=IOException.class) public void playlistRejectsRemoteDiscs()throws Exception {GameFormats.references("game.m3u","https://host/game.iso");}
    @Test(expected=IOException.class) public void playlistRejectsAbsolutePaths()throws Exception {GameFormats.references("PS/game.m3u","/private/game.iso");}
    @Test(expected=IOException.class) public void cueNeedsTracks()throws Exception {GameFormats.references("game.cue","REM missing tracks");}
    @Test public void biosNamesAreExplicit(){assertTrue(GameFormats.bios("SCPH5501.BIN"));assertFalse(GameFormats.bios("game.bin"));assertFalse(GameFormats.bios("../scph5501.bin"));}
}
