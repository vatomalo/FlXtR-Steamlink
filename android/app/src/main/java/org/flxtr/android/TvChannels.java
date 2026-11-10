package org.flxtr.android;
import android.content.*;
import org.json.*;
import java.util.*;
import java.time.*;
final class TvChannels {
    static final String[] GENRES={"Mixed shows","Comedy","Crime / drama","Sci-fi","Animation","Action / adventure","Mystery","Sci-fi / adult animation","Documentary","Family","Reality","Kids","Soap","War / politics","Western","Talk show"};
    static final int[] HOURS={0,6,9,11,13,15,17,19,21,23},DEFAULT_GENRES={6,11,1,9,8,2,5,3,7,4};
    static final String[] FILTERS={"","35","80|9648|18","10765","16","10759","9648","16","99","10751","10764","10762","10766","10768","37","10767"};
    static int at(int[] hours,int hour){int chosen=0;for(int i=1;i<hours.length;i++)if(hour>=hours[i])chosen=i;return chosen;}
    static boolean valid(int[] hours){if(hours.length!=10||hours[0]!=0)return false;for(int i=0;i<10;i++)if(hours[i]<0||hours[i]>23||(i>0&&hours[i]<=hours[i-1]))return false;return true;}
    static double score(double rating,int votes,double popularity,double novelty,int wr,int wp,int wv){return Math.max(0,wr)*((rating*votes+650)/(votes+100.0))+Math.max(0,wp)*(popularity/(popularity+50)*10)+Math.max(0,wv)*novelty;}
    static CatalogPage list(Context context){ArrayList<CatalogItem> rows=new ArrayList<>();rows.add(new CatalogItem("tv-channel",100,0,0,"Scheduled TV","Original schedule · Europe/Oslo","","","scheduled"));for(int i=0;i<GENRES.length;i++)rows.add(new CatalogItem("tv-channel",i+1,0,0,GENRES[i],"Continuous TV channel","",""));return new CatalogPage(rows,1);}
    static int scheduled(SharedPreferences p){int[] hours=new int[10];for(int i=0;i<10;i++)hours[i]=p.getInt("tv-hour-"+i,HOURS[i]);if(!valid(hours))hours=HOURS;int slot=at(hours,ZonedDateTime.now(ZoneId.of("Europe/Oslo")).getHour());return Math.max(0,Math.min(15,p.getInt("tv-genre-"+slot,DEFAULT_GENRES[slot])));}
    static CatalogItem next(Context context,CatalogItem channel,StreamResolver.Request request) throws Exception {
        SharedPreferences prefs=context.getSharedPreferences("android-settings",Context.MODE_PRIVATE);int genre=channel.link.equals("scheduled")?scheduled(prefs):Math.max(0,channel.id-1);Random random=new Random();
        String station="tv-last-"+channel.id+"-"+genre;String last=prefs.getString(station,"");
        if(prefs.getBoolean("tv-marathon",true)&&!last.isEmpty())try{CatalogItem previous=CatalogItem.saved(new JSONObject(last));CatalogItem next=MediaSources.next(context,previous,request);if(next!=null){prefs.edit().putString(station,next.json().toString()).apply();return next;}}catch(Exception unavailable){request.check();}
        String filter=FILTERS[Math.min(15,genre)];
        JSONObject root=new JSONObject(MediaHttp.text(StreamProtocol.BASE+"/api/tmdb/discover/tv?include_adult=false&vote_count.gte=50&sort_by=popularity.desc&page="+(random.nextInt(5)+1)+(filter.isEmpty()?"":"&with_genres="+MediaHttp.encode(filter))+(genre==7?"&with_networks=80":""),request));JSONArray shows=root.getJSONArray("results");JSONObject chosen=null;double best=-Double.MAX_VALUE;
        for(int i=0;i<shows.length();i++){JSONObject show=shows.getJSONObject(i);double value=score(show.optDouble("vote_average"),show.optInt("vote_count"),show.optDouble("popularity"),random.nextDouble()*10,prefs.getInt("tv-rating",5),prefs.getInt("tv-popularity",2),prefs.getInt("tv-variety",4));if(value>best){best=value;chosen=show;}}
        if(chosen==null)throw new java.io.IOException("No TV shows available");int id=chosen.getInt("id");int[] cursor=MediaHistory.cursor(context,id);
        for(int attempt=0;attempt<2;attempt++){
            JSONObject season=new JSONObject(MediaHttp.text(StreamProtocol.BASE+"/api/tmdb/tv/"+id+"/season/"+cursor[0],request));CatalogPage episodes=CatalogPage.parse(season,"episode",id,cursor[0]);for(CatalogItem item:episodes.items)if(item.episode>cursor[1]){CatalogItem picked=new CatalogItem(item.kind,item.id,item.season,item.episode,chosen.optString("name")+" · "+item.title,item.meta,item.overview,CatalogItem.safePoster(chosen.optString("poster_path")));prefs.edit().putString(station,picked.json().toString()).apply();return picked;}
            cursor[0]++;cursor[1]=0;
        }throw new java.io.IOException("No unplayed episode available");
    }
}
