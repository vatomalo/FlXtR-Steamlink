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

/** In-process Libretro frontend with per-system cores, saves and disc switching. */
public final class RetroActivity extends Activity implements SurfaceHolder.Callback {
    static {System.loadLibrary("flxtr_retro");}
    static native double[] startCore(String path,String saves,String system,String library,Surface surface);
    static native int diskCount();
    static native int diskIndex();
    static native boolean swapDisk(int index);
    static native String coreInfo(String library);
    static native short[] frame(int mask,short leftX,short leftY,short rightX,short rightY,short deltaX,short deltaY,short absoluteX,short absoluteY,boolean pressed);
    static native int renderedFrames();
    static native String handoffMetadata();
    static native void sendKey(boolean down,int key,int character,int modifiers);
    private static native byte[] messageBytes();
    static String lastMessage(){byte[] message=messageBytes();return message==null?"":new String(message,java.nio.charset.StandardCharsets.UTF_8);}
    static native byte[] readMemory(boolean state);
    static native boolean writeMemory(byte[] data,boolean state);
    static native void resetCore();
    static native void stopCore();
    private volatile boolean running,paused;
    private volatile int touch,keys,stick;
    private volatile short leftX,leftY,rightX,rightY;
    private final java.util.concurrent.atomic.AtomicInteger mouseX=new java.util.concurrent.atomic.AtomicInteger(),mouseY=new java.util.concurrent.atomic.AtomicInteger();
    private volatile short pointerX,pointerY;
    private volatile boolean pointerPressed;
    private float lastPointerX,lastPointerY;
    private boolean pointer(View view,MotionEvent event){int action=event.getActionMasked();float x=event.getX(),y=event.getY();pointerX=(short)Math.round(Math.max(-1f,Math.min(1f,x/Math.max(1,view.getWidth())*2-1))*32767);pointerY=(short)Math.round(Math.max(-1f,Math.min(1f,y/Math.max(1,view.getHeight())*2-1))*32767);if(action==MotionEvent.ACTION_DOWN){pointerPressed=true;lastPointerX=x;lastPointerY=y;}else if(action==MotionEvent.ACTION_MOVE){mouseX.addAndGet(Math.round((x-lastPointerX)*baseWidth/Math.max(1,view.getWidth())));mouseY.addAndGet(Math.round((y-lastPointerY)*baseHeight/Math.max(1,view.getHeight())));lastPointerX=x;lastPointerY=y;}else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL)pointerPressed=false;return true;}
    private void clearAnalog(){leftX=leftY=rightX=rightY=0;pointerPressed=false;mouseX.set(0);mouseY.set(0);}
    private static short analog(float value){return (short)Math.round(Math.max(-1f,Math.min(1f,Math.abs(value)<0.15f?0:value))*32767);}
    volatile long framesPlayed,samplesPlayed;
    private boolean resumed,ready;
    private Thread thread;
    private volatile AudioTrack audio;
    private SurfaceView surface;
    private FrameLayout root;
    private static final Object CORE_LOCK=new Object();
    private TextView loadingText;
    private View touchDpad,touchButtons;
    private boolean touchControls;
    private volatile boolean handingOff;
    private final ConcurrentLinkedQueue<Runnable> commands=new ConcurrentLinkedQueue<>();
    private File rom,save;
    private String coreName;
    private int scaling,baseWidth=160,baseHeight=144;
    private double aspect=160.0/144;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);WindowCompat.setDecorFitsSystemWindows(getWindow(),false);
        String path=getIntent().getStringExtra("rom");if(path==null){finish();return;}rom=new File(path);File folder=new File(getFilesDir(),"games");
        coreName=GameFormats.core(rom.getName());
        String requestedCore=getIntent().getStringExtra("core");if(GameFormats.compatible(rom.getName(),requestedCore))coreName=requestedCore;
        if("scummvm".equals(requestedCore)&&getIntent().getBooleanExtra("directoryGame",false))coreName="scummvm";
String optionsKey=getIntent().getStringExtra("optionsKey");if(optionsKey!=null&&optionsKey.matches("[0-9a-f]{64}")){android.content.SharedPreferences options=getSharedPreferences("retro-options",MODE_PRIVATE);String chosen=options.getString("core:"+optionsKey,"");if(GameFormats.compatible(rom.getName(),chosen))coreName=chosen;scaling=Math.max(0,Math.min(3,options.getInt("scale:"+optionsKey,0)));}
        try{if(!rom.getCanonicalPath().startsWith(folder.getCanonicalPath()+File.separator)||!rom.isFile()||coreName.isEmpty())throw new IOException();String key=getIntent().getStringExtra("saveKey");if(key!=null&&!key.isEmpty()){if(!key.matches("[0-9a-f]{64}"))throw new IOException();File saves=new File(folder,"saves");if(!saves.isDirectory()&&!saves.mkdirs())throw new IOException();save=new File(saves,key);}else save=new File(folder,rom.getName());File system=new File(folder,"system");if(!system.isDirectory()&&!system.mkdirs())throw new IOException();}catch(Exception invalid){finish();return;}
        root=new FrameLayout(this);root.setBackgroundColor(0xff000000);surface=new SurfaceView(this);surface.setOnTouchListener(this::pointer);root.addView(surface,new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));loadingText=new TextView(this);loadingText.setText("Preparing "+GameFormats.label(coreName)+"...\nFirst launch may download engine resources.");loadingText.setTextColor(0xffffffff);loadingText.setTextSize(14);loadingText.setGravity(Gravity.CENTER);root.addView(loadingText,new FrameLayout.LayoutParams(-1,-1));surface.getHolder().addCallback(this);root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->fit());
        LinearLayout dpad=new LinearLayout(this);dpad.setOrientation(LinearLayout.VERTICAL);dpad.addView(row(control("↑",4)));dpad.addView(row(control("←",6),control("↓",5),control("→",7)));root.addView(dpad,new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.LEFT));
        LinearLayout buttons=new LinearLayout(this);buttons.setOrientation(LinearLayout.VERTICAL);
        if(coreName.equals("pcsx_rearmed")||coreName.equals("ppsspp")){buttons.addView(row(control("L2",12),control("R2",13)));buttons.addView(row(control("□",1),control("△",9)));buttons.addView(row(control("×",0),control("○",8)));}
        else{if(coreName.equals("snes9x")||coreName.equals("som_ai"))buttons.addView(row(control("Y",1),control("X",9)));buttons.addView(row(control("B",0),control("A",8)));}
        if(!coreName.equals("gambatte")&&!coreName.equals("fceumm"))buttons.addView(row(control("L",10),control("R",11)));
        buttons.addView(row(control("Select",2),control("Start",3)));root.addView(buttons,new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.RIGHT));
Button menu=new Button(this);menu.setText("⋮");menu.setContentDescription("Game menu");menu.setAlpha(.75f);menu.setOnClickListener(v->menu());root.addView(menu,new FrameLayout.LayoutParams(dp(56),dp(48),Gravity.TOP|Gravity.RIGHT));touchDpad=dpad;touchButtons=buttons;setTouchControls(getSharedPreferences("retro-ui",MODE_PRIVATE).getBoolean("touch-controls",false));setContentView(root);hideBars();
    }
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private void setTouchControls(boolean enabled){touchControls=enabled;touch=0;touchDpad.setVisibility(enabled?View.VISIBLE:View.GONE);touchButtons.setVisibility(enabled?View.VISIBLE:View.GONE);getSharedPreferences("retro-ui",MODE_PRIVATE).edit().putBoolean("touch-controls",enabled).apply();}
    private LinearLayout row(View... views){LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER);for(View v:views)row.addView(v,new LinearLayout.LayoutParams(dp(64),dp(56)));return row;}
    private Button control(String text,int bit){Button b=new Button(this);b.setText(text);b.setTextSize(12);b.setAllCaps(false);b.setAlpha(.6f);b.setOnTouchListener((v,e)->{int action=e.getActionMasked();if(action==MotionEvent.ACTION_DOWN){touch|=1<<bit;return true;}if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){touch&=~(1<<bit);v.performClick();return true;}return true;});return b;}
    private void fit(){if(root==null)return;int w=root.getWidth(),h=root.getHeight();if(w<=0||h<=0)return;int width,height;if(scaling==3){width=w;height=h;}else if(scaling==1&&Math.min(w/baseWidth,h/baseHeight)>=1){int factor=Math.min(w/baseWidth,h/baseHeight);width=baseWidth*factor;height=baseHeight*factor;}else{width=(int)(scaling==2?Math.max(w,h*aspect):Math.min(w,h*aspect));height=(int)(width/aspect);}FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)surface.getLayoutParams();if(p.width!=width||p.height!=height){p.width=width;p.height=height;p.gravity=Gravity.CENTER;surface.setLayoutParams(p);}}
    private void hideBars(){WindowInsetsControllerCompat c=WindowCompat.getInsetsController(getWindow(),getWindow().getDecorView());c.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);c.hide(WindowInsetsCompat.Type.systemBars());}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)hideBars();}
    @Override protected void onResume(){super.onResume();resumed=true;start();}
    @Override protected void onPause(){resumed=false;stop();super.onPause();}
    @Override public void surfaceCreated(SurfaceHolder holder){ready=true;start();}
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    @Override public void surfaceDestroyed(SurfaceHolder holder){ready=false;stop();}
    private synchronized void start(){if(!resumed||!ready||running||thread!=null&&thread.isAlive())return;running=true;paused=false;touch=keys=stick=0;clearAnalog();commands.clear();thread=new Thread(this::run,"FlXtR-Libretro");thread.start();}
    private void run(){synchronized(CORE_LOCK){runCore();}}
    private void runCore(){
        boolean started=false;try{
            CoreAssets.install(this,coreName);
            if(coreName.equals("som_ai")){
                // The experimental proxy exchanges data only inside app-private storage.
                File aiDir=new File(getFilesDir(),"som-ai");
                if(!aiDir.isDirectory()&&!aiDir.mkdirs())
                    throw new IOException("Cannot prepare SoM AI bridge directory");
            }
            runOnUiThread(()->loadingText.setText("Starting "+GameFormats.label(coreName)+"..."));
            File coreSaves=new File(getFilesDir(),"games/core-saves/"+coreName);if(!coreSaves.isDirectory()&&!coreSaves.mkdirs())throw new IOException("Cannot create engine save directory");
            String library=coreName.equals("gambatte")?"":new File(getApplicationInfo().nativeLibraryDir,"libflxtr_"+coreName+".so").getAbsolutePath();
double[] av=startCore(coreName.equals("scummvm")&&getIntent().getBooleanExtra("coreOnly",false)?"":rom.getAbsolutePath(),coreName.equals("ppsspp")||coreName.equals("scummvm")||coreName.equals("dolphin")?coreSaves.getAbsolutePath():save.getParent(),new File(getFilesDir(),"games/system").getAbsolutePath(),library,surface.getHolder().getSurface());if(av==null)throw new IOException("This "+GameFormats.label(coreName)+" game couldn't be loaded. Check the game files and, for PlayStation, its BIOS and disc tracks.");started=true;aspect=av[2];if(av.length>=5){baseWidth=Math.max(1,(int)av[3]);baseHeight=Math.max(1,(int)av[4]);}runOnUiThread(this::fit);
            restore(".srm",false);if(!coreName.equals("scummvm"))restore(".auto",true);
            int rate=(int)Math.round(av[1]);if(rate<8000||rate>192000)throw new IOException("Unsupported game audio");int buffer=Math.max(AudioTrack.getMinBufferSize(rate,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT),rate/5*4);
            audio=new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()).setAudioFormat(new AudioFormat.Builder().setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build()).setBufferSizeInBytes(buffer).setTransferMode(AudioTrack.MODE_STREAM).build();if(audio.getState()!=AudioTrack.STATE_INITIALIZED)throw new IOException("Game audio is unavailable");audio.play();
            long period=(long)(1_000_000_000/Math.max(1,Math.min(120,av[0]))),next=System.nanoTime();boolean wasPaused=false;
            boolean displayed=false;while(running){Runnable command;while((command=commands.poll())!=null)command.run();
                if(paused||handingOff){if(!wasPaused){audio.pause();audio.flush();wasPaused=true;}Thread.sleep(20);next=System.nanoTime();continue;}
                if(wasPaused){audio.play();wasPaused=false;}
                short[] samples=frame(touch|keys|stick,leftX,leftY,rightX,rightY,(short)Math.max(-32767,Math.min(32767,mouseX.getAndSet(0))),(short)Math.max(-32767,Math.min(32767,mouseY.getAndSet(0))),pointerX,pointerY,pointerPressed);framesPlayed++;if(!displayed&&renderedFrames()>0){displayed=true;runOnUiThread(()->loadingText.setVisibility(View.GONE));}if(samples!=null&&samples.length>0&&running){samplesPlayed+=samples.length/2;if(audio.write(samples,0,samples.length,AudioTrack.WRITE_BLOCKING)<0&&running)throw new IOException("Game audio stopped");}
                next+=period;long wait=next-System.nanoTime();if(wait>0)Thread.sleep(wait/1000000,(int)(wait%1000000));else if(wait<-period*3)next=System.nanoTime();
            }
        }catch(InterruptedException stopped){Thread.currentThread().interrupt();}catch(Exception failure){runOnUiThread(()->{if(!isFinishing()&&!isDestroyed()&&resumed)new AlertDialog.Builder(this).setTitle("Libretro").setMessage(failure.getMessage()==null?"Game playback stopped.":failure.getMessage()).setPositiveButton("Close",(d,i)->finish()).setOnCancelListener(d->finish()).show();});}
        finally{if(started){persist(".srm",false);if(!coreName.equals("scummvm"))persist(".auto",true);stopCore();}AudioTrack track=audio;audio=null;if(track!=null){track.pause();track.flush();track.release();}running=false;}
    }
    private boolean persist(String suffix,boolean state){try{byte[] data=readMemory(state);if(data==null)return !state;android.util.AtomicFile file=new android.util.AtomicFile(new File(save+suffix));FileOutputStream out=file.startWrite();try{out.write(data);file.finishWrite(out);}catch(Exception failure){file.failWrite(out);return false;}return true;}catch(Exception ignored){return false;}catch(OutOfMemoryError memory){return false;}}
    private boolean restore(String suffix,boolean state){try{File file=new File(save+suffix);return file.isFile()&&file.length()<=128*1024*1024&&writeMemory(Files.readAllBytes(file.toPath()),state);}catch(Exception failure){return false;}catch(OutOfMemoryError memory){return false;}}
    private synchronized void stop(){running=false;touch=keys=stick=0;clearAnalog();AudioTrack track=audio;if(track!=null)try{track.pause();track.flush();}catch(Exception ignored){}if(thread!=null){thread.interrupt();try{thread.join(2000);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}}}
    @Override public void onBackPressed(){menu();}
private void menu(){if(!running){finish();return;}paused=true;touch=keys=stick=0;clearAnalog();AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Libretro · "+GameFormats.label(coreName)).setItems(new String[]{"Resume",coreName.equals("scummvm")?"Save state (unsupported)":"Save state",coreName.equals("scummvm")?"Load state (unsupported)":"Load state","Reset game","Next disc","Touch controls: "+(touchControls?"On":"Off"),"Exit to library","Continue on TV"},(d,i)->{if((i==1||i==2)&&coreName.equals("scummvm")){toast("Use ScummVM's in-game save/load menu");return;}if(i==1)commands.add(()->toast(persist(".state",true)?"State saved":"Could not save state"));if(i==2)commands.add(()->toast(restore(".state",true)?"State loaded":"No saved state"));if(i==3)commands.add(RetroActivity::resetCore);if(i==4)commands.add(()->{int count=diskCount(),next=count>1?(diskIndex()+1)%count:0;toast(count>1&&swapDisk(next)?"Disc "+(next+1)+" of "+count:"Load an M3U playlist to switch discs");});if(i==5)setTouchControls(!touchControls);if(i==6)finish();if(i==7)continueOnTv();}).setNegativeButton("Resume",null).create();dialog.setOnDismissListener(d->{paused=false;hideBars();});dialog.show();Gamepad.dialog(dialog);}
    private void toast(String text){runOnUiThread(()->Toast.makeText(this,text,Toast.LENGTH_SHORT).show());}
    private void continueOnTv(){
        String source=coreName.equals("gambatte")?TvHandoff.SOURCE:coreName.equals("fceumm")?TvHandoff.NES_SOURCE:coreName+":unverified";
        TvHandoff.send(this,source,()->{
            java.util.concurrent.CompletableFuture<File> captured=new java.util.concurrent.CompletableFuture<>();
            commands.add(()->{if(captured.isCancelled())return;try{
                handingOff=true;touch=keys=stick=0;clearAnalog();
                org.json.JSONObject metadata=new org.json.JSONObject(handoffMetadata());
                if((metadata.optLong("quirks")&125)!=0)throw new IOException("This core cannot transfer portable save states");
                byte[] state=readMemory(true);if(state==null)throw new IOException("This core could not create a save state");
                metadata.put("protocol",1).put("type","game").put("source",source).put("romSha256",TvHandoff.hash(rom)).put("stateSha256",TvHandoff.hex(java.security.MessageDigest.getInstance("SHA-256").digest(state)));
                captured.complete(TvHandoff.archive(this,metadata,state,rom));
            }catch(Throwable error){handingOff=false;captured.completeExceptionally(error);}});
            try{return captured.get(30,java.util.concurrent.TimeUnit.SECONDS);}catch(Exception error){captured.cancel(false);handingOff=false;throw error;}
        },this::finish,()->handingOff=false);
    }
    private int bit(int code){boolean ps=coreName.equals("pcsx_rearmed")||coreName.equals("ppsspp");switch(code){case KeyEvent.KEYCODE_BUTTON_A:return ps?0:8;case KeyEvent.KEYCODE_BUTTON_B:return ps?8:0;case KeyEvent.KEYCODE_BUTTON_X:return ps?1:9;case KeyEvent.KEYCODE_BUTTON_Y:return ps?9:1;case KeyEvent.KEYCODE_BUTTON_SELECT:return 2;case KeyEvent.KEYCODE_BUTTON_START:return 3;case KeyEvent.KEYCODE_DPAD_UP:return 4;case KeyEvent.KEYCODE_DPAD_DOWN:return 5;case KeyEvent.KEYCODE_DPAD_LEFT:return 6;case KeyEvent.KEYCODE_DPAD_RIGHT:return 7;case KeyEvent.KEYCODE_BUTTON_L1:return 10;case KeyEvent.KEYCODE_BUTTON_R1:return 11;case KeyEvent.KEYCODE_BUTTON_L2:return 12;case KeyEvent.KEYCODE_BUTTON_R2:return 13;default:return -1;}}
@Override public boolean dispatchKeyEvent(KeyEvent e){if(e.getKeyCode()==KeyEvent.KEYCODE_BUTTON_THUMBR||e.getKeyCode()==KeyEvent.KEYCODE_MENU){if(e.getAction()==KeyEvent.ACTION_DOWN&&e.getRepeatCount()==0)menu();return true;}if(coreName.equals("scummvm")&&e.isFromSource(InputDevice.SOURCE_KEYBOARD)){int key=GameKeyboard.key(e.getKeyCode());if(key!=0){boolean down=e.getAction()==KeyEvent.ACTION_DOWN;int character=down?e.getUnicodeChar():0;if(e.getAction()==KeyEvent.ACTION_DOWN||e.getAction()==KeyEvent.ACTION_UP){int modifiers=GameKeyboard.modifiers(e.getMetaState());commands.add(()->sendKey(down,key,character,modifiers));}return true;}}int bit=bit(e.getKeyCode());if(bit>=0){if(e.getAction()==KeyEvent.ACTION_DOWN)keys|=1<<bit;else if(e.getAction()==KeyEvent.ACTION_UP)keys&=~(1<<bit);return true;}return super.dispatchKeyEvent(e);}
    @Override public boolean onGenericMotionEvent(MotionEvent e){if((e.getSource()&InputDevice.SOURCE_JOYSTICK)!=0){leftX=analog(e.getAxisValue(MotionEvent.AXIS_X));leftY=analog(e.getAxisValue(MotionEvent.AXIS_Y));rightX=analog(e.getAxisValue(MotionEvent.AXIS_Z));rightY=analog(e.getAxisValue(MotionEvent.AXIS_RZ));float x=e.getAxisValue(MotionEvent.AXIS_X),y=e.getAxisValue(MotionEvent.AXIS_Y),hatX=e.getAxisValue(MotionEvent.AXIS_HAT_X),hatY=e.getAxisValue(MotionEvent.AXIS_HAT_Y);if(Math.abs(hatX)>.5)x=hatX;if(Math.abs(hatY)>.5)y=hatY;stick=(x<-.35?1<<6:0)|(x>.35?1<<7:0)|(y<-.35?1<<4:0)|(y>.35?1<<5:0);return true;}return super.onGenericMotionEvent(e);}
}
