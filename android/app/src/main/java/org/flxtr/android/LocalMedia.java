package org.flxtr.android;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import org.json.*;
import java.util.*;
final class LocalMedia {
    static List<CatalogItem> list(Context c){ArrayList<CatalogItem> rows=new ArrayList<>();try{JSONArray array=new JSONArray(c.getSharedPreferences("local-media",Context.MODE_PRIVATE).getString("items","[]"));for(int i=0;i<array.length();i++)rows.add(CatalogItem.saved(array.getJSONObject(i)));}catch(Exception ignored){}return rows;}
    static void add(Context context,Intent result){
        ArrayList<Uri> uris=new ArrayList<>();if(result.getClipData()!=null){for(int i=0;i<result.getClipData().getItemCount();i++)uris.add(result.getClipData().getItemAt(i).getUri());}else if(result.getData()!=null)uris.add(result.getData());
        List<CatalogItem> rows=list(context);
        for(Uri uri:uris){if(!"content".equals(uri.getScheme()))continue;try{context.getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException notPersistable){}
            String name=uri.getLastPathSegment();try(Cursor cursor=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(cursor!=null&&cursor.moveToFirst())name=cursor.getString(0);}catch(Exception unavailable){}
            if(name==null)name="Local video";CatalogItem item=new CatalogItem("local",0,0,0,name,"On this device","","",uri.toString());rows.removeIf(old->old.key().equals(item.key()));rows.add(item);
        }
        JSONArray array=new JSONArray();try{for(int i=0;i<Math.min(rows.size(),500);i++)array.put(rows.get(i).json());context.getSharedPreferences("local-media",Context.MODE_PRIVATE).edit().putString("items",array.toString()).apply();}catch(Exception ignored){}
    }
}
