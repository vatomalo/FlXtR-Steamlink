package org.flxtr.android;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import java.io.*;
import java.lang.reflect.Field;
import java.util.Queue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class EnginePlaybackInstrumentation extends Instrumentation {
    private String core="ppsspp";
    @Override public void onCreate(Bundle args){super.onCreate(args);if(args!=null&&"dolphin".equals(args.getString("core")))core="dolphin";start();}
    @Override public void onStart(){Bundle result=new Bundle();RetroActivity activity=null;try{
        File root=new File(getTargetContext().getFilesDir(),"games/engine-test");root.mkdirs();File rom=new File(root,core.equals("dolphin")?"homebrew.iso":"homebrew.pbp");
        try(InputStream input=getTargetContext().getAssets().open(core.equals("dolphin")?"fixtures/gamecube-test.iso":"fixtures/psp-homebrew.pbp");OutputStream output=new FileOutputStream(rom)){byte[] buffer=new byte[32768];int read;while((read=input.read(buffer))!=-1)output.write(buffer,0,read);}
        activity=(RetroActivity)startActivitySync(new Intent(getTargetContext(),RetroActivity.class).putExtra("rom",rom.getAbsolutePath()).putExtra("core",core).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));
        long deadline=System.currentTimeMillis()+120000;while((activity.framesPlayed<120||RetroActivity.renderedFrames()<120)&&System.currentTimeMillis()<deadline&&!activity.isFinishing())Thread.sleep(200);
        result.putInt(core+"RenderedFrames",RetroActivity.renderedFrames());result.putLong(core+"CoreFrames",activity.framesPlayed);
        if(activity.framesPlayed<120||RetroActivity.renderedFrames()<120)throw new IOException(core+" did not present 120 frames");
        if(core.equals("ppsspp")){Field field=RetroActivity.class.getDeclaredField("commands");field.setAccessible(true);CountDownLatch ready=new CountDownLatch(1);boolean[] restored={false};((Queue<Runnable>)field.get(activity)).add(()->{try{byte[] state=RetroActivity.readMemory(true);result.putInt("stateBytes",state==null?0:state.length);restored[0]=state!=null&&RetroActivity.writeMemory(state,true);}catch(Throwable failure){result.putString("stateError",failure.toString());}finally{ready.countDown();}});if(!ready.await(30,TimeUnit.SECONDS)||!restored[0])throw new IOException("PSP save-state round trip failed");result.putBoolean("stateRoundTrip",true);}
        RetroActivity finished=activity;runOnMainSync(finished::finish);finish(-1,result);
    }catch(Throwable failure){if(activity!=null){RetroActivity finished=activity;runOnMainSync(finished::finish);}result.putString("error",failure.toString());finish(0,result);}}
}
