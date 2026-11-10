package org.flxtr.android;

import android.content.Context;
import org.json.*;
import java.io.IOException;
import java.util.*;

/** Resolve a series poster to its current episode without opening a local player. */
final class MediaStart {
    static boolean group(CatalogItem item){return item.kind.equals("tv")||item.kind.equals("season")||item.kind.equals("kiss");}
    static boolean belongs(CatalogItem group,CatalogItem episode){
        if(group.kind.equals("kiss"))return episode.kind.equals("kiss-episode")&&episode.season==group.id;
        return episode.kind.equals("episode")&&episode.id==group.id&&(!group.kind.equals("season")||episode.season==group.season);
    }
    static JSONObject recent(CatalogItem group,JSONArray history){
        for(int i=0;i<history.length();i++){JSONObject row=history.optJSONObject(i);if(row!=null&&belongs(group,CatalogItem.saved(row)))return row;}
        return null;
    }
    static CatalogItem select(Context context,CatalogItem item,StreamResolver.Request request)throws Exception{
        if(!group(item))return item;
        JSONArray history;try{history=new JSONArray(context.getSharedPreferences(MediaHistory.PREFS,0).getString("items","[]"));}catch(Exception invalid){history=new JSONArray();}
        JSONObject latest=recent(item,history);
        if(latest!=null){CatalogItem previous=CatalogItem.saved(latest);
            if(!latest.optBoolean("completed"))return title(item,previous);
            CatalogItem next=MediaSources.next(context,previous,request);if(next!=null&&belongs(item,next))return title(item,next);
        }
        if(item.kind.equals("kiss")){
            CatalogPage episodes=KissAnime.episodes(item.id,0,request);if(!episodes.items.isEmpty())return title(item,episodes.items.get(0));
        }else{
            List<Integer> seasons=new ArrayList<>();
            if(item.kind.equals("season"))seasons.add(item.season);
            else{
                CatalogPage all=CatalogPage.parse(new JSONObject(MediaHttp.text(StreamProtocol.BASE+"/api/tmdb/tv/"+item.id,request)),"season",item.id,0);
                for(CatalogItem season:all.items)if(season.season>0)seasons.add(season.season);
                Collections.sort(seasons);if(seasons.isEmpty())seasons.add(1);
            }
            for(int index=0;index<Math.min(seasons.size(),5);index++){
                request.check();int season=seasons.get(index);
                CatalogPage episodes=CatalogPage.parse(new JSONObject(MediaHttp.text(StreamProtocol.BASE+"/api/tmdb/tv/"+item.id+"/season/"+season,request)),"episode",item.id,season);
                if(!episodes.items.isEmpty())return title(item,episodes.items.get(0));
            }
        }
        throw new IOException("No episode is available. Hold this title and choose Browse episodes.");
    }
    private static CatalogItem title(CatalogItem group,CatalogItem episode){
        if(episode.title.startsWith(group.title+" · "))return episode;
        return new CatalogItem(episode.kind,episode.id,episode.season,episode.episode,group.title+" · "+episode.title,episode.meta,episode.overview,episode.poster,episode.link);
    }
    static long position(Context context,CatalogItem item){Object saved=context.getSharedPreferences("playback-progress",0).getAll().get("position:"+item.key());return saved instanceof Number?Math.max(0,((Number)saved).longValue()):0;}
}
