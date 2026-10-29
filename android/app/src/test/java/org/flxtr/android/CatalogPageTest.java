package org.flxtr.android;
import org.junit.Test;
import org.json.JSONObject;
import org.json.JSONException;
import static org.junit.Assert.*;
public class CatalogPageTest {
    @Test public void movieMetadataPreservesUnicode() throws Exception {
        CatalogPage page=CatalogPage.parse(new JSONObject("{\"total_pages\":9,\"results\":[{\"id\":12,\"title\":\"El niño\",\"release_date\":\"2024-05-12\",\"vote_average\":8.2,\"poster_path\":\"/poster.jpg\"}]}"),"movie",0,0);
        assertEquals("El niño",page.items.get(0).title);assertEquals("movie:12:0:0",page.items.get(0).key());assertEquals(9,page.pages);
    }
    @Test public void specialsKeepParentShowIdentity() throws Exception {
        CatalogItem season=CatalogPage.parse(new JSONObject("{\"seasons\":[{\"id\":999,\"season_number\":0,\"name\":\"Specials\"}]}"),"season",42,0).items.get(0);
        assertEquals("season:42:0:0",season.key());
    }
    @Test public void episodeKeepsShowSeasonEpisode() throws Exception {
        CatalogItem item=CatalogPage.parse(new JSONObject("{\"episodes\":[{\"id\":777,\"episode_number\":3,\"name\":\"Chapter three\",\"still_path\":\"/still.jpg\"}]}"),"episode",42,2).items.get(0);
        assertEquals("episode:42:2:3",item.key());assertEquals("/still.jpg",item.poster);
    }
    @Test(expected=JSONException.class) public void errorResponseIsNotAnEmptySuccessfulPage() throws Exception {CatalogPage.parse(new JSONObject("{\"error\":\"unavailable\"}"),"movie",0,0);}
    @Test public void invalidRowsAreSkippedAndPagingIsBounded() throws Exception {
        CatalogPage page=CatalogPage.parse(new JSONObject("{\"total_pages\":999999,\"results\":[null,{\"id\":0},{\"id\":7,\"name\":\"Show\",\"poster_path\":\"/../../secret\"}]}"),"tv",0,0);
        assertEquals(1,page.items.size());assertEquals(500,page.pages);assertEquals("",page.items.get(0).poster);
    }
    @Test public void favoriteRoundTripPreservesIdentity() throws Exception {
        CatalogItem item=new CatalogItem("episode",4,2,8,"Finale","S02 E08","A story","/a.jpg");
        CatalogItem restored=CatalogItem.saved(item.json());assertEquals(item.key(),restored.key());assertEquals(item.overview,restored.overview);
    }
    @Test public void unsafeStreamLinksAreRejected() {
        assertFalse(CatalogItem.validStream("http://example.com/movie.mp4"));assertFalse(CatalogItem.validStream("file:///sdcard/movie.mp4"));
        assertFalse(CatalogItem.validStream("https://user:pass@example.com/movie.mp4"));assertFalse(CatalogItem.validStream("https://example.com/\nmovie"));
        assertTrue(CatalogItem.validStream("https://example.com/master.m3u8?token=abc%20def"));
    }
}
