package org.flxtr.android;

import android.app.*;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import android.media.*;
import androidx.core.view.*;
import java.io.*;
import java.nio.file.Files;
import java.util.concurrent.ConcurrentLinkedQueue;

/** In-process Libretro frontend: bundled Gambatte, stereo audio and joypad input. */
public final class RetroActivity extends Activity implements SurfaceHolder.Callback {
    static {System.loadLibrary("flxtr_retro");}
    static native double[] startCore(String path,String saves,Surface surface);
    static native short[] frame(int mask);
    static native byte[] readMemory(boolean state);
    static native boolean writeMemory(byte[] data,boolean state);
    static native void resetCore();
    static native void stopCore();
    private volatile boolean running,paused;
    private volatile int touch,keys,stick;
    volatile long framesPlayed,samplesPlayed;
    private boolean resumed,ready;
    private Thread thread;
    private volatile AudioTrack audio;
    private SurfaceView surface;
    private FrameLayout root;
    private final ConcurrentLinkedQueue<Runnable> commands=new ConcurrentLinkedQueue<>();
    private File rom,save;
    private double aspect=160.0/144;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
        rom=new File(getIntent().getStringExtra("rom"));File folder=new File(getFilesDir(),"games");save=new File(folder,rom.getName());
        try{if(!rom.getCanonicalFile().getParentFile().equals(folder.getCanonicalFile())||!rom.isFile())throw new IOException();}catch(Exception invalid){finish();return;}
        root=new FrameLayout(this);root.setBackgroundColor(0xff000000);surface=new SurfaceView(this);root.addView(surface,new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));surface.getHolder().addCallback(this);root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->fit());
        LinearLayout dpad=new LinearLayout(this);dpad.setOrientation(LinearLayout.VERTICAL);dpad.addView(row(control("↑",4)));dpad.addView(row(control("←",6),control("↓",5),control("→",7)));root.addView(dpad,new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.LEFT));
        LinearLayout buttons=new LinearLayout(this);buttons.setOrientation(LinearLayout.VERTICAL);buttons.addView(row(control("B",0),control("A",8)));buttons.addView(row(control("Select",2),control("Start",3)));root.addView(buttons,new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.RIGHT));
        Button menu=new Button(this);menu.setText("⋮");menu.setContentDescription("Game menu");menu.setAlpha(.75f);menu.setOnClickListener(v->menu());root.addView(menu,new FrameLayout.LayoutParams(dp(56),dp(48),Gravity.TOP|Gravity.RIGHT));setContentView(root);hideBars();
    }
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private LinearLayout row(View... views){LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER);for(View v:views)row.addView(v,new LinearLayout.LayoutParams(dp(64),dp(56)));return row;}
    private Button control(String text,int bit){Button b=new Button(this);b.setText(text);b.setTextSize(12);b.setAllCaps(false);b.setAlpha(.6f);b.setOnTouchListener((v,e)->{int action=e.getActionMasked();if(action==MotionEvent.ACTION_DOWN){touch|=1<<bit;return true;}if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){touch&=~(1<<bit);v.performClick();return true;}return true;});return b;}
    private void fit(){if(root==null)return;int w=root.getWidth(),h=root.getHeight();if(w<=0||h<=0)return;int width=(int)Math.min(w,h*aspect),height=(int)(width/aspect);FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)surface.getLayoutParams();if(p.width!=width||p.height!=height){p.width=width;p.height=height;p.gravity=Gravity.CENTER;surface.setLayoutParams(p);}}
    private void hideBars(){WindowInsetsControllerCompat c=WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());c.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);c.hide(WindowInsetsCompat.Type.systemBars());}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)hideBars();}
    @Override protected void onResume(){super.onResume();resumed=true;start();}
    @Override protected void onPause(){resumed=false;stop();super.onPause();}
    @Override public void surfaceCreated(SurfaceHolder holder){ready=true;start();}
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    @Override public void surfaceDestroyed(SurfaceHolder holder){ready=false;stop();}
    private synchronized void start(){if(!resumed||!ready||running||thread!=null&&thread.isAlive())return;running=true;paused=false;touch=keys=stick=0;commands.clear();thread=new Thread(this::run,"FlXtR-Libretro");thread.start();}
    private void run(){
        boolean started=false;try{
            double[] av=startCore(rom.getAbsolutePath(),save.getParent(),surface.getHolder().getSurface());if(av==null)throw new IOException("This game couldn't be loaded. Choose an uncompressed .gb or .gbc ROM.");started=true;aspect=av[2];runOnUiThread(this::fit);
            restore(".srm",false);restore(".auto",true);
            int rate=(int)Math.round(av[1]);if(rate<8000||rate>192000)throw new IOException("Unsupported game audio");int buffer=Math.max(AudioTrack.getMinBufferSize(rate,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT),rate/5*4);
            audio=new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()).setAudioFormat(new AudioFormat.Builder().setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build()).setBufferSizeInBytes(buffer).setTransferMode(AudioTrack.MODE_STREAM).build();if(audio.getState()!=AudioTrack.STATE_INITIALIZED)throw new IOException("Game audio is unavailable");audio.play();
            long period=(long)(1_000_000_000/Math.max(1,Math.min(120,av[0]))),next=System.nanoTime();boolean wasPaused=false;
            while(running){Runnable command;while((command=commands.poll())!=null)command.run();
                if(paused){if(!wasPaused){audio.pause();audio.flush();wasPaused=true;}Thread.sleep(20);next=System.nanoTime();continue;}
                if(wasPaused){audio.play();wasPaused=false;}
                short[] samples=frame(touch|keys|stick);framesPlayed++;if(samples!=null&&samples.length>0&&running){samplesPlayed+=samples.length/2;if(audio.write(samples,0,samples.length,AudioTrack.WRITE_BLOCKING)<0&&running)throw new IOException("Game audio stopped");}
                next+=period;long wait=next-System.nanoTime();if(wait>0)Thread.sleep(wait/1000000,(int)(wait%1000000));else if(wait<-period*3)next=System.nanoTime();
            }
        }catch(InterruptedException stopped){Thread.currentThread().interrupt();}catch(Exception failure){runOnUiThread(()->{if(!isFinishing())new AlertDialog.Builder(this).setTitle("Libretro").setMessage(failure.getMessage()==null?"Game playback stopped.":failure.getMessage()).setPositiveButton("Close",(d,i)->finish()).setOnCancelListener(d->finish()).show();});}
        finally{if(started){persist(".srm",false);persist(".auto",true);stopCore();}AudioTrack track=audio;audio=null;if(track!=null){track.pause();track.flush();track.release();}running=false;}
    }
    private boolean persist(String suffix,boolean state){try{byte[] data=readMemory(state);if(data==null)return !state;android.util.AtomicFile file=new android.util.AtomicFile(new File(save+suffix));FileOutputStream out=file.startWrite();try{out.write(data);file.finishWrite(out);}catch(Exception failure){file.failWrite(out);return false;}return true;}catch(Exception ignored){return false;}}
    private boolean restore(String suffix,boolean state){try{File file=new File(save+suffix);return file.isFile()&&file.length()<=8*1024*1024&&writeMemory(Files.readAllBytes(file.toPath()),state);}catch(Exception failure){return false;}}
    private synchronized void stop(){running=false;touch=keys=stick=0;AudioTrack track=audio;if(track!=null)try{track.pause();track.flush();}catch(Exception ignored){}if(thread!=null){thread.interrupt();try{thread.join(2000);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}}}
    @Override public void onBackPressed(){menu();}
    private void menu(){if(!running){finish();return;}paused=true;touch=keys=stick=0;AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Libretro · Gambatte").setItems(new String[]{"Resume","Save state","Load state","Reset game","Exit to library"},(d,i)->{if(i==1)commands.add(()->{toast(persist(".state",true)?"State saved":"Could not save state");});if(i==2)commands.add(()->toast(restore(".state",true)?"State loaded":"No saved state"));if(i==3)commands.add(RetroActivity::resetCore);if(i==4)finish();}).setNegativeButton("Resume",null).create();dialog.setOnDismissListener(d->{paused=false;hideBars();});dialog.show();Gamepad.dialog(dialog);}
    private void toast(String text){runOnUiThread(()->Toast.makeText(this,text,Toast.LENGTH_SHORT).show());}
    private int bit(int code){switch(code){case KeyEvent.KEYCODE_BUTTON_A:return 8;case KeyEvent.KEYCODE_BUTTON_B:return 0;case KeyEvent.KEYCODE_BUTTON_X:return 9;case KeyEvent.KEYCODE_BUTTON_Y:return 1;case KeyEvent.KEYCODE_BUTTON_SELECT:return 2;case KeyEvent.KEYCODE_BUTTON_START:return 3;case KeyEvent.KEYCODE_DPAD_UP:return 4;case KeyEvent.KEYCODE_DPAD_DOWN:return 5;case KeyEvent.KEYCODE_DPAD_LEFT:return 6;case KeyEvent.KEYCODE_DPAD_RIGHT:return 7;case KeyEvent.KEYCODE_BUTTON_L1:return 10;case KeyEvent.KEYCODE_BUTTON_R1:return 11;default:return -1;}}
    @Override public boolean dispatchKeyEvent(KeyEvent e){if(e.getKeyCode()==KeyEvent.KEYCODE_BUTTON_THUMBR||e.getKeyCode()==KeyEvent.KEYCODE_MENU){if(e.getAction()==KeyEvent.ACTION_DOWN&&e.getRepeatCount()==0)menu();return true;}int bit=bit(e.getKeyCode());if(bit>=0){if(e.getAction()==KeyEvent.ACTION_DOWN)keys|=1<<bit;else if(e.getAction()==KeyEvent.ACTION_UP)keys&=~(1<<bit);return true;}return super.dispatchKeyEvent(e);}
    @Override public boolean onGenericMotionEvent(MotionEvent e){if((e.getSource()&InputDevice.SOURCE_JOYSTICK)!=0){float x=e.getAxisValue(MotionEvent.AXIS_X),y=e.getAxisValue(MotionEvent.AXIS_Y),hatX=e.getAxisValue(MotionEvent.AXIS_HAT_X),hatY=e.getAxisValue(MotionEvent.AXIS_HAT_Y);if(Math.abs(hatX)>.5)x=hatX;if(Math.abs(hatY)>.5)y=hatY;stick=(x<-.35?1<<6:0)|(x>.35?1<<7:0)|(y<-.35?1<<4:0)|(y>.35?1<<5:0);return true;}return super.onGenericMotionEvent(e);}
}
