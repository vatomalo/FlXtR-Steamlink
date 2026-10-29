package org.flxtr.android;
import android.content.*;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.session.*;
/** Owns music while the library is visible; MediaSession supplies system controls. */
@androidx.annotation.OptIn(markerClass=androidx.media3.common.util.UnstableApi.class)
public final class AudioPlaybackService extends MediaSessionService {
    private static ExoPlayer pending;private static AudioPlaybackService running;private MediaSession session;
    static void handoff(Context context,ExoPlayer player){stop();pending=player;androidx.core.content.ContextCompat.startForegroundService(context,new Intent(context,AudioPlaybackService.class));}
    static ExoPlayer take(){ExoPlayer player=pending;pending=null;if(running!=null){if(running.session!=null){player=(ExoPlayer)running.session.getPlayer();running.session.release();running.session=null;}running.stopSelf();}return player;}
    static void stop(){ExoPlayer player=take();if(player!=null)player.release();}
    @Override public void onCreate(){super.onCreate();running=this;if(pending!=null){session=new MediaSession.Builder(this,pending).build();pending=null;}}
    @Override public int onStartCommand(Intent intent,int flags,int startId){if(session==null&&pending!=null){session=new MediaSession.Builder(this,pending).build();pending=null;}return super.onStartCommand(intent,flags,startId);}
    @Override public MediaSession onGetSession(MediaSession.ControllerInfo info){return session;}
    @Override public void onDestroy(){if(session!=null){session.getPlayer().release();session.release();session=null;}if(running==this)running=null;super.onDestroy();}
}
