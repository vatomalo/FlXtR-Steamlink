package org.flxtr.android;
import android.content.*;
import org.json.*;
import java.util.*;
import java.io.*;
final class MediaSources {
    private static final Map<String,ArrayDeque<CatalogItem>> radio=new HashMap<>();
    static StreamProtocol.Stream resolve(Context context,CatalogItem item,int attempt,StreamResolver.Request request,StreamResolver.Progress progress) throws Exception {
        android.content.SharedPreferences prefs=context.getSharedPreferences("android-settings",Context.MODE_PRIVATE);
        if(item.kind.equals("kiss-episode")){progress.update("Opening anime "+(prefs.getBoolean("anime-dub",false)?"DUB":"SUB")+"…");return KissAnime.resolve(item,attempt,prefs.getBoolean("anime-dub",false),request);}
        if(item.kind.equals("archive-file")||item.kind.equals("local")){
            if(item.kind.equals("local")&&!item.link.startsWith("content://"))throw new IOException("Choose a local media document");
            if(item.kind.equals("archive-file")&&!CatalogItem.validStream(item.link))throw new IOException("Invalid archive video");StreamProtocol.Stream stream=new StreamProtocol.Stream(item.link,"Local / Archive");stream.headers.clear();stream.headers.put("User-Agent","FlXtR-Android/0.4");stream.item=item;return stream;
        }
        if(item.kind.equals("offline"))return OfflineMedia.stream(context,item);
        if(item.kind.equals("music")||item.kind.equals("music-query")||item.kind.equals("music-video")){progress.update("Finding music playback…");return MusicEngine.resolve(context,item,item.kind.equals("music-video"),request);}
        if(item.kind.equals("mtv-channel")){
            progress.update("Selecting a music video…");CatalogItem video;
            synchronized(radio){ArrayDeque<CatalogItem> queue=radio.get(item.key());if(queue==null||queue.isEmpty()){
                int[] weights=new int[16];for(int i=1;i<16;i++)weights[i]=prefs.getInt("mtv-weight-"+i,1);
                String query=MtvPolicy.query(item.id,prefs.getInt("mtv-fusion",0),weights,new Random().nextInt(),prefs.getString("mtv-custom",""));
                List<CatalogItem> candidates=new ArrayList<>(MusicEngine.search(context,query,1,request).items);Collections.shuffle(candidates);queue=new ArrayDeque<>();for(CatalogItem row:candidates)queue.add(new CatalogItem("music-video",0,0,0,row.title,row.meta,row.overview,row.poster,row.link));radio.put(item.key(),queue);
            }video=queue.pollFirst();}
            if(video==null)throw new IOException("No music videos available");return MusicEngine.resolve(context,video,true,request);
        }
        if(item.kind.equals("tv-channel")){progress.update("Selecting a TV episode…");CatalogItem episode=TvChannels.next(context,item,request);return new StreamResolver.Session(episode).next(request,progress);}
        throw new IOException("Unsupported media section");
    }
    static CatalogItem next(Context context,CatalogItem item,StreamResolver.Request request) throws Exception {
        if(item.kind.equals("episode")){
            for(int sn=item.season;sn<=item.season+1;sn++){
                CatalogPage episodes=CatalogPage.parse(new JSONObject(MediaHttp.text(StreamProtocol.BASE+"/api/tmdb/tv/"+item.id+"/season/"+sn,request)),"episode",item.id,sn);
                for(CatalogItem episode:episodes.items)if(sn>item.season||episode.episode>item.episode)return episode;
            }
        }
        if(item.kind.equals("kiss-episode")){
            CatalogPage all=KissAnime.episodesHtml(MediaHttp.text(KissAnime.BASE+"/?post_type=anime&p="+item.season,request),item.season,0);
            for(int i=0;i<all.items.size()-1;i++)if(all.items.get(i).id==item.id)return all.items.get(i+1);
        }
        return null;
    }
}
