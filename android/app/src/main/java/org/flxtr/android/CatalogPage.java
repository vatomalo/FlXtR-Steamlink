package org.flxtr.android;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class CatalogPage {
    final List<CatalogItem> items;
    final int pages;
    boolean cached;
    CatalogPage(List<CatalogItem> items,int pages){this.items=items;this.pages=pages;}
    static CatalogPage parse(JSONObject root,String kind,int showId,int season) throws JSONException {
        String field=kind.equals("season")?"seasons":kind.equals("episode")?"episodes":"results";
        JSONArray rows=root.optJSONArray(field);
        if(rows==null)throw new JSONException("Catalogue response is missing "+field);
        List<CatalogItem> items=new ArrayList<>();
        for(int i=0;i<Math.min(rows.length(),200);i++){
            JSONObject row=rows.optJSONObject(i);if(row==null)continue;
            int id=row.optInt("id"),sn=0,ep=0;
            if(kind.equals("season")){id=showId;sn=row.optInt("season_number",-1);if(sn<0)continue;}
            if(kind.equals("episode")){id=showId;sn=season;ep=row.optInt("episode_number",0);if(ep<1)continue;}
            if(id<1)continue;
            String title=row.optString(kind.equals("movie")?"title":"name","Untitled");
            if(title.isEmpty()||title.equals("null"))title="Untitled";
            String date=row.optString(kind.equals("movie")?"release_date":kind.equals("tv")?"first_air_date":"air_date","");
            String year=date.length()>=4?date.substring(0,4):"";
            String meta=kind.equals("season")?row.optInt("episode_count")+" episodes":
                kind.equals("episode")?String.format(Locale.ROOT,"S%02d E%02d",sn,ep):year;
            double vote=row.optDouble("vote_average",0);
            if(vote>0)meta+=(meta.isEmpty()?"":"  ·  ")+String.format(Locale.ROOT,"%.1f / 10",vote);
            String poster=row.optString(kind.equals("episode")?"still_path":"poster_path","");
            items.add(new CatalogItem(kind,id,sn,ep,title,meta,row.optString("overview",""),CatalogItem.safePoster(poster)));
        }
        return new CatalogPage(items,kind.equals("season")||kind.equals("episode")?1:Math.max(1,Math.min(500,root.optInt("total_pages",1))));
    }
}
