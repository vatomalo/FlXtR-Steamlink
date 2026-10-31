package org.flxtr.android;
import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.Executors;

/** Local ROM library. No game content is bundled or downloaded. */
public final class GamesActivity extends Activity {
    private LinearLayout root;
    private SharedPreferences prefs;
    @Override public void onCreate(Bundle state){super.onCreate(state);prefs=getSharedPreferences("retro-library",0);show();}
    private void show(){
        LibraryTheme theme=new LibraryTheme(this,getSharedPreferences("android-settings",0).getInt("theme",0));root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(20,12,20,12);root.setBackground(theme.background(this));
        TextView title=new TextView(this);title.setText("Libretro · Game Boy / Game Boy Color");title.setTextSize(19);title.setTextColor(theme.ink);title.setTypeface(theme.font);root.addView(title);
        TextView info=new TextView(this);info.setText("Gambatte runs inside FlXtR. Add an uncompressed .gb or .gbc game. Touch controls and gamepads are supported; saves stay on this device.");info.setTextColor(theme.muted);root.addView(info);
        Button add=new Button(this);add.setText("Add games");add.setOnClickListener(v->startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_ALLOW_MULTIPLE,true),90));root.addView(add);
        ScrollView scroll=new ScrollView(this);LinearLayout games=new LinearLayout(this);games.setOrientation(LinearLayout.VERTICAL);scroll.addView(games);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        try{JSONArray library=new JSONArray(prefs.getString("games","[]"));for(int i=0;i<library.length();i++){JSONObject game=library.getJSONObject(i);File file=new File(new File(getFilesDir(),"games"),game.getString("file"));if(!file.isFile())continue;Button row=new Button(this);row.setAllCaps(false);row.setText(game.getString("name"));row.setOnClickListener(v->startActivity(new Intent(this,RetroActivity.class).putExtra("rom",file.getAbsolutePath())));games.addView(row);}}catch(Exception ignored){}
        Button back=new Button(this);back.setText("Back to library");back.setOnClickListener(v->finish());root.addView(back);setContentView(root);
    }
    @Override protected void onActivityResult(int code,int result,Intent data){super.onActivityResult(code,result,data);if(code!=90||result!=RESULT_OK||data==null)return;ArrayList<Uri> uris=new ArrayList<>();if(data.getClipData()!=null)for(int i=0;i<Math.min(50,data.getClipData().getItemCount());i++)uris.add(data.getClipData().getItemAt(i).getUri());else if(data.getData()!=null)uris.add(data.getData());
        Toast.makeText(this,"Adding games…",Toast.LENGTH_SHORT).show();java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();worker.execute(()->{int added=0;try{JSONArray library=new JSONArray(prefs.getString("games","[]"));for(Uri uri:uris){try{JSONObject game=copy(uri);boolean exists=false;for(int i=0;i<library.length();i++)if(library.getJSONObject(i).getString("file").equals(game.getString("file")))exists=true;if(!exists){library.put(game);added++;}}catch(Exception invalid){}}prefs.edit().putString("games",library.toString()).apply();}catch(Exception ignored){}final int count=added;runOnUiThread(()->{if(!isFinishing()&&!isDestroyed()){show();Toast.makeText(this,count>0?"Added "+count+" game(s)":"Choose an uncompressed .gb or .gbc game (up to 16 MB).",Toast.LENGTH_LONG).show();}});worker.shutdown();});
    }
    private JSONObject copy(Uri uri)throws Exception{
        String name="";try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst())name=c.getString(0);}if(name==null||!name.toLowerCase(Locale.ROOT).matches(".*\\.(gb|gbc)$"))throw new IOException("Unsupported game");
        File folder=new File(getFilesDir(),"games");if(!folder.isDirectory()&&!folder.mkdirs())throw new IOException("Cannot create games folder");File temporary=File.createTempFile("import-",".part",folder);try{
            MessageDigest digest=MessageDigest.getInstance("SHA-256");long size=0;try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(temporary)){if(in==null)throw new IOException();byte[] buffer=new byte[65536];int n;while((n=in.read(buffer))!=-1){size+=n;if(size>16*1024*1024)throw new IOException("Game too large");digest.update(buffer,0,n);out.write(buffer,0,n);}}if(size<336)throw new IOException("Invalid game");
            StringBuilder hash=new StringBuilder();for(byte b:digest.digest())hash.append(String.format(Locale.US,"%02x",b&255));String suffix=name.toLowerCase(Locale.ROOT).endsWith(".gbc")?".gbc":".gb";File target=new File(folder,hash+suffix);if(!target.exists()&&!temporary.renameTo(target))throw new IOException("Cannot save game");return new JSONObject().put("file",target.getName()).put("name",name);
        }finally{temporary.delete();}
    }
}
