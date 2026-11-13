package org.flxtr.android;

import org.json.JSONObject;
import org.json.JSONArray;
import java.io.*;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.*;
import java.util.regex.*;

final class StreamResolver {
    private static final String FMOVIES_PLAYER="https://videm.buzz";
    interface Progress {void update(String message);}
    interface ExtraBackend {StreamProtocol.Stream resolve(CatalogItem item,int attempt,Request request,Progress progress) throws Exception;}
    static volatile ExtraBackend extraBackend;
    static final class Request {
        volatile boolean cancelled;
        volatile Runnable onCancel;
        volatile HttpsURLConnection connection;
        final long deadline;
        Request(){this(90);}
        Request(int seconds){deadline=System.nanoTime()+seconds*1_000_000_000L;}
        void cancel(){cancelled=true;HttpsURLConnection active=connection;if(active!=null)active.disconnect();if(onCancel!=null)onCancel.run();}
        void check() throws IOException {if(cancelled||Thread.currentThread().isInterrupted())throw new InterruptedIOException("Cancelled");if(System.nanoTime()>deadline)throw new IOException("Stream lookup timed out");}
    }
    static final class Session {
        private final CatalogItem item;
        private List<String> servers;
        private int next;
        Session(CatalogItem item){this.item=item;}
        StreamProtocol.Stream next(Request request,Progress progress) throws Exception {
            if(!item.kind.equals("movie")&&!item.kind.equals("episode")){
                if(extraBackend==null)throw new IOException("Media backend unavailable");
                int tries=0;boolean continuous=item.kind.equals("mtv-channel")||item.kind.equals("tv-channel");
                while(tries++<2&&(continuous||next<2)){request.check();try{StreamProtocol.Stream result=extraBackend.resolve(item,next++,request,progress);if(result.offlineId==null&&CatalogItem.validStream(result.url)){probe(result,request);if(result.audioUrl!=null){StreamProtocol.Stream audio=new StreamProtocol.Stream(result.audioUrl,result.server);audio.headers.clear();audio.headers.putAll(result.headers);probe(audio,request);}}result.item=result.item==null?item:result.item;return result;}catch(Exception failure){request.check();}}
                throw new IOException("No working media source was found");
            }
            String path=StreamProtocol.path(item);
            if(servers==null){
                progress.update("Finding available servers…");
                try{servers=new ArrayList<>(StreamProtocol.servers(lookup(path,"",request)));}catch(Exception unavailable){request.check();servers=new ArrayList<>();}
                servers.add("fmovies");servers.add("sflix");
            }
            while(next<servers.size()){
                request.check();String server=servers.get(next++);progress.update("Opening server "+next+" of "+servers.size()+"…");
                try{StreamProtocol.Stream stream=server.equals("fmovies")||server.equals("sflix")?web(item,server,request):StreamProtocol.stream(lookup(path,server,request),server);probe(stream,request);if(stream.subtitles.isEmpty())discoverCaptions(item,stream,request);request.check();stream.item=item;return stream;}
                catch(Exception unavailable){request.check();/* Try the next provider; never log signed media URLs. */}
            }
            throw new IOException("No playable stream was returned. Please retry or choose another title.");
        }
    }
    private static void discoverCaptions(CatalogItem item,StreamProtocol.Stream stream,Request request) throws IOException {
        String path=item.kind.equals("movie")?"movie/"+item.id:"tv/"+item.id+"/"+item.season+"/"+item.episode;
        for(int version=2;version>=1&&stream.subtitles.isEmpty();version--){
            request.check();
            try{String raw=MediaHttp.text("https://sub.vdrk.site/v"+version+"/"+path,Collections.emptyMap(),524288,request);JSONObject root=new JSONObject();root.put("tracks",new org.json.JSONArray(raw));MediaCaptions.read(root,stream);}catch(Exception unavailable){request.check();}
        }
    }
    static StreamProtocol.Stream web(CatalogItem item,String provider,Request request) throws Exception {
        return provider.equals("fmovies")?fmovies(item,request):sflix(item,request);
    }
    private static StreamProtocol.Stream sflix(CatalogItem item,Request request) throws Exception {
        String path=item.kind.equals("movie")?"movie/"+item.id:"tv/"+item.id+"/"+item.season+"/"+item.episode;
        Map<String,String> site=Collections.singletonMap("Referer","https://ssflix.pro/");
        String page=text("https://vidspark.to/"+path,site,request,"SFlix player page request");
        Matcher asset=Pattern.compile("src=[\\\"']/assets/index-([A-Za-z0-9._-]+)[\\\"']").matcher(page);
        if(!asset.find())throw new IOException("SFlix player asset discovery failed");
        String script=text("https://vidspark.to/assets/index-"+asset.group(1),Collections.singletonMap("Referer","https://vidspark.to/"),request,"SFlix player asset request");
        Matcher key=Pattern.compile("var\\s+fz=`([0-9A-Fa-f]{16,127})`").matcher(script);if(!key.find())throw new IOException("SFlix player key discovery failed");
        Map<String,String> headers=new HashMap<>();headers.put("Referer","https://vidspark.to/");headers.put("x-player-key",key.group(1));
        JSONObject root=new JSONObject(text("https://vidspark.to/api/vidora/v1/"+path,headers,request,"SFlix source API request"));
        JSONArray sources=root.optJSONArray("sources");if(!root.optBoolean("result")||sources==null||sources.length()==0)throw new IOException("SFlix source API returned no source");
        return webStream(sources.getJSONObject(0),"SFLIX","https://vidspark.to");
    }
    private static StreamProtocol.Stream fmovies(CatalogItem item,Request request) throws Exception {
        String path=item.kind.equals("movie")?"/embed/movie/"+item.id:"/embed/tv/"+item.id+"/"+item.season+"/"+item.episode;
        String embed=FMOVIES_PLAYER+path;
        JSONObject metadata=assignment(text(embed,Collections.singletonMap("Referer","https://www.f-moviess.org/"),request,"FMovies player page request"),"var Q = ");
        String token=metadata.optString("t","");JSONObject ssr=metadata.optJSONObject("ssr");JSONArray rows=ssr==null?null:ssr.optJSONArray("servers");
        if(token.isEmpty()||rows==null)throw new IOException("FMovies player metadata was incomplete");
        Exception last=null;
        for(int i=0;i<Math.min(2,rows.length());i++){
            String ref=rows.optJSONObject(i)==null?"":rows.optJSONObject(i).optString("ref","");if(ref.isEmpty())continue;
            try{
                String url=FMOVIES_PLAYER+"/api.php?a=play&ref="+MediaHttp.encode(ref)+"&t="+MediaHttp.encode(token);
                JSONObject source=new JSONObject(text(url,Collections.singletonMap("Referer",embed),request,"FMovies source API request"));
                return webStream(source,"FMOVIES",FMOVIES_PLAYER);
            }catch(Exception failure){request.check();last=failure;}
        }
        throw new IOException("FMovies source API returned no compatible source",last);
    }
    private static JSONObject assignment(String page,String marker) throws IOException {
        int start=page.indexOf(marker);if(start<0)throw new IOException("FMovies player metadata discovery failed");start+=marker.length();
        int depth=0;char quote=0;boolean escaped=false;
        for(int i=start;i<page.length();i++){
            char value=page.charAt(i);
            if(quote!=0){if(escaped)escaped=false;else if(value=='\\')escaped=true;else if(value==quote)quote=0;continue;}
            if(value=='\"'||value=='\''){quote=value;continue;}if(value=='{')depth++;else if(value=='}'&&--depth==0){try{return new JSONObject(page.substring(start,i+1));}catch(Exception invalid){break;}}
        }
        throw new IOException("FMovies player metadata was invalid");
    }
    private static StreamProtocol.Stream webStream(JSONObject source,String provider,String origin) throws IOException {
        String url=source.optString("url",source.optString("file",""));if(url.startsWith("/")&&!url.startsWith("//"))url=origin+url;
        if(!CatalogItem.validStream(url))throw new IOException(provider+" returned no compatible HTTPS stream");
        StreamProtocol.Stream stream=new StreamProtocol.Stream(url,provider);stream.headers.clear();stream.headers.put("User-Agent",StreamProtocol.UA);stream.headers.put("Origin",origin);stream.headers.put("Referer",origin+"/");
        MediaCaptions.read(source,stream);return stream;
    }
    private static JSONObject lookup(String path,String server,Request request) throws Exception {
        request.check();
        synchronized(StreamDecoder.LOCK){
            request.check();
            try{
                String key=StreamDecoder.nativeBegin();
                if(key==null||key.length()!=64)throw new IOException("Stream decoder unavailable");
                String marker=flixerMarker(request);
                JSONObject clock=new JSONObject(text(StreamProtocol.BASE+"/api/time",Collections.emptyMap(),request,"Flixer clock request"));
                long time=clock.optLong("timestamp",0);if(time<=0)throw new IOException("Provider clock unavailable");
                String timestamp=Long.toString(time);byte[] random=new byte[16];new SecureRandom().nextBytes(random);
                String nonce=Base64.getEncoder().withoutPadding().encodeToString(random).replace("/","").replace("+","");
                Map<String,String> headers=new HashMap<>();headers.put("Origin","https://flixer.gd");headers.put("Referer","https://flixer.gd/");headers.put("Accept","text/plain");
                headers.put("x-fingerprint-lite",marker);headers.put("X-Api-Key",key);headers.put("X-Request-Timestamp",timestamp);headers.put("X-Request-Nonce",nonce);
                headers.put("X-Request-Signature",StreamProtocol.signature(key,timestamp,nonce,path));headers.put("X-Client-Fingerprint",StreamProtocol.fingerprint());
                if(server.isEmpty())headers.put("bW90aGFmYWth","1");else{if(!StreamProtocol.validServer(server))throw new IOException("Invalid server");headers.put("X-Only-Sources","1");headers.put("X-Server",server);}
                String raw=text(StreamProtocol.BASE+path,headers,request,"Flixer authenticated request");request.check();
                String decoded;try{decoded=StreamDecoder.nativeDecode(raw,key);}catch(IOException failure){throw new IOException("Flixer response decode failed",failure);}
                request.check();try{return new JSONObject(decoded);}catch(org.json.JSONException failure){throw new IOException("Flixer decoded response was not valid JSON",failure);}
            }finally{StreamDecoder.nativeEnd();}
        }
    }
    static String markerFromScript(String script) throws IOException {
        Matcher match=Pattern.compile("[\\\"']x-fingerprint-lite[\\\"']\\s*,\\s*[\\\"']([A-Za-z0-9]{8,63})[\\\"']").matcher(script);
        if(!match.find())throw new IOException("Flixer client marker discovery failed");return match.group(1);
    }
    private static String flixerMarker(Request request) throws IOException {
        String page=text("https://flixer.gd/",Collections.emptyMap(),request,"Flixer client page request");
        Matcher asset=Pattern.compile("/assets/js/index-[A-Za-z0-9._-]+\\.js").matcher(page);
        if(!asset.find())throw new IOException("Flixer client asset discovery failed");
        Map<String,String> headers=Collections.singletonMap("Referer","https://flixer.gd/");
        return markerFromScript(text("https://flixer.gd"+asset.group(),headers,request,"Flixer client asset request"));
    }
    private static HttpsURLConnection connect(String url,Map<String,String> headers,Request request) throws IOException {
        request.check();if(!CatalogItem.validStream(url))throw new IOException("Invalid HTTPS media URL");
        HttpsURLConnection connection=(HttpsURLConnection)new URL(url).openConnection();request.connection=connection;
        connection.setConnectTimeout(7000);connection.setReadTimeout(12000);connection.setInstanceFollowRedirects(false);connection.setRequestProperty("User-Agent",StreamProtocol.UA);
        for(Map.Entry<String,String> header:headers.entrySet())connection.setRequestProperty(header.getKey(),header.getValue());
        request.check();return connection;
    }
    private static String text(String url,Map<String,String> headers,Request request,String stage) throws IOException {
        HttpsURLConnection connection=connect(url,headers,request);
        try{
            int code=connection.getResponseCode();if(code!=200)throw new IOException(stage+" failed (HTTP "+code+")");
            if(connection.getContentLengthLong()>2*1024*1024)throw new IOException("Provider response is too large");
            try(InputStream in=connection.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){
                byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1){request.check();if(out.size()+n>2*1024*1024)throw new IOException("Provider response is too large");out.write(buffer,0,n);}
                return new String(out.toByteArray(),StandardCharsets.UTF_8);
            }
        }finally{request.connection=null;connection.disconnect();}
    }
    static boolean compatibleHls(String playlist){
        Matcher variants=Pattern.compile("RESOLUTION=([0-9]+)x([0-9]+)",Pattern.CASE_INSENSITIVE).matcher(playlist);boolean found=false;
        while(variants.find()){found=true;try{if(Integer.parseInt(variants.group(1))<=1280&&Integer.parseInt(variants.group(2))<=720)return true;}catch(NumberFormatException invalid){return false;}}
        return !found;
    }
    static String firstHlsEntry(String playlist){for(String line:playlist.split("\\r?\\n")){line=line.trim();if(!line.isEmpty()&&!line.startsWith("#"))return line;}return null;}
    private static void probe(StreamProtocol.Stream stream,Request request) throws IOException {probe(stream,stream.url,new HashMap<>(stream.headers),request,0);}
    private static void probe(StreamProtocol.Stream stream,String initialUrl,Map<String,String> headers,Request request,int depth) throws IOException {
        if(depth>3)throw new IOException("HLS playlist nesting exceeded");
        String url=initialUrl;headers.put("Range","bytes=0-65535");
        for(int redirect=0;redirect<=3;redirect++){
            HttpsURLConnection connection=connect(url,headers,request);
            try{
                int code=connection.getResponseCode();
                if(code>=300&&code<400){String location=connection.getHeaderField("Location");if(location==null)throw new IOException("Missing stream redirect");url=new URL(new URL(url),location).toString();continue;}
                if(code!=200&&code!=206)throw new IOException("Stream is unavailable");
                String type=connection.getContentType();
                try(InputStream in=connection.getInputStream()){
                    byte[] prefix=new byte[32768];int used=0,n;while(used<prefix.length&&(n=in.read(prefix,used,prefix.length-used))>0){request.check();used+=n;}
                    String head=new String(prefix,0,used,StandardCharsets.UTF_8).trim();
                    if(head.startsWith("#EXTM3U")||(type!=null&&type.toLowerCase(Locale.ROOT).contains("mpegurl"))){
                        String lower=head.toLowerCase(Locale.ROOT);if(!compatibleHls(head)||((lower.contains("hvc1")||lower.contains("hev1"))&&!lower.contains("avc1")&&!lower.contains("h264")))throw new IOException("Server has no compatible H.264 rendition");
                        stream.mime="application/x-mpegURL";
                        String entry=firstHlsEntry(head);if(entry==null)throw new IOException("HLS playlist has no media entries");
                        probe(stream,new URL(new URL(url),entry).toString(),headers,request,depth+1);
                    }
                    else if(used<1||head.startsWith("<!DOCTYPE")||head.startsWith("<html")||(type!=null&&type.contains("text/html")))throw new IOException("Server returned a web page instead of video");
                    return;
                }
            }finally{request.connection=null;connection.disconnect();}
        }
        throw new IOException("Too many media redirects");
    }
}
