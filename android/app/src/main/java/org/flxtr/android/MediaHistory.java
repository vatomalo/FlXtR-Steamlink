package org.flxtr.android;
import android.content.*;
import org.json.*;
import java.util.*;
final class MediaHistory {
    static final String PREFS="media-history";
    static synchronized void record(Context context,CatalogItem item,long position,boolean completed){
        if(item==null)return;SharedPreferences p=context.getSharedPreferences(PREFS,Context.MODE_PRIVATE);JSONArray old;
        try{old=new JSONArray(p.getString("items","[]"));}catch(Exception bad){old=new JSONArray();}
        JSONArray rows=new JSONArray();try{boolean wasCompleted=completed;
            for(int i=0;i<old.length();i++)if(CatalogItem.saved(old.getJSONObject(i)).key().equals(item.key())&&old.getJSONObject(i).optBoolean("completed"))wasCompleted=completed||position<5000;
            JSONObject row=item.json().put("position",position).put("completed",wasCompleted).put("when",System.currentTimeMillis());rows.put(row);for(int i=0;i<old.length()&&rows.length()<100;i++){JSONObject o=old.getJSONObject(i);if(!CatalogItem.saved(o).key().equals(item.key()))rows.put(o);}p.edit().putString("items",rows.toString()).apply();}catch(Exception ignored){}
    }
    static List<CatalogItem> list(Context context,boolean unfinished){ArrayList<CatalogItem> rows=new ArrayList<>();try{JSONArray array=new JSONArray(context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getString("items","[]"));for(int i=0;i<array.length();i++){JSONObject row=array.getJSONObject(i);if(!unfinished||(!row.optBoolean("completed")&&row.optLong("position")>5000))rows.add(CatalogItem.saved(row));}}catch(Exception ignored){}return rows;}
    static int[] cursor(Context context,int show){try{JSONArray array=new JSONArray(context.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getString("items","[]"));for(int i=0;i<array.length();i++){JSONObject row=array.getJSONObject(i);if(row.optInt("id")==show&&row.optString("kind").equals("episode")&&row.optBoolean("completed"))return new int[]{row.optInt("season"),row.optInt("episode")};}}catch(Exception ignored){}return new int[]{1,0};}
}
