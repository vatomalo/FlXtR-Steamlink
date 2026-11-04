package org.flxtr.android;
import android.app.Instrumentation;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import java.io.*;

/** Run without a foreground activity so screen changes cannot interrupt boot. */
public final class DolphinPlaybackInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){Bundle result=new Bundle();ImageReader reader=null;boolean active=false;
        try{
            CoreAssets.install(getTargetContext(),"dolphin");
            File root=new File(getTargetContext().getFilesDir(),"games"),fixture=new File(root,"engine-test/direct.iso"),saves=new File(root,"engine-test/direct-saves");saves.mkdirs();
            try(InputStream input=getTargetContext().getAssets().open("fixtures/gamecube-test.iso");OutputStream output=new FileOutputStream(fixture)){byte[] buffer=new byte[32768];int count;while((count=input.read(buffer))!=-1)output.write(buffer,0,count);}
            reader=ImageReader.newInstance(640,528,PixelFormat.RGBA_8888,3);
            String library=new File(getTargetContext().getApplicationInfo().nativeLibraryDir,"libflxtr_dolphin.so").getAbsolutePath();
            double[] av=RetroActivity.startCore(fixture.getAbsolutePath(),saves.getAbsolutePath(),new File(root,"system").getAbsolutePath(),library,reader.getSurface());
            if(av==null)throw new IOException("Dolphin initialization failed");active=true;
            int frames=0,samples=0;long deadline=System.currentTimeMillis()+90000;
            while(RetroActivity.renderedFrames()<120&&System.currentTimeMillis()<deadline){samples+=RetroActivity.frame(0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,false).length;frames++;Image image=reader.acquireLatestImage();if(image!=null)image.close();Thread.sleep(16);}
            result.putInt("dolphinCoreFrames",frames);result.putInt("dolphinRenderedFrames",RetroActivity.renderedFrames());result.putInt("audioSamples",samples);result.putString("coreMessage",RetroActivity.lastMessage());
            if(RetroActivity.renderedFrames()<120)throw new IOException("Dolphin did not present 120 frames");
            RetroActivity.stopCore();active=false;finish(-1,result);
        }catch(Throwable failure){result.putString("error",failure.toString());finish(0,result);}finally{if(active)RetroActivity.stopCore();if(reader!=null)reader.close();}
    }
}
