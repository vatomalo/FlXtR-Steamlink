package org.flxtr.android;
import android.app.*;
import android.content.*;
import android.os.*;
import androidx.media3.common.*;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.offline.*;
import androidx.media3.exoplayer.offline.DownloadManager;
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.scheduler.Requirements;
import org.json.*;
import java.util.*;
import java.util.concurrent.*;
@androidx.annotation.OptIn(markerClass=androidx.media3.common.util.UnstableApi.class)
public final class OfflineService extends Service {
    private static final String CHANNEL="flxtr-downloads";private static final int NOTIFICATION=7201;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();private final Handler main=new Handler(Looper.getMainLooper());
    private final Map<String,StreamResolver.Request> preparing=new HashMap<>();private final Map<String,DownloadHelper> helpers=new HashMap<>();private DownloadManager manager;
    private final DownloadManager.Listener listener=new DownloadManager.Listener(){@Override public void onDownloadChanged(DownloadManager m,Download d,Exception error){getSharedPreferences("offline-pending",MODE_PRIVATE).edit().remove(d.request.id).apply();update();}@Override public void onIdle(DownloadManager m){update();}};
    @Override public void onCreate(){super.onCreate();NotificationManager notifications=getSystemService(NotificationManager.class);notifications.createNotificationChannel(new NotificationChannel(CHANNEL,"Media downloads",NotificationManager.IMPORTANCE_LOW));manager=OfflineMedia.get(this).manager;manager.addListener(listener);startForeground(NOTIFICATION,notification());main.post(tick);}
    @Override public int onStartCommand(Intent intent,int flags,int startId){
        manager.setRequirements(new Requirements(getSharedPreferences("android-settings",MODE_PRIVATE).getBoolean("wifi-downloads",true)?Requirements.NETWORK_UNMETERED:Requirements.NETWORK));manager.resumeDownloads();
        if(intent!=null&&"remove".equals(intent.getAction())){String id=intent.getStringExtra("id");if(id!=null){StreamResolver.Request request=preparing.remove(id);if(request!=null)request.cancel();DownloadHelper helper=helpers.remove(id);if(helper!=null)helper.release();getSharedPreferences("offline-pending",MODE_PRIVATE).edit().remove(id).apply();getSharedPreferences("offline-errors",MODE_PRIVATE).edit().remove(id).apply();getSharedPreferences("offline-items",MODE_PRIVATE).edit().remove(id).apply();manager.removeDownload(id);manager.removeDownload(id+"-audio");}}
        else if(intent!=null&&intent.hasExtra("item")){try{CatalogItem item=CatalogItem.saved(new JSONObject(intent.getStringExtra("item")));prepare(item);}catch(Exception bad){stopSelf();}}
        else for(Object stored:getSharedPreferences("offline-pending",MODE_PRIVATE).getAll().values())try{prepare(CatalogItem.saved(new JSONObject(String.valueOf(stored))));}catch(Exception ignored){}
        return START_STICKY;
    }
    private void prepare(CatalogItem item){
        String id=item.key();if(preparing.containsKey(id))return;
        if(manager.getRequirements().getNotMetRequirements(this)!=0){try{getSharedPreferences("offline-pending",MODE_PRIVATE).edit().putString(id,item.json().toString()).apply();getSharedPreferences("offline-items",MODE_PRIVATE).edit().putString(id,item.json().toString()).apply();}catch(Exception ignored){}return;}
        if(new StatFs(getNoBackupFilesDir().getAbsolutePath()).getAvailableBytes()<250L*1024*1024){ToastError("Not enough free space for a download");return;}
        StreamResolver.Request request=new StreamResolver.Request();preparing.put(id,request);try{getSharedPreferences("offline-pending",MODE_PRIVATE).edit().putString(id,item.json().toString()).apply();getSharedPreferences("offline-items",MODE_PRIVATE).edit().putString(id,item.json().toString()).apply();getSharedPreferences("offline-errors",MODE_PRIVATE).edit().remove(id).apply();}catch(Exception ignored){}
        worker.submit(()->{try{StreamProtocol.Stream stream=new StreamResolver.Session(item).next(request,text->{});main.post(()->{if(!preparing.containsKey(id))return;download(item,stream,request);});}catch(Exception error){main.post(()->failed(id,"Could not prepare this download. Retry from Downloads."));}});
    }
    private void download(CatalogItem item,StreamProtocol.Stream stream,StreamResolver.Request request){
        String id=item.key();
        try{
            JSONObject data=new JSONObject().put("item",item.json()).put("headers",new JSONObject(stream.headers));if(stream.audioUrl!=null)data.put("audioUrl",stream.audioUrl);
            DefaultHttpDataSource.Factory http=new DefaultHttpDataSource.Factory().setUserAgent(stream.headers.getOrDefault("User-Agent","FlXtR-Android/0.4")).setDefaultRequestProperties(stream.headers);
            MediaItem.Builder media=new MediaItem.Builder().setUri(stream.url);if(stream.mime!=null)media.setMimeType(stream.mime);
            DefaultTrackSelector.Parameters parameters=new DefaultTrackSelector.Parameters.Builder(this).setMaxVideoSize(1280,720).setPreferredVideoMimeTypes(MimeTypes.VIDEO_H264).setForceHighestSupportedBitrate(true).setPreferredAudioLanguage("en").setPreferredTextLanguage("en").build();
            DownloadHelper helper=DownloadHelper.forMediaItem(media.build(),parameters,new DefaultRenderersFactory(this),new RangeDataSource.Factory(http));helpers.put(id,helper);
            helper.prepare(new DownloadHelper.Callback(){
                @Override public void onPrepared(DownloadHelper ready){
                    if(!preparing.containsKey(id)){ready.release();return;}
                    manager.addDownload(ready.getDownloadRequest(id,data.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)));
                    if(stream.audioUrl!=null)try{JSONObject audio=new JSONObject(data.toString()).put("companion",true);manager.addDownload(new DownloadRequest.Builder(id+"-audio",android.net.Uri.parse(stream.audioUrl)).setData(audio.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)).build());}catch(Exception error){failed(id,"Could not queue audio");}
                    preparing.remove(id);helpers.remove(id);ready.release();update();
                }
                @Override public void onPrepareError(DownloadHelper helper,java.io.IOException error){helper.release();failed(id,"Could not read the download playlist");}
            });
        }catch(Exception error){failed(id,"Could not prepare offline media");}
    }
    private void failed(String id,String message){preparing.remove(id);helpers.remove(id);getSharedPreferences("offline-pending",MODE_PRIVATE).edit().remove(id).apply();getSharedPreferences("offline-errors",MODE_PRIVATE).edit().putString(id,message).apply();ToastError(message);update();}
    private void ToastError(String text){android.widget.Toast.makeText(this,text,android.widget.Toast.LENGTH_LONG).show();}
    private Notification notification(){int active=0,percent=-1;if(manager!=null)for(Download d:manager.getCurrentDownloads())if(d.state==Download.STATE_DOWNLOADING||d.state==Download.STATE_QUEUED){active++;percent=Math.max(percent,(int)d.getPercentDownloaded());}
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class).putExtra("section","downloads"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.stat_sys_download).setContentTitle("FlXtR downloads").setContentText(!preparing.isEmpty()?"Preparing "+preparing.size()+" media item(s)…":active+" queued / downloading").setProgress(100,Math.max(0,percent),percent<0).setContentIntent(open).setOngoing(true).build();
    }
    private void update(){getSystemService(NotificationManager.class).notify(NOTIFICATION,notification());boolean active=false;for(Download d:manager.getCurrentDownloads())if(d.state!=Download.STATE_COMPLETED&&d.state!=Download.STATE_FAILED&&d.state!=Download.STATE_STOPPED)active=true;if(preparing.isEmpty()&&helpers.isEmpty()&&manager.isIdle()&&!active&&getSharedPreferences("offline-pending",MODE_PRIVATE).getAll().isEmpty())stopSelf();}
    private final Runnable tick=new Runnable(){public void run(){if(manager.getRequirements().getNotMetRequirements(OfflineService.this)==0)for(Object stored:getSharedPreferences("offline-pending",MODE_PRIVATE).getAll().values())try{prepare(CatalogItem.saved(new JSONObject(String.valueOf(stored))));}catch(Exception ignored){}update();main.postDelayed(this,1500);}};
    @Override public void onDestroy(){main.removeCallbacks(tick);for(StreamResolver.Request request:preparing.values())request.cancel();for(DownloadHelper helper:helpers.values())helper.release();worker.shutdownNow();manager.removeListener(listener);manager.pauseDownloads();super.onDestroy();}
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onTimeout(int startId,int foregroundServiceType){manager.pauseDownloads();stopSelf();}
}
