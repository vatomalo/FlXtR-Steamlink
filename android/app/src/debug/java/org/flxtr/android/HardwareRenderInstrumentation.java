package org.flxtr.android;
import android.app.Instrumentation;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import java.io.File;
import java.nio.ByteBuffer;

public final class HardwareRenderInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){Bundle result=new Bundle();ImageReader reader=null;boolean active=false;try{
        reader=ImageReader.newInstance(64,64,PixelFormat.RGBA_8888,3);
        File root=getTargetContext().getFilesDir();String library=new File(getTargetContext().getApplicationInfo().nativeLibraryDir,"libflxtr_gpu_fixture.so").getAbsolutePath();
        double[] info=RetroActivity.startCore(new File(root,"fixture.rom").getAbsolutePath(),root.getAbsolutePath(),root.getAbsolutePath(),library,reader.getSurface());
        if(info==null)throw new IllegalStateException("GLES context did not initialize");active=true;
        for(int frame=0;frame<6;frame++){
            short[] audio=RetroActivity.frame(0,(short)20000,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,false);
            if(audio.length!=512)throw new IllegalStateException("Audio callback failed");
            Image image=null;long deadline=System.currentTimeMillis()+2000;while(image==null&&System.currentTimeMillis()<deadline){image=reader.acquireLatestImage();if(image==null)Thread.sleep(10);}
            if(image==null)throw new IllegalStateException("GPU frame not delivered");
            try{ByteBuffer pixels=image.getPlanes()[0].getBuffer();int red=pixels.get(0)&255,green=pixels.get(1)&255,blue=pixels.get(2)&255;if(red<240||green<110||green>145||blue<45||blue>85)throw new IllegalStateException("Rendered pixels or analog input incorrect: "+red+","+green+","+blue);}finally{image.close();}
        }
        byte[] state=RetroActivity.readMemory(true);if(state==null||!RetroActivity.writeMemory(state,true))throw new IllegalStateException("GPU core state round trip failed");
        result.putInt("gpuFrames",RetroActivity.renderedFrames());result.putBoolean("analogInput",true);result.putBoolean("pixelReadback",true);RetroActivity.stopCore();active=false;reader.close();reader=null;finish(-1,result);
    }catch(Throwable failure){result.putString("error",failure.toString());finish(0,result);}finally{if(active)RetroActivity.stopCore();if(reader!=null)reader.close();}}
}
