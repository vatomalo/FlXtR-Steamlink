package org.flxtr.android;
import android.app.Instrumentation;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import org.json.JSONObject;

public final class AdventureImportInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    private Context fixtureContext(){return new ContextWrapper(getTargetContext()){
        @Override public Context getApplicationContext(){return this;}
        @Override public SharedPreferences getSharedPreferences(String name,int mode){return super.getSharedPreferences("fixture-adventure-"+name,mode);}
        @Override public File getFilesDir(){File folder=new File(super.getFilesDir(),"adventure-import-fixture");folder.mkdirs();return folder;}
        @Override public File getCacheDir(){File folder=new File(super.getCacheDir(),"adventure-import-fixture");folder.mkdirs();return folder;}
    };}
    @Override public void onStart(){Bundle result=new Bundle();int outcome=-1;
        try{Context context=fixtureContext();context.getSharedPreferences("retro-library",0).edit().clear().commit();
            GameLibrary library=new GameLibrary(context);Uri tree=DocumentsContract.buildTreeDocumentUri("org.flxtr.android.testadventure","root");AdventureDocumentsProvider.generation=1;library.addScummVMFolder(tree);library.addScummVMFolder(tree);
            if(library.games().size()!=1)throw new IllegalStateException("Duplicate adventure folder");JSONObject game=library.games().get(0);if(!game.optBoolean("adventureTree")||!game.optString("core").equals("scummvm"))throw new IllegalStateException("ScummVM folder classification failed");
            File entry=library.stage(game),folder=entry.getParentFile();if(!new File(folder,"sky.dnr").isFile()||!new File(folder,"sky.dsk").isFile()||!new File(folder,"audio/voice.dat").isFile())throw new IllegalStateException("Incomplete game-data import");
            AdventureDocumentsProvider.generation=2;library.stage(game);String data=new String(Files.readAllBytes(new File(folder,"sky.dsk").toPath()),StandardCharsets.UTF_8);if(!data.endsWith(":2"))throw new IllegalStateException("Same-size changed game asset remained cached");result.putBoolean("folderImport",true);result.putBoolean("nestedAssets",true);result.putBoolean("changedAssets",true);
        }catch(Throwable failure){outcome=0;result.putString("error",failure.toString());}finally{AdventureDocumentsProvider.generation=1;}finish(outcome,result);
    }
}
