package org.flxtr.android;
import android.app.Application;
public final class MediaApp extends Application {
    @Override public void onCreate(){super.onCreate();TailscaleRuntime.install(this);ConsoleMenu.installMoonlightNavigation(this);AppUpdates.installStartup(this);OfflineMedia.get(this);StreamResolver.extraBackend=(item,attempt,request,progress)->MediaSources.resolve(this,item,attempt,request,progress);}
}
