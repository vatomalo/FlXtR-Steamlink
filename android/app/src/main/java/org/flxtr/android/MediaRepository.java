package org.flxtr.android;
import android.content.*;
import org.json.*;
import java.util.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
final class MediaRepository {
    static final String[] GENRES={"All genres","Action","Adventure","Comedy","Drama","Fantasy","Horror","Mystery","Romance","Sci-fi","Thriller","Animation"};
    static final String[] ORDERS={"Most popular","Top rated","Latest","Title A–Z"};
    static final String[] KISS_GENRES={"","action","adventure","comedy","drama","fantasy","horror","mystery","romance","sci-fi","thriller","kids"},KISS_ORDERS={"popular","rating","latest","title"};
    static final class Query {
        final String kind,text,link;final int page,id,season,genre,order;
        Query(String kind,int page,String text,int id,int season,String link,int genre,int order){this.kind=kind;this.page=page;this.text=text;this.id=id;this.season=season;this.link=link;this.genre=genre;this.order=order;}
        String cacheKey(){return kind+"|"+page+"|"+text+"|"+id+"|"+season+"|"+link+"|"+genre+"|"+order;}
    }
    static final class Job {
        final CatalogClient.Request catalog=new CatalogClient.Request();final StreamResolver.Request media=new StreamResolver.Request();
        void cancel(){catalog.cancel();media.cancel();}
    }
    private final Context context;private final CatalogClient catalog;private final File pages;
    MediaRepository(Context context,CatalogClient catalog){this.context=context.getApplicationContext();this.catalog=catalog;pages=new File(context.getCacheDir(),"media-pages");pages.mkdirs();}
    CatalogPage load(Query q,Job job,boolean refresh) throws Exception {
        if(Arrays.asList("movie","tv","season","episode").contains(q.kind)){
            if(refresh)new File(catalog.jsonDir,CatalogClient.hash(CatalogClient.path(q.kind,q.page,q.text,q.id,q.season,q.genre,q.order))+".json").delete();
            CatalogPage all=catalog.load(q.kind,q.page,q.text,q.id,q.season,q.genre,q.order,job.catalog);
            if(q.kind.equals("season")||q.kind.equals("episode")){int from=Math.min(all.items.size(),Math.max(0,q.page-1)*20);return new CatalogPage(new ArrayList<>(all.items.subList(from,Math.min(from+20,all.items.size()))),Math.max(1,(all.items.size()+19)/20));}return all;
        }
        if(q.kind.equals("archive-home"))return ArchiveMedia.folders();
        if(q.kind.equals("local"))return new CatalogPage(LocalMedia.list(context),1);
        if(q.kind.equals("downloads"))return new CatalogPage(OfflineMedia.list(context),1);
        if(q.kind.equals("history")||q.kind.equals("continue"))return new CatalogPage(MediaHistory.list(context,q.kind.equals("continue")),1);
        if(q.kind.equals("channels"))return TvChannels.list(context);
        if(q.kind.equals("mtv")){ArrayList<CatalogItem> rows=new ArrayList<>();for(int i=0;i<16;i++)rows.add(new CatalogItem("mtv-channel",i,0,0,MtvPolicy.NAMES[i],"Continuous music videos","",""));return new CatalogPage(rows,1);}
        File cache=new File(pages,CatalogClient.hash(q.cacheKey())+".json");long age=System.currentTimeMillis()-cache.lastModified();
        if(!refresh&&cache.isFile()&&age<15*60*1000)return read(cache,false);
        try{
            CatalogPage page;
            switch(q.kind){
                case "kiss":page=KissAnime.list(q.page,q.text,KISS_GENRES[Math.max(0,Math.min(11,q.genre))],KISS_ORDERS[Math.max(0,Math.min(3,q.order))],job.media);break;
                case "kiss-episodes":page=KissAnime.episodes(q.id,q.page,job.media);break;
                case "archive":page=ArchiveMedia.list(q.page,q.id,q.text,job.media);break;
                case "archive-files":page=ArchiveMedia.files(q.link,q.page,job.media);break;
                case "music":page=MusicEngine.search(context,q.text,q.page,job.media);break;
                default:throw new IOException("Unknown media library");
            }
            job.media.check();JSONArray rows=new JSONArray();for(CatalogItem item:page.items)rows.put(item.json());JSONObject stored=new JSONObject().put("pages",page.pages).put("rows",rows);Files.write(cache.toPath(),stored.toString().getBytes(StandardCharsets.UTF_8));trim();return page;
        }catch(Exception error){job.media.check();if(cache.isFile()&&age<7L*24*3600*1000)return read(cache,true);throw error;}
    }
    private CatalogPage read(File file,boolean stale) throws Exception {if(file.length()>2*1024*1024)throw new IOException("Cached page is too large");JSONObject data=new JSONObject(new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8));JSONArray array=data.getJSONArray("rows");ArrayList<CatalogItem> rows=new ArrayList<>();for(int i=0;i<Math.min(array.length(),5000);i++)rows.add(CatalogItem.saved(array.getJSONObject(i)));CatalogPage page=new CatalogPage(rows,data.getInt("pages"));page.cached=stale;return page;}
    private void trim(){File[] files=pages.listFiles();if(files==null||files.length<=64)return;Arrays.sort(files,Comparator.comparingLong(File::lastModified));for(int i=0;i<files.length-64;i++)files[i].delete();}
}
