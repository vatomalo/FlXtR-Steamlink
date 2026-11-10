package org.flxtr.android;
import android.app.Instrumentation;
import android.os.Bundle;
import java.io.File;

public final class EngineCoreInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){Bundle result=new Bundle();int outcome=-1;for(String core:new String[]{"ppsspp","scummvm","dolphin"}){
        try{String info=RetroActivity.coreInfo(new File(getTargetContext().getApplicationInfo().nativeLibraryDir,"libflxtr_"+core+".so").getAbsolutePath());if(info==null||info.trim().isEmpty())throw new IllegalStateException("Core could not load");result.putString(core,info);}catch(Throwable failure){outcome=0;result.putString(core,"FAILED: "+failure);}
    }finish(outcome,result);}
}
