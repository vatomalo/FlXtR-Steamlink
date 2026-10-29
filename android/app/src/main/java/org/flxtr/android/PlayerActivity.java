package org.flxtr.android;

import android.app.Activity;
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
    private int generation;
    private boolean playWhenReady=true,foreground,ended,resolving;
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        progress=getSharedPreferences("playback-progress",MODE_PRIVATE);
        title=getIntent().getStringExtra("title");if(title==null)title="FlXtR";
        String identity=getIntent().getStringExtra("resumeKey");if(identity==null)identity=CatalogClient.hash(String.valueOf(getIntent().getStringExtra("url")));
        resumeKey="position:"+identity;
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
        another=new Button(this);another.setText("Next server");another.setOnClickListener(v->startResolution(false));bar.addView(another);
        FrameLayout.LayoutParams top=new FrameLayout.LayoutParams(-1,dp(50),Gravity.TOP);root.addView(bar,top);setContentView(root);
        if(!getIntent().hasExtra("mediaKind"))another.setVisibility(View.GONE);
        message.setText("Preparing playback…");
    }
    @Override protected void onStart(){super.onStart();foreground=true;if(current!=null)prepare(current);else startResolution(false);}
    private CatalogItem item(){return new CatalogItem(getIntent().getStringExtra("mediaKind"),getIntent().getIntExtra("mediaId",0),getIntent().getIntExtra("season",0),getIntent().getIntExtra("episode",0),title,"","","");}
    private void cancel(){++generation;if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);resolving=false;}
    private void startResolution(boolean reset){
        cancel();release();current=null;retry.setVisibility(View.GONE);loading.setVisibility(View.VISIBLE);message.setText("Finding an available stream…");another.setEnabled(false);
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
        if(!foreground)return;release();current=stream;
        DefaultHttpDataSource.Factory http=new DefaultHttpDataSource.Factory().setUserAgent(stream.headers.getOrDefault("User-Agent","FlXtR-Android/0.3"))
            .setConnectTimeoutMs(10000).setReadTimeoutMs(15000).setDefaultRequestProperties(stream.headers);
        DefaultLoadControl buffer=new DefaultLoadControl.Builder().setBufferDurationsMs(10000,30000,1500,3000).setTargetBufferBytes(24*1024*1024).build();
        player=new ExoPlayer.Builder(this).setMediaSourceFactory(new DefaultMediaSourceFactory(this).setDataSourceFactory(http)).setLoadControl(buffer).build();
        player.setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(),true);
        player.setHandleAudioBecomingNoisy(true);
        player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon().setMaxVideoSize(1280,720).setPreferredVideoMimeTypes(MimeTypes.VIDEO_H264).build());
        view.setPlayer(player);loading.setVisibility(View.GONE);another.setEnabled(true);
        final ExoPlayer active=player;
        player.addListener(new Player.Listener(){
            @Override public void onPlaybackStateChanged(int state){
                if(player!=active||!foreground)return;
                if(state==Player.STATE_READY&&player!=null&&player.getDuration()>0&&position>=player.getDuration()-1500){position=0;player.seekTo(0);}
                if(state==Player.STATE_ENDED){ended=true;progress.edit().remove(resumeKey).apply();finish();}
            }
            @Override public void onPlayerError(PlaybackException error){
                if(player!=active||!foreground)return;
                if(getIntent().hasExtra("mediaKind"))startResolution(false);
                else{release();failure("This stream could not be played. Check the link and connection, then retry.");}
            }
        });
        MediaItem.Builder media=new MediaItem.Builder().setUri(stream.url).setMediaMetadata(new MediaMetadata.Builder().setTitle(title).build());
        if(stream.mime!=null)media.setMimeType(stream.mime);
        player.setMediaItem(media.build());if(position>0)player.seekTo(position);player.prepare();player.setPlayWhenReady(playWhenReady);
    }
    private void failure(String text){loading.setVisibility(View.VISIBLE);message.setText(text);retry.setVisibility(View.VISIBLE);another.setEnabled(false);}
    private void snapshot(){if(player!=null){long now=player.getCurrentPosition();if(now>0)position=now;playWhenReady=player.getPlayWhenReady();}if(!ended)progress.edit().putLong(resumeKey,position).apply();}
    private void release(){if(player!=null){snapshot();view.setPlayer(null);player.release();player=null;}}
    @Override protected void onStop(){foreground=false;cancel();release();super.onStop();}
    @Override protected void onSaveInstanceState(Bundle state){snapshot();state.putLong("position",position);state.putBoolean("playing",playWhenReady);super.onSaveInstanceState(state);}
    @Override protected void onDestroy(){cancel();release();executor.shutdownNow();super.onDestroy();}
}
