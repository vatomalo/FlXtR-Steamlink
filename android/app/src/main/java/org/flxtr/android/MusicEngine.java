package org.flxtr.android;
import android.content.Context;
import com.yausername.youtubedl_android.*;
import org.json.*;
import org.jsoup.Jsoup;
import java.util.*;
import java.util.concurrent.*;
import java.io.*;

/** yt-dlp/QuickJS run from the packaged Android runtime, never a Linux worker. */
final class MusicEngine {
    private static final ScheduledExecutorService watchdog=Executors.newSingleThreadScheduledExecutor();
    private static String execute(Context context,String address,boolean flat,boolean audio,StreamResolver.Request active) throws Exception {
        active.check();YoutubeDL.INSTANCE.init(context.getApplicationContext());preparePython(context);active.check();
        YoutubeDLRequest request=new YoutubeDLRequest(address);request.addOption("--skip-download");request.addOption("--no-warnings");request.addOption("--socket-timeout","15");request.addOption("--retries","1");request.addOption("--extractor-retries","1");request.addOption("--dump-single-json");request.addOption("--playlist-end",flat?"30":"1");
        if(flat)request.addOption("--flat-playlist");else{request.addOption("--no-playlist");request.addOption("-f",audio?"bestaudio[ext=m4a][audio_channels<=2]/bestaudio[ext=m4a]/bestaudio":"best[ext=mp4][height<=720][fps<=30]/bestvideo[vcodec^=avc1][height<=720][fps<=30]+bestaudio[ext=m4a][audio_channels<=2]/best[height<=720]/best");}
        String process="flxtr-"+UUID.randomUUID();Runnable kill=()->YoutubeDL.INSTANCE.destroyProcessById(process);active.onCancel=kill;ScheduledFuture<?> deadline=watchdog.schedule(kill,70,TimeUnit.SECONDS);
        try{String result=YoutubeDL.INSTANCE.execute(request,process,false,null).getOut();active.check();if(result.length()>4*1024*1024)throw new IOException("Music response is too large");return result;}finally{active.onCancel=null;deadline.cancel(false);}
    }
    private static synchronized void preparePython(Context context)throws IOException{
        File lib=new File(context.getNoBackupFilesDir(),"youtubedl-android/packages/python/usr/lib");
        File[] versions=lib.listFiles(file->file.isDirectory()&&file.getName().matches("python3\\.[0-9]+"));
        if(versions==null||versions.length!=1)throw new IOException("Music runtime is unavailable");
        File target=new File(versions[0],"site-packages/sitecustomize.py");
        byte[] content;try(InputStream in=context.getAssets().open("music-sitecustomize.py");ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] buffer=new byte[1024];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);content=out.toByteArray();}
        if(target.isFile()&&java.util.Arrays.equals(content,java.nio.file.Files.readAllBytes(target.toPath())))return;
        File temporary=new File(target.getParentFile(),"sitecustomize.py.tmp");try(OutputStream out=new FileOutputStream(temporary)){out.write(content);}
        if(!temporary.renameTo(target))throw new IOException("Cannot prepare music runtime");
    }
    static CatalogPage search(Context context,String query,int page,StreamResolver.Request request) throws Exception {
        if(query.matches("https://open\\.spotify\\.com/.*"))return spotify(query,request);
        String target=query.startsWith("https://")?youtubeAddress(query):"ytsearch30:"+(query.isEmpty()?"official music videos":query);
        JSONObject root=new JSONObject(execute(context,target,true,true,request));ArrayList<CatalogItem> rows=new ArrayList<>();JSONArray entries=root.optJSONArray("entries");if(entries==null)entries=new JSONArray().put(root);
        for(int i=0;i<entries.length()&&rows.size()<100;i++){JSONObject item=entries.optJSONObject(i);if(item==null)continue;String id=item.optString("id");if(!id.matches("[A-Za-z0-9_-]{11}"))continue;String poster=item.optString("thumbnail","");if(poster.isEmpty())poster="https://i.ytimg.com/vi/"+id+"/hqdefault.jpg";
            rows.add(new CatalogItem("music",0,0,0,item.optString("title","Music"),item.optString("channel",item.optString("uploader","YouTube")),"",CatalogItem.safePoster(poster),"https://www.youtube.com/watch?v="+id));}
        return new CatalogPage(rows,1);
    }
    static String youtubeAddress(String value) throws IOException {
        java.net.URI uri;try{uri=java.net.URI.create(value);}catch(Exception bad){throw new IOException("Invalid music URL");}
        String host=uri.getHost();if(!"https".equals(uri.getScheme())||host==null||!(host.equals("youtube.com")||host.equals("www.youtube.com")||host.equals("music.youtube.com")||host.equals("youtu.be"))||uri.getUserInfo()!=null)throw new IOException("Use a YouTube or public Spotify playlist URL");return value;
    }
    static CatalogPage spotify(String url,StreamResolver.Request request) throws Exception {
        java.util.regex.Matcher match=java.util.regex.Pattern.compile("https://open\\.spotify\\.com/(?:intl-[a-z]+/)?playlist/([A-Za-z0-9]{22})(?:\\?[^\\s]*)?").matcher(url);if(!match.matches())throw new IOException("Use a public Spotify playlist URL");
        org.jsoup.nodes.Element script=Jsoup.parse(MediaHttp.text("https://open.spotify.com/embed/playlist/"+match.group(1),request)).selectFirst("script#__NEXT_DATA__");if(script==null)throw new IOException("This Spotify playlist is not public");
        return spotifyJson(new JSONObject(script.data()));
    }
    static CatalogPage spotifyJson(JSONObject root) throws Exception {
        JSONArray tracks=root.getJSONObject("props").getJSONObject("pageProps").getJSONObject("state").getJSONObject("data").getJSONObject("entity").getJSONArray("trackList");ArrayList<CatalogItem> rows=new ArrayList<>();
        for(int i=0;i<Math.min(tracks.length(),100);i++){JSONObject track=tracks.getJSONObject(i);String title=track.optString("title"),artist=track.optString("subtitle");if(!title.isEmpty())rows.add(new CatalogItem("music-query",0,0,0,title,artist,"","",artist+" "+title));}return new CatalogPage(rows,1);
    }
    static StreamProtocol.Stream resolve(Context context,CatalogItem item,boolean video,StreamResolver.Request request) throws Exception {
        String address=item.kind.equals("music-query")?"ytsearch1:"+item.link:youtubeAddress(item.link);JSONObject info=new JSONObject(execute(context,address,false,!video,request));JSONArray entries=info.optJSONArray("entries");if(entries!=null&&entries.length()>0)info=entries.getJSONObject(0);
        JSONObject selected=info,audio=null;JSONArray formats=info.optJSONArray("requested_formats");if(formats!=null)for(int i=0;i<formats.length();i++){JSONObject format=formats.getJSONObject(i);if(!format.optString("vcodec","none").equals("none"))selected=format;else audio=format;}
        String url=selected.optString("url");if(!CatalogItem.validStream(url))throw new IOException("No compatible music stream");StreamProtocol.Stream stream=new StreamProtocol.Stream(url,"YouTube");stream.item=new CatalogItem(item.kind,0,0,0,info.optString("title",item.title),item.meta,"",item.poster,item.link);
        stream.headers.clear();stream.headers.put("User-Agent",StreamProtocol.UA);JSONObject headers=selected.optJSONObject("http_headers");if(headers==null)headers=info.optJSONObject("http_headers");if(headers!=null)for(String name:new String[]{"User-Agent","Referer","Origin"}){String value=headers.optString(name,"");if(!value.isEmpty()&&value.indexOf('\n')<0&&value.indexOf('\r')<0)stream.headers.put(name,value);}
        String ext=selected.optString("ext","");if(ext.equals("mp4"))stream.mime="video/mp4";else if(ext.equals("m4a"))stream.mime="audio/mp4";else if(ext.equals("webm"))stream.mime=video?"video/webm":"audio/webm";
        if(audio!=null&&CatalogItem.validStream(audio.optString("url")))stream.audioUrl=audio.optString("url");return stream;
    }
    static void update(Context context) throws Exception {YoutubeDL.INSTANCE.init(context.getApplicationContext());YoutubeDL.INSTANCE.updateYoutubeDL(context.getApplicationContext(),YoutubeDL.UpdateChannel._STABLE);}
}
