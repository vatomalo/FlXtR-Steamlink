package org.flxtr.android;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.widget.ImageView;
import android.util.LruCache;
import java.io.File;
import java.net.URLEncoder;
import java.nio.file.Files;
import java.util.Collections;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONObject;

final class GameCovers {
    private final Activity activity;
    private final LruCache<String,Bitmap> bitmaps=new LruCache<String,Bitmap>(4*1024*1024){
        @Override protected int sizeOf(String key,Bitmap bitmap){return bitmap.getAllocationByteCount();}
    };
    private volatile boolean closed;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final StreamResolver.Request request=new StreamResolver.Request();
    GameCovers(Activity activity){this.activity=activity;}
    void load(JSONObject game,ImageView image,Runnable show){
        String name=game.optString("name").replaceFirst("\\.[^.]+$","");
        String core=game.optString("core"),system;
switch(core){case "genesis_plus_gx":system=game.optString("name").toLowerCase(java.util.Locale.ROOT).endsWith(".gg")?"Sega - Game Gear":game.optString("name").toLowerCase(java.util.Locale.ROOT).endsWith(".sms")?"Sega - Master System - Mark III":"Sega - Mega Drive - Genesis";break;case "mednafen_pce_fast":system="NEC - PC Engine - TurboGrafx 16";break;case "ppsspp":system="Sony - PlayStation Portable";break;case "dolphin":system=game.optString("name").toLowerCase(java.util.Locale.ROOT).endsWith(".wbfs")?"Nintendo - Wii":"Nintendo - GameCube";break;case "scummvm":return;case "fceumm":system="Nintendo - Nintendo Entertainment System";break;case "snes9x":system="Nintendo - Super Nintendo Entertainment System";break;case "mgba":system="Nintendo - Game Boy Advance";break;case "pcsx_rearmed":system="Sony - PlayStation";break;default:system=game.optString("name").toLowerCase(java.util.Locale.ROOT).endsWith(".gbc")?"Nintendo - Game Boy Color":"Nintendo - Game Boy";}
        final String platform=system;
        try{
            String url="https://thumbnails.libretro.com/"+URLEncoder.encode(platform,"UTF-8").replace("+","%20")+"/Named_Boxarts/"+URLEncoder.encode(name.replaceAll("[&*/:`<>?\\\\|]","_"),"UTF-8").replace("+","%20")+".png";
            image.setTag(url);
            Bitmap ready=bitmaps.get(url);
            if(ready!=null){image.setImageBitmap(ready);show.run();return;}
            if(closed)return;
            worker.execute(()->{try{
            if(closed)return;
            File cache=new File(activity.getCacheDir(),"game-covers");cache.mkdirs();
            File[] cached=cache.listFiles();if(cached!=null&&cached.length>256){java.util.Arrays.sort(cached,java.util.Comparator.comparingLong(File::lastModified));for(int index=0;index<cached.length-256;index++)cached[index].delete();}
            File file=new File(cache,GameLibrary.hash(url)+".png"),miss=new File(cache,file.getName()+".miss");
            if(!file.isFile()&&miss.isFile()&&System.currentTimeMillis()-miss.lastModified()<86400000)return;
            byte[] bytes;
            if(file.isFile()&&file.length()<=2097152)bytes=Files.readAllBytes(file.toPath());
            else{try{bytes=MediaHttp.bytes(url,Collections.emptyMap(),2097152,request);}catch(Exception unavailable){if(!request.cancelled){miss.createNewFile();miss.setLastModified(System.currentTimeMillis());}return;}Files.write(file.toPath(),bytes);}
            BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(bytes,0,bytes.length,bounds);
            if(bounds.outWidth<1||bounds.outHeight<1||bounds.outWidth>4096||bounds.outHeight>4096){file.delete();return;}
            BitmapFactory.Options options=new BitmapFactory.Options();options.inSampleSize=Math.max(1,Math.max(bounds.outWidth,bounds.outHeight)/256);options.inPreferredConfig=Bitmap.Config.RGB_565;
            Bitmap bitmap=BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);
            if(bitmap!=null&&!closed){bitmaps.put(url,bitmap);activity.runOnUiThread(()->{if(!closed&&!activity.isFinishing()&&!activity.isDestroyed()&&url.equals(image.getTag())){image.setImageBitmap(bitmap);show.run();}});}
        }catch(Exception ignored){}});
        }catch(Exception ignored){}
    }
    void close(){closed=true;request.cancel();worker.shutdownNow();bitmaps.evictAll();}
}
