package org.flxtr.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.net.Uri;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.MediaController;
import android.widget.VideoView;

/** First platform playback backend. Android decodes direct MP4/HLS media. */
public final class PlayerActivity extends Activity {
    private VideoView video;
    private int position;
    private boolean resumePlaying=true;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        video=new VideoView(this);
        FrameLayout root=new FrameLayout(this); root.setBackgroundColor(0xFF000000);
        FrameLayout.LayoutParams layout=new FrameLayout.LayoutParams(-1,-1,android.view.Gravity.CENTER);
        root.addView(video,layout); setContentView(root);
        MediaController controller=new MediaController(this); controller.setAnchorView(video);
        video.setMediaController(controller);
        position=state==null?0:state.getInt("position",0);
        resumePlaying=state==null||state.getBoolean("playing",true);
        video.setOnPreparedListener(player->{ if(position>0)video.seekTo(position); if(resumePlaying)video.start(); });
        video.setOnCompletionListener(player->finish());
        video.setOnErrorListener((player,what,extra)->{
            new AlertDialog.Builder(this).setTitle("Playback failed")
                .setMessage("Android could not play this stream. Check the connection and media format. Error "+what+" / "+extra)
                .setPositiveButton("Back",(dialog,which)->finish()).setOnCancelListener(dialog->finish()).show();return true;
        });
        String url=getIntent().getStringExtra("url");
        if(url==null||!"https".equalsIgnoreCase(Uri.parse(url).getScheme())) { finish();return; }
        video.setVideoURI(Uri.parse(url)); video.requestFocus();
    }
    @Override protected void onPause(){position=video.getCurrentPosition();resumePlaying=video.isPlaying();video.pause();super.onPause();}
    @Override protected void onResume(){super.onResume();if(video!=null&&resumePlaying)video.start();}
    @Override protected void onSaveInstanceState(Bundle state){state.putInt("position",position);state.putBoolean("playing",resumePlaying);super.onSaveInstanceState(state);}
    @Override protected void onDestroy(){video.stopPlayback();super.onDestroy();}
}
