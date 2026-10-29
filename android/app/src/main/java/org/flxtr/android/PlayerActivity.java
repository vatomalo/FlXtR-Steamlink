package org.flxtr.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import java.util.*;
import org.json.*;
import androidx.media3.datasource.*;
import androidx.media3.exoplayer.source.*;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import androidx.media3.common.*;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.ui.PlayerView;
import java.util.concurrent.*;

/** Resolves catalogue identities in-process, then plays HLS or direct video. */
@androidx.annotation.OptIn(markerClass=androidx.media3.common.util.UnstableApi.class)
public final class PlayerActivity extends Activity {
    private final ExecutorService executor=Executors.newSingleThreadExecutor();
    private final Gamepad gamepad=new Gamepad();
    private CatalogItem playingItem,requestedItem;
    private boolean advancing;
    private SharedPreferences settings;
    private PlayerView view;
    private ExoPlayer player;
    private StreamResolver.Session session;
    private StreamResolver.Request request;
    private Future<?> pending;
    private StreamProtocol.Stream current;
    private LinearLayout loading;
    private TextView message;
    private Button retry,another;
    private SharedPreferences progress;
    private String resumeKey,title;
    private long position;
    private int generation,playbackFailures;
    private boolean playWhenReady=true,foreground,ended,resolving;
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        progress=getSharedPreferences("playback-progress",MODE_PRIVATE);settings=getSharedPreferences("android-settings",MODE_PRIVATE);
        try{if(state!=null&&state.containsKey("requestedItem"))requestedItem=CatalogItem.saved(new JSONObject(state.getString("requestedItem")));else if(getIntent().hasExtra("item"))requestedItem=CatalogItem.saved(new JSONObject(getIntent().getStringExtra("item")));}catch(Exception ignored){}
        title=getIntent().getStringExtra("title");if(title==null)title="FlXtR";
        String identity=getIntent().getStringExtra("resumeKey");if(identity==null)identity=CatalogClient.hash(String.valueOf(getIntent().getStringExtra("url")));
        if(state!=null)title=state.getString("title",title);resumeKey=state!=null?state.getString("resumeKey","position:"+identity):"position:"+identity;
        Object saved=progress.getAll().get(resumeKey);position=state!=null?state.getLong("position",0):saved instanceof Number?((Number)saved).longValue():0;
        playWhenReady=state==null||state.getBoolean("playing",true);
        FrameLayout root=new FrameLayout(this);root.setBackgroundColor(0xff000000);
        view=new PlayerView(this);view.setShowBuffering(PlayerView.SHOW_BUFFERING_ALWAYS);view.setKeepContentOnPlayerReset(false);view.setShowSubtitleButton(true);root.addView(view,new FrameLayout.LayoutParams(-1,-1));
        loading=new LinearLayout(this);loading.setOrientation(LinearLayout.VERTICAL);loading.setGravity(Gravity.CENTER);loading.setPadding(dp(30),dp(70),dp(30),dp(30));loading.setBackgroundColor(0xff0b1115);
        message=new TextView(this);message.setTextColor(0xffe4eee8);message.setTextSize(18);message.setGravity(Gravity.CENTER);loading.addView(message);
        retry=new Button(this);retry.setText("Retry");retry.setVisibility(View.GONE);retry.setOnClickListener(v->{session=null;startResolution(true);});loading.addView(retry);
        root.addView(loading,new FrameLayout.LayoutParams(-1,-1));
        LinearLayout bar=new LinearLayout(this);bar.setGravity(Gravity.CENTER_VERTICAL);bar.setBackgroundColor(0x99000000);
        Button back=new Button(this);back.setText("Back");back.setOnClickListener(v->finish());bar.addView(back);
        TextView name=new TextView(this);name.setText(title);name.setTextColor(0xffffffff);name.setTextSize(16);name.setMaxLines(1);name.setEllipsize(android.text.TextUtils.TruncateAt.END);bar.addView(name,new LinearLayout.LayoutParams(0,dp(48),1));
        another=new Button(this);another.setText("Next server");another.setOnClickListener(v->startResolution(false));bar.addView(another);Button options=new Button(this);options.setText("Options");options.setOnClickListener(v->options());bar.addView(options);
        FrameLayout.LayoutParams top=new FrameLayout.LayoutParams(-1,dp(50),Gravity.TOP);root.addView(bar,top);setContentView(root);
        if(!getIntent().hasExtra("mediaKind"))another.setVisibility(View.GONE);
        message.setText("Preparing playback…");
    }
    @Override protected void onStart(){super.onStart();foreground=true;ExoPlayer background=null;if(current!=null)background=AudioPlaybackService.take();else AudioPlaybackService.stop();if(background!=null){player=background;view.setPlayer(player);loading.setVisibility(View.GONE);}else if(current!=null)prepare(current);else startResolution(false);}
    private CatalogItem item(){if(requestedItem!=null)return requestedItem;return new CatalogItem((getIntent().hasExtra("mediaKind")?getIntent().getStringExtra("mediaKind"):"stream"),getIntent().getIntExtra("mediaId",0),getIntent().getIntExtra("season",0),getIntent().getIntExtra("episode",0),title,"","","");}
    private void cancel(){++generation;if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);resolving=false;advancing=false;}
    private void startResolution(boolean reset){
        if(reset)playbackFailures=0;cancel();release();current=null;retry.setVisibility(View.GONE);loading.setVisibility(View.VISIBLE);message.setText("Finding an available stream…");another.setEnabled(false);
        if(!foreground)return;
        if(!getIntent().hasExtra("mediaKind")){
            String url=getIntent().getStringExtra("url");if(!CatalogItem.validStream(url)){failure("This is not a valid direct HTTPS video URL.");return;}
            StreamProtocol.Stream direct=new StreamProtocol.Stream(url,"");direct.headers.clear();direct.headers.put("User-Agent","FlXtR-Android/0.3");prepare(direct);return;
        }
        if(reset||session==null)session=new StreamResolver.Session(item());
        final StreamResolver.Session job=session;final int token=generation;request=new StreamResolver.Request();final StreamResolver.Request active=request;resolving=true;
        pending=executor.submit(()->{
            try{
                StreamProtocol.Stream result=job.next(active,text->runOnUiThread(()->{if(token==generation&&foreground)message.setText(text);}));
                runOnUiThread(()->{if(token!=generation||!foreground)return;resolving=false;prepare(result);});
            }catch(Exception|LinkageError unavailable){runOnUiThread(()->{if(token!=generation||!foreground)return;resolving=false;failure("No working stream was available. Retry, try another title, or return to the library.");});}
        });
    }
    private void prepare(StreamProtocol.Stream stream){
        if(!foreground)return;release();current=stream;playingItem=stream.item==null?item():stream.item;
        if(playingItem!=null&&!resumeKey.equals("position:"+playingItem.key())&&!playingItem.kind.equals("stream")){resumeKey="position:"+playingItem.key();Object saved=progress.getAll().get(resumeKey);position=saved instanceof Number?((Number)saved).longValue():0;}
        DefaultHttpDataSource.Factory http=new DefaultHttpDataSource.Factory().setUserAgent(stream.headers.getOrDefault("User-Agent","FlXtR-Android/0.3"))
            .setConnectTimeoutMs(10000).setReadTimeoutMs(15000).setDefaultRequestProperties(stream.headers);
        DefaultLoadControl buffer=new DefaultLoadControl.Builder().setBufferDurationsMs(10000,30000,1500,3000).setTargetBufferBytes(24*1024*1024).build();
        DataSource.Factory data=stream.offlineId!=null?OfflineMedia.source(this):new DefaultDataSource.Factory(this,new RangeDataSource.Factory(http));
        DefaultMediaSourceFactory sources=new DefaultMediaSourceFactory(this).setDataSourceFactory(data);
        player=new ExoPlayer.Builder(this).setMediaSourceFactory(sources).setLoadControl(buffer).build();
        player.setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(),true);
        player.setHandleAudioBecomingNoisy(true);
        player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon().setMaxVideoSize(1280,720).setPreferredVideoMimeTypes(MimeTypes.VIDEO_H264).build());
        view.setPlayer(player);loading.setVisibility(View.GONE);another.setEnabled(stream.offlineId==null);
        String language=settings.getString("subtitle-language","en");player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon().setPreferredTextLanguage(language).setTrackTypeDisabled(C.TRACK_TYPE_TEXT,language.isEmpty()).build());
        if(view.getSubtitleView()!=null)view.getSubtitleView().setFractionalTextSize(settings.getFloat("subtitle-size",0.0533f));
        final ExoPlayer active=player;
        player.addListener(new Player.Listener(){
            @Override public void onPlaybackStateChanged(int state){
                if(player!=active||!foreground)return;
                if(state==Player.STATE_READY&&player!=null&&player.getDuration()>0&&position>=player.getDuration()-1500){position=0;player.seekTo(0);}
                if(state==Player.STATE_ENDED){ended=true;progress.edit().remove(resumeKey).apply();MediaHistory.record(PlayerActivity.this,playingItem,0,true);advance();}
            }
            @Override public void onPlayerError(PlaybackException error){
                if(player!=active||!foreground)return;
                if(getIntent().hasExtra("mediaKind")&&stream.offlineId==null&&++playbackFailures<3)startResolution(false);
                else{release();failure("This stream could not be played. Check the link and connection, then retry.");}
            }
        });
        MediaItem.Builder media=new MediaItem.Builder().setUri(stream.url).setMediaMetadata(new MediaMetadata.Builder().setTitle(title).build());
        if(stream.mime!=null)media.setMimeType(stream.mime);
        ArrayList<MediaItem.SubtitleConfiguration> captions=new ArrayList<>();for(MediaCaptions.Track track:stream.subtitles)captions.add(new MediaItem.SubtitleConfiguration.Builder(Uri.parse(track.url)).setMimeType(track.mime).setLanguage(track.language).setLabel(track.label).build());media.setSubtitleConfigurations(captions);
        try{
            MediaItem primary=stream.offlineId==null?media.build():OfflineMedia.item(this,stream.offlineId);
            if(stream.audioUrl!=null){MediaItem audio=stream.offlineId==null?MediaItem.fromUri(stream.audioUrl):OfflineMedia.item(this,stream.offlineId+"-audio");player.setMediaSource(new MergingMediaSource(sources.createMediaSource(primary),sources.createMediaSource(audio)));}else player.setMediaItem(primary);
            if(position>0)player.seekTo(position);player.prepare();player.setPlayWhenReady(playWhenReady);
        }catch(Exception unavailable){release();failure("This download could not be opened. Remove it and download again.");}
    }
    private void advance(){advance(false);}
    private void advance(boolean manual){
        if(advancing)return;CatalogItem station=item();boolean continuous=station.kind.equals("tv-channel")||station.kind.equals("mtv-channel");if(!manual&&!continuous&&!settings.getBoolean("autoplay",true)){finish();return;}
        advancing=true;final int token=generation;StreamResolver.Request nextRequest=new StreamResolver.Request();request=nextRequest;
        executor.submit(()->{try{CatalogItem nextItem=continuous?station:queuedNext();if(nextItem==null)nextItem=MediaSources.next(this,playingItem,nextRequest);final CatalogItem selectedNext=nextItem;runOnUiThread(()->{if(!foreground||token!=generation)return;advancing=false;if(selectedNext==null){finish();return;}release();requestedItem=selectedNext;title=selectedNext.title;resumeKey="position:"+selectedNext.key();position=0;ended=false;current=null;session=null;startResolution(true);});}catch(Exception failed){runOnUiThread(()->{advancing=false;if(foreground)failure("Could not open the next episode. Return to the library or retry.");});}});
    }
    private CatalogItem queuedNext(){try{JSONArray queue=new JSONArray(getIntent().getStringExtra("queue"));for(int i=0;i<queue.length()-1;i++)if(CatalogItem.saved(queue.getJSONObject(i)).key().equals(playingItem.key()))return CatalogItem.saved(queue.getJSONObject(i+1));}catch(Exception ignored){}return null;}
    private void options(){String[] labels={"Play / pause","Next episode / video","Speed","Picture size","Subtitles","Download this title"};AlertDialog d=new AlertDialog.Builder(this).setTitle("Playback").setItems(labels,(dialog,index)->{if(player==null)return;switch(index){case 0:player.setPlayWhenReady(!player.getPlayWhenReady());break;case 1:advance(true);break;case 2:AlertDialog speed=new AlertDialog.Builder(this).setTitle("Speed").setItems(new String[]{"0.75×","1×","1.25×","1.5×","2×"},(v,n)->{if(player!=null)player.setPlaybackSpeed(new float[]{0.75f,1,1.25f,1.5f,2}[n]);}).show();Gamepad.dialog(speed);break;case 3:view.setResizeMode((view.getResizeMode()+1)%5);break;case 4:view.showController();View subtitle=view.findViewById(androidx.media3.ui.R.id.exo_subtitle);if(subtitle!=null)subtitle.performClick();break;case 5:if(playingItem!=null&&!playingItem.kind.equals("local"))OfflineMedia.queue(this,playingItem);break;}}).setNegativeButton("Close",null).show();Gamepad.dialog(d);}
    private final Gamepad.Actions controls=new Gamepad.Actions(){public boolean button(int code){switch(code){case KeyEvent.KEYCODE_BUTTON_A:View focused=getCurrentFocus();if(view.isControllerFullyVisible()&&focused!=null&&focused.isClickable()){Gamepad.send(focused,KeyEvent.KEYCODE_DPAD_CENTER);return true;}case KeyEvent.KEYCODE_BUTTON_START:if(player!=null)player.setPlayWhenReady(!player.getPlayWhenReady());return true;case KeyEvent.KEYCODE_BUTTON_B:finish();return true;case KeyEvent.KEYCODE_BUTTON_X:view.showController();return true;case KeyEvent.KEYCODE_BUTTON_Y:options();return true;case KeyEvent.KEYCODE_BUTTON_L1:if(player!=null)player.seekBack();return true;case KeyEvent.KEYCODE_BUTTON_R1:if(player!=null)player.seekForward();return true;default:return false;}}public void direction(int code){view.showController();Gamepad.send(view,code);}};
    @Override public boolean dispatchKeyEvent(KeyEvent event){return gamepad.key(event,controls)||super.dispatchKeyEvent(event);}
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event){return gamepad.motion(event,controls)||super.dispatchGenericMotionEvent(event);}

    private void failure(String text){loading.setVisibility(View.VISIBLE);message.setText(text);retry.setVisibility(View.VISIBLE);another.setEnabled(false);}
    private void snapshot(){if(player!=null){long now=player.getCurrentPosition();if(now>0)position=now;playWhenReady=player.getPlayWhenReady();}if(!ended){progress.edit().putLong(resumeKey,position).apply();if(position>1000)MediaHistory.record(this,playingItem,position,false);}}
    private void release(){if(player!=null){snapshot();view.setPlayer(null);player.release();player=null;}}
    @Override protected void onStop(){gamepad.reset();foreground=false;cancel();if(player!=null&&playingItem!=null&&(playingItem.kind.equals("music")||playingItem.kind.equals("music-query"))&&player.getPlayWhenReady()&&!ended){snapshot();view.setPlayer(null);AudioPlaybackService.handoff(this,player);player=null;}else release();super.onStop();}
    @Override protected void onSaveInstanceState(Bundle state){snapshot();try{if(requestedItem!=null)state.putString("requestedItem",requestedItem.json().toString());}catch(Exception ignored){}state.putString("title",title);state.putString("resumeKey",resumeKey);state.putLong("position",position);state.putBoolean("playing",playWhenReady);super.onSaveInstanceState(state);}
    @Override protected void onDestroy(){cancel();release();executor.shutdownNow();super.onDestroy();}
}
