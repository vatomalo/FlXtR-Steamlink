package org.flxtr.android;
import org.junit.Test;
import org.json.JSONObject;
import org.json.JSONArray;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.util.Arrays;
import static org.junit.Assert.*;
public class StreamProtocolTest {
    @Test public void titlePathsPreserveEpisodeAndSpecialsIdentity() throws Exception {
        assertEquals("/api/tmdb/movie/550/images",StreamProtocol.path(new CatalogItem("movie",550,0,0,"","","","")));
        assertEquals("/api/tmdb/tv/42/season/0/episode/3/images",StreamProtocol.path(new CatalogItem("episode",42,0,3,"","","","")));
    }
    @Test(expected=IOException.class) public void wholeSeriesIsNotAnEpisode() throws Exception {StreamProtocol.path(new CatalogItem("tv",42,0,0,"","","",""));}
    @Test public void requestSignatureMatchesIndependentVector() throws Exception {
        assertEquals("B2OP7Cw1cfXhXB304ZzirrNidv/XgYCg52WSobGhRAk=",StreamProtocol.signature(new String(new char[64]).replace('\0','a'),"123456","abc","/api/tmdb/movie/550/images"));
    }
    @Test public void fingerprintMatchesNativeUnsignedAlgorithm(){assertEquals("1x4dw4",StreamProtocol.fingerprint());}
    @Test public void serverNamesAreDeduplicatedAndValidated() throws Exception {
        JSONObject root=new JSONObject("{\"sources\":[{\"server\":\"echo\"},{\"server\":\"alpha\"},{\"server\":\"alpha\"},{\"server\":\"bad name\"}],\"servers\":{\"charlie\":{}}}");
        assertEquals(Arrays.asList("alpha","charlie","echo"),StreamProtocol.servers(root));
    }
    @Test public void selectedServerUsesOnlyMediaHeaders() throws Exception {
        JSONObject headers=new JSONObject().put("Referer","https://example.com/").put("X-Api-Key","never-forward").put("Authorization","never-forward").put("Origin","bad\r\nheader");
        JSONObject wrong=new JSONObject().put("server","echo").put("url","https://example.com/wrong.m3u8");
        JSONObject right=new JSONObject().put("server","alpha").put("url","https://example.com/right.m3u8").put("headers",headers);
        StreamProtocol.Stream stream=StreamProtocol.stream(new JSONObject().put("sources",new JSONArray().put(wrong).put(right)),"alpha");
        assertEquals("https://example.com/right.m3u8",stream.url);assertEquals("https://example.com/",stream.headers.get("Referer"));
        assertFalse(stream.headers.containsKey("X-Api-Key"));assertFalse(stream.headers.containsKey("Authorization"));assertEquals("https://flixer.gd",stream.headers.get("Origin"));
    }
    @Test public void objectSourceUsesFileField() throws Exception {
        StreamProtocol.Stream stream=StreamProtocol.stream(new JSONObject("{\"sources\":{\"file\":\"https://example.com/movie.mp4\"}}"),"alpha");assertEquals("https://example.com/movie.mp4",stream.url);
    }
    @Test(expected=IOException.class) public void insecureMediaUrlIsRejected() throws Exception {StreamProtocol.stream(new JSONObject("{\"sources\":{\"file\":\"http://example.com/movie.mp4\"}}"),"alpha");}
    @Test(expected=InterruptedIOException.class) public void cancelledLookupStopsBeforeNetworkOrDecoder() throws Exception {
        StreamResolver.Request request=new StreamResolver.Request();request.cancel();
        new StreamResolver.Session(new CatalogItem("movie",550,0,0,"","","","")).next(request,message->{});
    }
}
