package org.flxtr.android;
import android.content.*;
import android.net.Uri;
import androidx.media3.common.*;
import androidx.media3.database.StandaloneDatabaseProvider;
import androidx.media3.datasource.*;
import androidx.media3.datasource.cache.*;
import androidx.media3.exoplayer.offline.*;
import org.json.*;
import java.util.*;
import java.io.*;
import java.util.concurrent.*;
@androidx.annotation.OptIn(markerClass=androidx.media3.common.util.UnstableApi.class)
final class OfflineMedia {
    private static OfflineMedia instance;final SimpleCache cache;final DownloadManager manager;
    static synchronized OfflineMedia get(Context context){if(instance==null)instance=new OfflineMedia(context.getApplicationContext());return instance;}
    private OfflineMedia(Context context){
        StandaloneDatabaseProvider db=new StandaloneDatabaseProvider(context);cache=new SimpleCache(new File(context.getNoBackupFilesDir(),"offline-media"),new NoOpCacheEvictor(),db);
        Executor executor=Executors.newFixedThreadPool(3);
        manager=new DownloadManager(context,new DefaultDownloadIndex(db),request->{
            Map<String,String> headers=new HashMap<>();try{JSONObject data=new JSONObject(new String(request.data,java.nio.charset.StandardCharsets.UTF_8));JSONObject map=data.optJSONObject("headers");if(map!=null){Iterator<String> keys=map.keys();while(keys.hasNext()){String key=keys.next();headers.put(key,map.getString(key));}}}catch(Exception ignored){}
            DefaultHttpDataSource.Factory http=new DefaultHttpDataSource.Factory().setUserAgent(headers.getOrDefault("User-Agent","FlXtR-Android/0.4")).setDefaultRequestProperties(headers).setConnectTimeoutMs(10000).setReadTimeoutMs(20000);
            CacheDataSource.Factory factory=new CacheDataSource.Factory().setCache(cache).setUpstreamDataSourceFactory(new RangeDataSource.Factory(http));
            return new DefaultDownloaderFactory(factory,executor).createDownloader(request);
        });manager.setMaxParallelDownloads(2);
    }
    static void queue(Context context,CatalogItem item){Intent intent=new Intent(context,OfflineService.class).setAction("add");try{intent.putExtra("item",item.json().toString());}catch(Exception bad){return;}androidx.core.content.ContextCompat.startForegroundService(context,intent);}
    static void remove(Context context,String id){androidx.core.content.ContextCompat.startForegroundService(context,new Intent(context,OfflineService.class).setAction("remove").putExtra("id",id));}
    static List<CatalogItem> list(Context context){ArrayList<CatalogItem> rows=new ArrayList<>();
        try(DownloadCursor cursor=get(context).manager.getDownloadIndex().getDownloads()){
            while(cursor.moveToNext()){Download d=cursor.getDownload();JSONObject data=new JSONObject(new String(d.request.data,java.nio.charset.StandardCharsets.UTF_8));if(data.optBoolean("companion"))continue;CatalogItem item=CatalogItem.saved(data.getJSONObject("item"));
                String state=d.state==Download.STATE_COMPLETED?"Ready offline":d.state==Download.STATE_DOWNLOADING?"Downloading "+Math.max(0,(int)d.getPercentDownloaded())+"%":d.state==Download.STATE_FAILED?"Failed · retry":d.state==Download.STATE_REMOVING?"Removing":"Queued / waiting for Wi-Fi";
                if(d.state==Download.STATE_COMPLETED&&data.has("audioUrl")){Download audio=get(context).manager.getDownloadIndex().getDownload(d.request.id+"-audio");if(audio==null||audio.state!=Download.STATE_COMPLETED)state="Downloading audio";}
                rows.add(new CatalogItem("offline",0,0,0,item.title,state,item.overview,item.poster,d.request.id));
            }
        }catch(Exception ignored){}
        SharedPreferences prefs=context.getSharedPreferences("offline-pending",Context.MODE_PRIVATE);for(Map.Entry<String,?> e:prefs.getAll().entrySet())try{CatalogItem item=CatalogItem.saved(new JSONObject(String.valueOf(e.getValue())));boolean exists=false;for(CatalogItem row:rows)if(row.link.equals(e.getKey()))exists=true;if(!exists)rows.add(new CatalogItem("offline",0,0,0,item.title,"Preparing download…",item.overview,item.poster,e.getKey()));}catch(Exception ignored){}
        for(Map.Entry<String,?> e:context.getSharedPreferences("offline-errors",Context.MODE_PRIVATE).getAll().entrySet())try{CatalogItem item=original(context,e.getKey());boolean exists=false;for(CatalogItem row:rows)if(row.link.equals(e.getKey()))exists=true;if(item!=null&&!exists)rows.add(new CatalogItem("offline",0,0,0,item.title,String.valueOf(e.getValue()),item.overview,item.poster,e.getKey()));}catch(Exception ignored){}
        return rows;
    }
    static boolean complete(Context context,String id){try{Download d=get(context).manager.getDownloadIndex().getDownload(id);if(d==null||d.state!=Download.STATE_COMPLETED)return false;JSONObject data=new JSONObject(new String(d.request.data,java.nio.charset.StandardCharsets.UTF_8));if(data.has("audioUrl")){Download audio=get(context).manager.getDownloadIndex().getDownload(id+"-audio");return audio!=null&&audio.state==Download.STATE_COMPLETED;}return true;}catch(Exception error){return false;}}
    static CatalogItem original(Context context,String id){try{Download d=get(context).manager.getDownloadIndex().getDownload(id);if(d!=null)return CatalogItem.saved(new JSONObject(new String(d.request.data,java.nio.charset.StandardCharsets.UTF_8)).getJSONObject("item"));String pending=context.getSharedPreferences("offline-items",Context.MODE_PRIVATE).getString(id,null);return pending==null?null:CatalogItem.saved(new JSONObject(pending));}catch(Exception error){return null;}}
    static StreamProtocol.Stream stream(Context context,CatalogItem item) throws Exception {
        if(!complete(context,item.link))throw new IOException("Download is not ready yet");Download d=get(context).manager.getDownloadIndex().getDownload(item.link);JSONObject data=new JSONObject(new String(d.request.data,java.nio.charset.StandardCharsets.UTF_8));
        StreamProtocol.Stream stream=new StreamProtocol.Stream(d.request.uri.toString(),"Offline");stream.headers.clear();stream.mime=d.request.mimeType;stream.audioUrl=data.optString("audioUrl",null);stream.offlineId=item.link;stream.item=CatalogItem.saved(data.getJSONObject("item"));return stream;
    }
    static MediaItem item(Context context,String id) throws IOException {Download d=get(context).manager.getDownloadIndex().getDownload(id);if(d==null)throw new IOException("Download no longer exists");return d.request.toMediaItem();}
    static CacheDataSource.Factory source(Context context){return new CacheDataSource.Factory().setCache(get(context).cache).setUpstreamDataSourceFactory(null).setFlags(CacheDataSource.FLAG_BLOCK_ON_CACHE);}
}
