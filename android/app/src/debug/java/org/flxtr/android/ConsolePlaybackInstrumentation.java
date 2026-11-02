package org.flxtr.android;
import android.app.*;
import android.content.Intent;
import android.os.*;
import java.io.*;
import java.lang.reflect.Field;
import java.util.Queue;
import java.util.concurrent.*;

/** Runs original cartridge fixtures through the real JNI renderer/audio loop. */
public final class ConsolePlaybackInstrumentation extends Instrumentation {
 @Override public void onCreate(Bundle args){super.onCreate(args);start();}
 @Override public void onStart(){Bundle result=new Bundle();try{getUiAutomation();for(String extension:new String[]{"nes","sfc","gba"}){
  File folder=new File(getTargetContext().getFilesDir(),"games/smoke");if(!folder.isDirectory()&&!folder.mkdirs())throw new IOException("Test storage unavailable");File rom=new File(folder,"smoke."+extension);
  try(InputStream in=getTargetContext().getAssets().open("fixtures/smoke."+extension);OutputStream out=new FileOutputStream(rom)){byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}
  RetroActivity activity=(RetroActivity)startActivitySync(new Intent(getTargetContext(),RetroActivity.class).putExtra("rom",rom.getAbsolutePath()).putExtra("saveKey",GameLibrary.hash("fixture-"+extension)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));
  long end=System.currentTimeMillis()+15000;while(activity.framesPlayed<120&&System.currentTimeMillis()<end)Thread.sleep(100);
  result.putLong(extension+"Frames",activity.framesPlayed);result.putLong(extension+"Audio",activity.samplesPlayed);if(activity.framesPlayed<120||activity.samplesPlayed<1000)throw new IOException(extension+": playback did not advance");
  Field queue=RetroActivity.class.getDeclaredField("commands");queue.setAccessible(true);CountDownLatch done=new CountDownLatch(1);boolean[] saved={false};((Queue<Runnable>)queue.get(activity)).add(()->{byte[] state=RetroActivity.readMemory(true);saved[0]=state!=null&&RetroActivity.writeMemory(state,true);done.countDown();});if(!done.await(5,TimeUnit.SECONDS)||!saved[0])throw new IOException(extension+": save-state round trip failed");result.putBoolean(extension+"State",true);
  runOnMainSync(activity::finish);Thread.sleep(600);
 }finish(-1,result);}catch(Throwable e){result.putString("error",e.getClass().getSimpleName()+": "+e.getMessage());finish(0,result);}}
}
