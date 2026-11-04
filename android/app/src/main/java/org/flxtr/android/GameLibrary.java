package org.flxtr.android;

import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.AtomicFile;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;

/** Scans granted trees, then stages only the selected game's files for native cores. */
public final class GameLibrary {
    private final Context context;
    private final SharedPreferences prefs;
    public String scanMessage="";
    public GameLibrary(Context context){this.context=context.getApplicationContext();prefs=context.getSharedPreferences("retro-library",0);}
    private JSONArray array(String key)throws JSONException{return new JSONArray(prefs.getString(key,"[]"));}
    public static String hash(String value)throws Exception {StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(value.getBytes(StandardCharsets.UTF_8)))out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    private static void interrupted()throws InterruptedIOException {if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Scan cancelled");}
    public List<JSONObject> games()throws Exception {
        ArrayList<JSONObject> result=new ArrayList<>();HashSet<String> seen=new HashSet<>();
        for(String key:new String[]{"games","singles","index"}){JSONArray list=array(key);for(int i=0;i<list.length();i++){JSONObject game=list.getJSONObject(i);String core=GameFormats.core(game.optString("name"));if(core.isEmpty())continue;
            if(game.has("file")&&!new File(directory(),game.getString("file")).isFile())continue;
            if(seen.add(identity(game))){String chosen=context.getSharedPreferences("retro-options",0).getString("core:"+hash(identity(game)),"");if(GameFormats.compatible(game.optString("name"),chosen))core=chosen;game.put("core",core);result.add(game);}}}
        result.sort(Comparator.comparing(g->g.optString("name").toLowerCase(Locale.ROOT)));return result;
    }
    public File directory(){return new File(context.getFilesDir(),"games");}
    public List<String> folders()throws Exception {ArrayList<String> result=new ArrayList<>();JSONArray a=array("folders");for(int i=0;i<a.length();i++)result.add(a.getString(i));return result;}
    public void addFolder(Uri uri)throws Exception {JSONArray a=array("folders");for(int i=0;i<a.length();i++)if(a.getString(i).equals(uri.toString()))return;a.put(uri.toString());prefs.edit().putString("folders",a.toString()).commit();}
    public void removeFolder(String tree)throws Exception {
        JSONArray keep=new JSONArray();for(String value:folders())if(!value.equals(tree))keep.put(value);
        JSONArray index=new JSONArray(),old=array("index");for(int i=0;i<old.length();i++)if(!old.getJSONObject(i).optString("tree").equals(tree))index.put(old.get(i));
        prefs.edit().putString("folders",keep.toString()).putString("index",index.toString()).commit();
        try{context.getContentResolver().releasePersistableUriPermission(Uri.parse(tree),Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException ignored){}
    }
    public void scan()throws Exception {
        JSONArray output=new JSONArray(),old=array("index");ArrayList<String> errors=new ArrayList<>();
        for(String tree:folders()){
            interrupted();JSONArray found=new JSONArray();
            try{Uri root=Uri.parse(tree);walk(root,DocumentsContract.getTreeDocumentId(root),"",found,new HashSet<>(),0);for(int i=0;i<found.length();i++)output.put(found.get(i));}
            catch(InterruptedIOException stopped){throw stopped;}
            catch(Exception failure){errors.add(Uri.decode(Uri.parse(tree).getLastPathSegment())+": "+message(failure));for(int i=0;i<old.length();i++)if(old.getJSONObject(i).optString("tree").equals(tree))output.put(old.get(i));}
        }
        interrupted();prefs.edit().putString("index",output.toString()).commit();int count=games().size();
        scanMessage=count+" game"+(count==1?"":"s")+(errors.isEmpty()?" · Scan complete":" · Folder unavailable: "+String.join("; ",errors));
    }
    private void walk(Uri tree,String id,String parent,JSONArray output,Set<String> visited,int depth)throws Exception {
        walk(tree,id,parent,output,visited,depth,false);
    }
    private void walk(Uri tree,String id,String parent,JSONArray output,Set<String> visited,int depth,boolean assets)throws Exception {
        interrupted();if(depth>32||visited.size()>20000||output.length()>50000)throw new IOException("Folder is too large. Choose a smaller game folder.");if(!visited.add(id))return;
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        String[] columns={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE,DocumentsContract.Document.COLUMN_SIZE,DocumentsContract.Document.COLUMN_LAST_MODIFIED};
        try(Cursor c=context.getContentResolver().query(children,columns,null,null,null)){
            if(c==null)throw new IOException("Cannot read this folder. Choose it again.");
            while(c.moveToNext()){String name=c.getString(1);if(name!=null&&name.toLowerCase(Locale.ROOT).endsWith(".scummvm"))assets=true;}c.moveToPosition(-1);
            while(c.moveToNext()){interrupted();String child=c.getString(0),name=c.getString(1);if(name==null||name.contains("/")||name.equals(".")||name.equals(".."))continue;String path=GameFormats.relative(parent+name);
                if(DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))){walk(tree,child,path+"/",output,visited,depth+1,assets);continue;}
                String lower=name.toLowerCase(Locale.ROOT);if(!assets&&GameFormats.core(name).isEmpty()&&!lower.endsWith(".bin"))continue;
                output.put(entry(DocumentsContract.buildDocumentUriUsingTree(tree,child),name,c.isNull(3)?-1:c.getLong(3),c.isNull(4)?0:c.getLong(4)).put("tree",tree.toString()).put("path",path));
            }
        }
    }
    private JSONObject entry(Uri uri,String name,long size,long modified)throws JSONException{return new JSONObject().put("uri",uri.toString()).put("name",name).put("size",size).put("modified",modified).put("tree","").put("path",name);}
    public void addFile(Uri uri)throws Exception {
        String name=null;long size=-1;
        try(Cursor c=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE},null,null,null)){if(c!=null&&c.moveToFirst()){name=c.getString(0);if(!c.isNull(1))size=c.getLong(1);}}
        if(name==null||GameFormats.core(name).isEmpty())throw new IOException("Choose GB, GBC, NES, SNES, GBA, CHD, PBP or ISO files. For CUE/BIN and M3U discs, add their folder.");
        if(name.toLowerCase(Locale.ROOT).endsWith(".scummvm"))throw new IOException("Add the parent folder containing the ScummVM game and its launcher so all game data can be imported.");
        if(name.toLowerCase(Locale.ROOT).endsWith(".cue")||name.toLowerCase(Locale.ROOT).endsWith(".m3u"))throw new IOException("Add the containing folder so FlXtR can access every disc track.");
        GameFormats.relative(name);JSONObject game=entry(uri,name,size,0);JSONArray a=array("singles");for(int i=0;i<a.length();i++)if(identity(a.getJSONObject(i)).equals(identity(game)))return;a.put(game);prefs.edit().putString("singles",a.toString()).commit();
    }
    public void addScummVMFolder(Uri tree)throws Exception {
        String name="ScummVM game";Uri document=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));try(Cursor cursor=context.getContentResolver().query(document,new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME},null,null,null)){if(cursor!=null&&cursor.moveToFirst()&&!cursor.isNull(0))name=cursor.getString(0);}
        JSONObject game=entry(tree,name+".scummvm",0,0).put("tree",tree.toString()).put("adventureTree",true);JSONArray singles=array("singles");for(int index=0;index<singles.length();index++)if(identity(singles.getJSONObject(index)).equals(identity(game)))return;singles.put(game);prefs.edit().putString("singles",singles.toString()).commit();
    }
    public static String identity(JSONObject game){return game.has("file")?"legacy:"+game.optString("file"):game.optString("uri");}
    public String saveKey(JSONObject game)throws Exception{return game.has("file")?"":hash(identity(game));}
    public File stage(JSONObject game)throws Exception {
        if(game.optBoolean("adventureTree"))return stageScummVMFolder(game);
        if(game.has("file")){File file=new File(directory(),game.getString("file"));if(!file.getCanonicalFile().getParentFile().equals(directory().getCanonicalFile()))throw new IOException("Invalid game path");return file;}
        HashMap<String,JSONObject> entries=new HashMap<>();JSONArray index=array("index");for(int i=0;i<index.length();i++){JSONObject item=index.getJSONObject(i);if(item.optString("tree").equals(game.optString("tree")))entries.put(item.getString("path").toLowerCase(Locale.ROOT),item);}
        entries.put(game.getString("path").toLowerCase(Locale.ROOT),game);
        File root=new File(directory(),"imported/"+hash(game.optString("tree").isEmpty()?identity(game):game.getString("tree")));
        if(game.optString("name").toLowerCase(Locale.ROOT).endsWith(".scummvm")){
            String path=GameFormats.relative(game.getString("path"));int slash=path.lastIndexOf('/');if(slash<0)throw new IOException("Put the ScummVM launcher inside its game folder, then add the parent folder.");
            String prefix=path.substring(0,slash+1).toLowerCase(Locale.ROOT);long total=0;int count=0;
            for(JSONObject asset:entries.values())if(asset.getString("path").toLowerCase(Locale.ROOT).startsWith(prefix)){
                interrupted();if(++count>5000)throw new IOException("Too many ScummVM game files");File target=new File(root,GameFormats.relative(asset.getString("path")));if(!target.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("Invalid game asset path");
                long size=asset.optLong("size",-1);if(size<0)throw new IOException("Cannot determine game asset size");total+=size;if(total>8L*1024*1024*1024)throw new IOException("ScummVM game exceeds import limit");stageCached(asset,target,2L*1024*1024*1024);
            }
            return new File(root,path);
        }
        return stageOne(game,root,entries,new HashSet<>(),0);
    }
    private File stageScummVMFolder(JSONObject game)throws Exception {
        Uri tree=Uri.parse(game.getString("tree"));JSONArray assets=new JSONArray();walk(tree,DocumentsContract.getTreeDocumentId(tree),"adventure/",assets,new HashSet<>(),0,true);File root=new File(directory(),"imported/"+hash(identity(game)));File entry=null;long total=0;
        if(assets.length()>5000)throw new IOException("Too many ScummVM game files");
        for(int index=0;index<assets.length();index++){
            interrupted();JSONObject asset=assets.getJSONObject(index);String path=GameFormats.relative(asset.getString("path"));File target=new File(root,path);if(!target.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("Invalid game asset path");stageCached(asset,target,2L*1024*1024*1024);total+=target.length();if(total>8L*1024*1024*1024)throw new IOException("ScummVM game exceeds import limit");
            if(path.indexOf('/',"adventure/".length())<0&&(entry==null||path.toLowerCase(Locale.ROOT).endsWith(".scummvm")))entry=target;
        }
        if(entry==null)throw new IOException("Select the folder directly containing the ScummVM game data files");return entry;
    }
    private File stageOne(JSONObject item,File root,Map<String,JSONObject> entries,Set<String> visited,int depth)throws Exception {
        interrupted();if(depth>8||visited.size()>128)throw new IOException("Too many nested disc references.");String path=GameFormats.relative(item.getString("path"));File target=new File(root,path);
        if(!target.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("Invalid game path");
        if(!visited.add(path.toLowerCase(Locale.ROOT)))return target;
        stageCached(item,target,item.optString("core",GameFormats.core(item.optString("name"))).equals("dolphin")?8L*1024*1024*1024:2L*1024*1024*1024);
        String lower=path.toLowerCase(Locale.ROOT);
        if(lower.endsWith(".cue")||lower.endsWith(".m3u")){
            if(target.length()>1024*1024)throw new IOException("Disc descriptor is too large");
            for(String ref:GameFormats.references(path,new String(Files.readAllBytes(target.toPath()),StandardCharsets.UTF_8))){JSONObject track=entries.get(ref.toLowerCase(Locale.ROOT));if(track==null)throw new IOException("Missing disc file: "+ref+". Keep all tracks in the selected folder.");stageOne(track,root,entries,visited,depth+1);}
        }
        return target;
    }
    private void stageCached(JSONObject item,File target,long limit)throws Exception {
        File stamp=new File(context.getCacheDir(),"game-stamps/"+hash(identity(item)));String signature=item.optLong("size",-1)+":"+item.optLong("modified")+":"+identity(item);
        boolean cached=target.isFile()&&item.optLong("modified")>0&&stamp.isFile()&&new String(Files.readAllBytes(stamp.toPath()),StandardCharsets.UTF_8).equals(signature)&&target.length()==item.optLong("size",-1);
        if(!cached){copy(Uri.parse(item.getString("uri")),target,item.optLong("size",-1),limit);if(!stamp.getParentFile().isDirectory()&&!stamp.getParentFile().mkdirs())throw new IOException("Cannot save import status");Files.write(stamp.toPath(),signature.getBytes(StandardCharsets.UTF_8));}
    }
    public void importBios(Uri uri)throws Exception {
        String name=null;try(Cursor c=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst())name=c.getString(0);}
        if(name==null||!GameFormats.bios(name))throw new IOException("Select scph1001.bin, scph5500.bin, scph5501.bin, scph5502.bin, scph7001.bin or psxonpsp660.bin.");
        File target=new File(directory(),"system/"+name.toLowerCase(Locale.ROOT));copy(uri,target,512*1024,4*1024*1024);
    }
    private void copy(Uri uri,File target,long expected,long max)throws Exception {
        if(expected>max)throw new IOException("Game file exceeds this engine's import limit.");File parent=target.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Cannot create game storage");
        if(expected>0&&parent.getUsableSpace()<expected+32L*1024*1024)throw new IOException("Not enough free space to import this game.");
        AtomicFile file=new AtomicFile(target);FileOutputStream out=file.startWrite();
        try(InputStream in=context.getContentResolver().openInputStream(uri)){
            if(in==null)throw new IOException("Cannot read game file. Choose its folder again.");byte[] buffer=new byte[262144];long size=0;int n;
            while((n=in.read(buffer))!=-1){interrupted();size+=n;if(size>max)throw new IOException("Game file is too large");out.write(buffer,0,n);}
            if(size==0||expected>=0&&size!=expected)throw new IOException("File size changed or is invalid. Scan again.");file.finishWrite(out);
        }catch(Exception failure){file.failWrite(out);throw failure;}
    }
    private static String message(Exception failure){return failure.getMessage()==null?"Choose the folder again.":failure.getMessage();}
}
