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
        void cancel(){cancelled=true;HttpsURLConnection c=connection;if(c!=null)c.disconnect();}
        void check() throws InterruptedIOException {if(cancelled||Thread.currentThread().isInterrupted())throw new InterruptedIOException("Cancelled");}
    }
    static String path(String kind,int page,String query,int id,int season) throws IOException {
        if(kind.equals("season"))return "/tv/"+id;
        if(kind.equals("episode"))return "/tv/"+id+"/season/"+season;
        if(!kind.equals("movie")&&!kind.equals("tv"))throw new IOException("Unknown catalogue");
        return query.isEmpty()?"/"+kind+"/popular?page="+page:
            "/search/"+kind+"?include_adult=false&query="+URLEncoder.encode(query,"UTF-8")+"&page="+page;
    }
    CatalogPage load(String kind,int page,String query,int id,int season,Request request) throws Exception {
        String path=path(kind,page,query,id,season);
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
        byte[] data=file.isFile()&&file.length()<=1024*1024?Files.readAllBytes(file.toPath()):null;
        if(data==null){data=download("https://image.tmdb.org/t/p/w185"+path,1024*1024,new Request());write(file,data);}
        BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;BitmapFactory.decodeByteArray(data,0,data.length,bounds);
        if(bounds.outWidth<1||bounds.outWidth>1024||bounds.outHeight>2048){file.delete();return null;}
        BitmapFactory.Options options=new BitmapFactory.Options();options.inPreferredConfig=Bitmap.Config.RGB_565;
        Bitmap bitmap=BitmapFactory.decodeByteArray(data,0,data.length,options);
        if(bitmap==null)file.delete();
        return bitmap;
    }
    private static byte[] download(String url,int limit,Request request) throws IOException {
        request.check();HttpsURLConnection connection=(HttpsURLConnection)new URL(url).openConnection();request.connection=connection;
        connection.setConnectTimeout(8000);connection.setReadTimeout(12000);connection.setInstanceFollowRedirects(false);
        connection.setRequestProperty("Accept","application/json,image/*");connection.setRequestProperty("User-Agent","FlXtR-Android/0.2");
        try {
            request.check();int code=connection.getResponseCode();if(code!=200)throw new IOException("Server returned HTTP "+code);
            if(connection.getContentLengthLong()>limit)throw new IOException("Response is too large");
            try(InputStream in=connection.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){
                byte[] buffer=new byte[8192];int n;
                while((n=in.read(buffer))!=-1){request.check();if(out.size()+n>limit)throw new IOException("Response is too large");out.write(buffer,0,n);}
                return out.toByteArray();
            }
        } finally {request.connection=null;connection.disconnect();}
    }
    private static void write(File file,byte[] bytes) throws IOException {
        File temp=File.createTempFile("download-",".tmp",file.getParentFile());
        try{Files.write(temp.toPath(),bytes);Files.move(temp.toPath(),file.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);}finally{temp.delete();}
    }
    static String hash(String text){try{byte[] digest=MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));StringBuilder out=new StringBuilder();for(byte b:digest)out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();}catch(Exception impossible){throw new IllegalStateException(impossible);}}
    private static void trim(File folder,int limit){File[] files=folder.listFiles();if(files==null||files.length<=limit)return;Arrays.sort(files,Comparator.comparingLong(File::lastModified));for(int i=0;i<files.length-limit;i++)files[i].delete();}
    void trimImages(){trim(imageDir,160);}
}
