package org.flxtr.android;
import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.provider.DocumentsContract;
import org.json.*;
import java.io.*;
import java.nio.file.Files;
import java.util.*;

/** Exercises storage discovery, caching, recovery and actual core loading. */
public final class ConsoleSmokeInstrumentation extends Instrumentation {
 @Override public void onCreate(Bundle args){super.onCreate(args);start();}
 @Override public void onStart(){Bundle result=new Bundle();SharedPreferences prefs=getTargetContext().getSharedPreferences("retro-library",0);Map<String,String> before=new HashMap<>();String[] keys={"folders","index","games","singles"};for(String key:keys)before.put(key,prefs.contains(key)?prefs.getString(key,""):null);
  int outcome=-1;try{getUiAutomation();for(String core:new String[]{"fceumm","snes9x","mgba","pcsx_rearmed"}){String path=new File(getTargetContext().getApplicationInfo().nativeLibraryDir,"libflxtr_"+core+".so").getAbsolutePath();String info=RetroActivity.coreInfo(path);if(info==null||info.trim().isEmpty())throw new IOException("Cannot load "+core);result.putString(core,info);}
   SharedPreferences.Editor e=prefs.edit();for(String key:keys)e.putString(key,"[]");e.commit();
   GameTestDocumentsProvider.generation=1;GameLibrary library=new GameLibrary(getTargetContext());Uri tree=DocumentsContract.buildTreeDocumentUri("org.flxtr.android.testgames","root");library.addFolder(tree);library.scan();if(library.games().size()!=4)throw new IOException("Recursive discovery failed: "+library.scanMessage);library.scan();if(library.games().size()!=4)throw new IOException("Duplicate scan entries");result.putBoolean("recursiveScan",true);
   JSONObject cartridge=null,cue=null,playlist=null;for(JSONObject game:library.games()){if(game.getString("name").endsWith(".nes"))cartridge=game;if(game.getString("name").endsWith(".cue"))cue=game;if(game.getString("name").endsWith(".m3u"))playlist=game;}
   File first=library.stage(cartridge);if(Files.readAllBytes(first.toPath())[0]!=1)throw new IOException("Wrong imported file");
   File disc=library.stage(cue);if(!new File(disc.getParentFile(),"Track 01.bin").isFile())throw new IOException("CUE tracks were not imported");library.stage(playlist);result.putBoolean("discDependencies",true);
   GameTestDocumentsProvider.generation=2;library.scan();if(library.games().size()!=5)throw new IOException("New games not discovered");for(JSONObject game:library.games())if(game.getString("name").endsWith(".nes"))cartridge=game;File changed=library.stage(cartridge);if(Files.readAllBytes(changed.toPath())[0]!=2)throw new IOException("Changed file remained cached");result.putBoolean("newAndChangedGames",true);
   GameTestDocumentsProvider.generation=3;library.scan();boolean rejected=false;try{library.stage(cue);}catch(IOException missing){rejected=true;}if(!rejected)throw new IOException("Missing track was ignored");result.putBoolean("missingTrackRejected",true);
   GameTestDocumentsProvider.generation=4;int count=library.games().size();library.scan();if(library.games().size()!=count||!library.scanMessage.contains("unavailable"))throw new IOException("Unavailable folder lost library");result.putBoolean("unavailableFolderPreserved",true);
  }catch(Throwable error){result.putString("error",error.getClass().getSimpleName()+": "+error.getMessage());outcome=0;}
  finally{GameTestDocumentsProvider.generation=1;SharedPreferences.Editor e=prefs.edit();for(String key:keys){if(before.get(key)==null)e.remove(key);else e.putString(key,before.get(key));}e.commit();}
  finish(outcome,result);
 }
}
