package org.flxtr.android;

import org.json.JSONObject;
import java.net.URI;

/** Metadata identity stays stable across pages, favorites and stream links. */
final class CatalogItem {
    final String kind,title,meta,overview,poster;
    final int id,season,episode;
    CatalogItem(String kind,int id,int season,int episode,String title,String meta,String overview,String poster){
        this.kind=kind;this.id=id;this.season=season;this.episode=episode;
        this.title=title;this.meta=meta;this.overview=overview;this.poster=poster;
    }
    String key(){return kind+":"+id+":"+season+":"+episode;}
    JSONObject json() throws org.json.JSONException {
        return new JSONObject().put("kind",kind).put("id",id).put("season",season).put("episode",episode)
            .put("title",title).put("meta",meta).put("overview",overview).put("poster",poster);
    }
    static CatalogItem saved(JSONObject o){
        return new CatalogItem(o.optString("kind","movie"),o.optInt("id"),o.optInt("season"),o.optInt("episode"),
            o.optString("title","Untitled"),o.optString("meta"),o.optString("overview"),safePoster(o.optString("poster")));
    }
    static String safePoster(String path){return path.matches("/[A-Za-z0-9_.-]+")&&!path.contains("..")?path:"";}
    static boolean validStream(String value){
        if(value==null||value.length()>8192)return false;
        for(int i=0;i<value.length();i++)if(value.charAt(i)<32||value.charAt(i)==127)return false;
        try {URI u=new URI(value);return "https".equalsIgnoreCase(u.getScheme())&&u.getHost()!=null&&u.getUserInfo()==null;}
        catch(Exception invalid){return false;}
    }
}
