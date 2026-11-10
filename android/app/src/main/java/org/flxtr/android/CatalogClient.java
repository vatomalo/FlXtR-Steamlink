package org.flxtr.android;

import org.json.JSONObject;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import java.net.URL;
import java.net.URLEncoder;
import javax.net.ssl.HttpsURLConnection;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.io.*;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Comparator;

/** Bounded HTTPS metadata and image requests, always used off the UI thread. */
final class CatalogClient {
    private static final String API="https://plsdontscrapemelove.flixer.gd/api/tmdb";
    final File jsonDir,imageDir;
    CatalogClient(File cache){jsonDir=new File(cache,"catalog-json");imageDir=new File(cache,"catalog-images");jsonDir.mkdirs();imageDir.mkdirs();trim(jsonDir,64);trim(imageDir,160);}
    static final class Request {
        volatile boolean cancelled;
        volatile HttpsURLConnection connection;
        volatile StreamResolver.Request bridge;
        void cancel(){cancelled=true;HttpsURLConnection c=connection;if(c!=null)c.disconnect();if(bridge!=null)bridge.cancel();}
        void check() throws InterruptedIOException {if(cancelled||Thread.currentThread().isInterrupted())throw new InterruptedIOException("Cancelled");}
    }
    static String path(String kind,int page,String query,int id,int season) throws IOException {
        if(kind.equals("season"))return "/tv/"+id;
        if(kind.equals("episode"))return "/tv/"+id+"/season/"+season;
        if(!kind.equals("movie")&&!kind.equals("tv"))throw new IOException("Unknown catalogue");
        return query.isEmpty()?"/"+kind+"/popular?page="+page:
            "/search/"+kind+"?include_adult=false&query="+URLEncoder.encode(query,"UTF-8")+"&page="+page;
    }
    static String path(String kind,int page,String query,int id,int season,int genre,int order) throws IOException {
        if(!query.isEmpty()||(!kind.equals("movie")&&!kind.equals("tv"))||(genre==0&&order==0))return path(kind,page,query,id,season);
        int[] movie={0,28,12,35,18,14,27,9648,10749,878,53,16},tv={0,10759,10759,35,18,10765,-1,9648,-1,10765,-1,16};
        if(genre<0||genre>=movie.length||order<0||order>3)throw new IOException("Invalid filter");int g=(kind.equals("movie")?movie:tv)[genre];if(g<0)throw new IOException("Genre unavailable for series");
        String[] sort={"popularity.desc","vote_average.desc",kind.equals("movie")?"primary_release_date.desc":"first_air_date.desc",kind.equals("movie")?"original_title.asc":"name.asc"};
        return "/discover/"+kind+"?include_adult=false&vote_count.gte=20&sort_by="+sort[order]+"&page="+page+(g==0?"":"&with_genres="+g);
    }
    CatalogPage load(String kind,int page,String query,int id,int season,Request request) throws Exception {
        return load(kind,page,query,id,season,0,0,request);
    }
    CatalogPage load(String kind,int page,String query,int id,int season,int genre,int order,Request request) throws Exception {
        String path=path(kind,page,query,id,season,genre,order);
        File file=new File(jsonDir,hash(path)+".json");
        long age=System.currentTimeMillis()-file.lastModified();
        byte[] data=null;boolean cached=false;
        if(file.isFile()&&file.length()<=2*1024*1024&&age<15*60*1000)data=Files.readAllBytes(file.toPath());
        if(data==null){
            try{data=download(API+path,2*1024*1024,request);CatalogPage.parse(new JSONObject(new String(data,StandardCharsets.UTF_8)),kind,id,season);write(file,data);trim(jsonDir,64);}
            catch(Exception error){request.check();if(!file.isFile()||age>7L*24*3600*1000||file.length()>2*1024*1024)throw error;data=Files.readAllBytes(file.toPath());cached=true;}
        }
        request.check();
        CatalogPage result=CatalogPage.parse(new JSONObject(new String(data,StandardCharsets.UTF_8)),kind,id,season);result.cached=cached;return result;
    }
    Bitmap poster(String path) throws Exception {
        if(CatalogItem.safePoster(path).isEmpty())return null;
        File file=new File(imageDir,hash(path)+".img");
        byte[] data=file.isFile()&&file.length()<=4*1024*1024?Files.readAllBytes(file.toPath()):null;
        if(data==null){data=download(path.startsWith("https://")?path:"https://image.tmdb.org/t/p/w185"+path,4*1024*1024,new Request());write(file,data);}
        BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(data,0,data.length,bounds);
        if(bounds.outWidth<1||bounds.outWidth>4096||bounds.outHeight>8192){file.delete();return null;}
        BitmapFactory.Options options=new BitmapFactory.Options();options.inPreferredConfig=Bitmap.Config.RGB_565;options.inSampleSize=Math.max(1,bounds.outWidth/250);
        Bitmap bitmap=BitmapFactory.decodeByteArray(data,0,data.length,options);
        if(bitmap==null)file.delete();
        return bitmap;
    }
    private static byte[] download(String url,int limit,Request request) throws IOException {
        request.check();StreamResolver.Request media=new StreamResolver.Request();
        request.bridge=media;if(request.cancelled)media.cancel();try{return MediaHttp.bytes(url,java.util.Collections.emptyMap(),limit,media);}finally{request.bridge=null;}
    }
    private static void write(File file,byte[] bytes) throws IOException {
        File temp=File.createTempFile("download-",".tmp",file.getParentFile());
        try{Files.write(temp.toPath(),bytes);Files.move(temp.toPath(),file.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);}finally{temp.delete();}
    }
    static String hash(String text){try{byte[] digest=MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));StringBuilder out=new StringBuilder();for(byte b:digest)out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();}catch(Exception impossible){throw new IllegalStateException(impossible);}}
    private static void trim(File folder,int limit){File[] files=folder.listFiles();if(files==null||files.length<=limit)return;Arrays.sort(files,Comparator.comparingLong(File::lastModified));for(int i=0;i<files.length-limit;i++)files[i].delete();}
    void trimImages(){trim(imageDir,160);}
}
