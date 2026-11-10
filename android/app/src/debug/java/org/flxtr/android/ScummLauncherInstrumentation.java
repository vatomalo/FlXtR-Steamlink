package org.flxtr.android;
import android.app.Instrumentation;
import android.graphics.PixelFormat;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import java.io.File;
import java.io.*;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.zip.*;

public final class ScummLauncherInstrumentation extends Instrumentation {
    private boolean game;
    @Override public void onCreate(Bundle args){super.onCreate(args);game=args!=null&&"sky".equals(args.getString("game"));start();}
    @Override public void onStart(){Bundle result=new Bundle();ImageReader reader=null;boolean active=false;try{
        CoreAssets.install(getTargetContext(),"scummvm");File root=getTargetContext().getFilesDir(),system=new File(root,"games/system"),saves=new File(root,"games/saves/scummvm-test");saves.mkdirs();
        String gamePath="";if(game){File folder=new File(root,"games/engine-test/sky");folder.mkdirs();try(ZipInputStream zip=new ZipInputStream(getTargetContext().getAssets().open("fixtures/scummvm-sky.zip"))){ZipEntry entry;byte[] buffer=new byte[32768];while((entry=zip.getNextEntry())!=null){File target=new File(folder,entry.getName());if(!target.getCanonicalPath().startsWith(folder.getCanonicalPath()+File.separator))throw new IOException("Invalid fixture path");if(entry.isDirectory()){target.mkdirs();continue;}try(OutputStream output=new FileOutputStream(target)){int read;while((read=zip.read(buffer))!=-1)output.write(buffer,0,read);}}}File hook=new File(folder,"sky.scummvm");Files.write(hook.toPath(),"sky\n".getBytes(StandardCharsets.UTF_8));gamePath=hook.getAbsolutePath();}
        reader=ImageReader.newInstance(game?320:640,game?200:480,PixelFormat.RGBA_8888,3);
        String library=new File(getTargetContext().getApplicationInfo().nativeLibraryDir,"libflxtr_scummvm.so").getAbsolutePath();
        double[] info=RetroActivity.startCore(gamePath,saves.getAbsolutePath(),system.getAbsolutePath(),library,reader.getSurface());
        if(info==null)throw new IllegalStateException("ScummVM launcher failed to initialize");active=true;
        int samples=0;for(int frame=0;frame<(game?900:30);frame++){short[] audio=RetroActivity.frame(0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,(short)0,false);samples+=audio.length;Image image=reader.acquireLatestImage();if(image!=null)image.close();Thread.sleep(16);if(game&&frame>=120&&samples>=1000)break;}
        result.putInt(game?"scummvmGameFrames":"scummvmFrames",RetroActivity.renderedFrames());result.putInt("audioSamples",samples);result.putString("coreMessage",RetroActivity.lastMessage());if(RetroActivity.renderedFrames()<1)throw new IllegalStateException("ScummVM did not render");if(game&&samples<1000)throw new IllegalStateException("ScummVM game did not generate audio samples");
        RetroActivity.stopCore();active=false;reader.close();reader=null;finish(-1,result);
    }catch(Throwable failure){result.putString("error",failure.toString());finish(0,result);}finally{if(active)RetroActivity.stopCore();if(reader!=null)reader.close();}}
}
