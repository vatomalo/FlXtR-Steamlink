package org.flxtr.android;

import org.json.JSONObject;
import java.net.URI;

/** Metadata identity stays stable across pages, favorites and stream links. */
final class CatalogItem {
    final String kind,title,meta,overview,poster,link;
    final int id,season,episode;
    CatalogItem(String kind,int id,int season,int episode,String title,String meta,String overview,String poster){
        this(kind,id,season,episode,title,meta,overview,poster,"");
    }
    CatalogItem(String kind,int id,int season,int episode,String title,String meta,String overview,String poster,String link){
        this.link=link;
        this.kind=kind;this.id=id;this.season=season;this.episode=episode;
        this.title=title;this.meta=meta;this.overview=overview;this.poster=poster;
    }
    String key(){return id!=0||link.isEmpty()?kind+":"+id+":"+season+":"+episode:kind+":"+digest(link);}
    private static String digest(String value){try{byte[] bytes=java.security.MessageDigest.getInstance("SHA-256").digest(value.getBytes(java.nio.charset.StandardCharsets.UTF_8));StringBuilder out=new StringBuilder();for(byte b:bytes)out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();}catch(Exception error){throw new IllegalStateException(error);}}
    JSONObject json() throws org.json.JSONException {
        return new JSONObject().put("kind",kind).put("id",id).put("season",season).put("episode",episode)
            .put("title",title).put("meta",meta).put("overview",overview).put("poster",poster).put("link",link);
    }
    static CatalogItem saved(JSONObject o){
        return new CatalogItem(o.optString("kind","movie"),o.optInt("id"),o.optInt("season"),o.optInt("episode"),
            o.optString("title","Untitled"),o.optString("meta"),o.optString("overview"),safePoster(o.optString("poster")),o.optString("link",""));
    }
    static String safePoster(String path){
        if(path.matches("/[A-Za-z0-9_.-]+")&&!path.contains(".."))return path;
        if(!validStream(path))return "";
        String host=URI.create(path).getHost().toLowerCase(java.util.Locale.ROOT);
        return host.equals("kissanime.com.cv")||host.equals("image.tmdb.org")||host.equals("i.ytimg.com")||host.equals("archive.org")||host.endsWith(".archive.org")||host.endsWith(".wp.com")?path:"";
    }
    static boolean validStream(String value){
        if(value==null||value.length()>8192)return false;
        for(int i=0;i<value.length();i++)if(value.charAt(i)<32||value.charAt(i)==127)return false;
        try {URI u=new URI(value);return "https".equalsIgnoreCase(u.getScheme())&&u.getHost()!=null&&u.getUserInfo()==null;}
        catch(Exception invalid){return false;}
    }
}
