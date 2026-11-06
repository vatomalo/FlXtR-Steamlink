package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;
import org.json.*;

public class MediaStartTest {
    private CatalogItem item(String kind,int id,int season,int episode){return new CatalogItem(kind,id,season,episode,"Title","","","");}
    @Test public void onlySeriesPostersRequireEpisodeSelection(){assertTrue(MediaStart.group(item("tv",42,0,0)));assertTrue(MediaStart.group(item("kiss",22,0,0)));assertFalse(MediaStart.group(item("movie",42,0,0)));assertFalse(MediaStart.group(item("episode",42,1,2)));}
    @Test public void seasonAndAnimeHistoryStayWithTheirOwnSeries()throws Exception{
        CatalogItem show=item("tv",42,0,0),season=item("season",42,2,0),anime=item("kiss",99,0,0);
        assertTrue(MediaStart.belongs(show,item("episode",42,1,2)));assertFalse(MediaStart.belongs(season,item("episode",42,1,2)));assertTrue(MediaStart.belongs(season,item("episode",42,2,3)));
        assertTrue(MediaStart.belongs(anime,item("kiss-episode",123,99,1)));assertFalse(MediaStart.belongs(anime,item("kiss-episode",123,98,1)));
        JSONArray history=new JSONArray().put(item("episode",43,1,1).json()).put(item("episode",42,2,3).json());
        assertEquals(3,MediaStart.recent(show,history).getInt("episode"));
    }
    @Test public void romFoldersFollowTheSystemRatherThanAnEmulatorLabel(){assertEquals("NES",RomTransfer.system("Game.NES"));assertEquals("SNES",RomTransfer.system("Game.smc"));assertEquals("GBC",RomTransfer.system("Game.gbc"));assertEquals("",RomTransfer.system("Game.cue"));}
}
