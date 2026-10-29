package org.flxtr.android;
import android.app.Application;
public final class MediaApp extends Application {
    @Override public void onCreate(){super.onCreate();OfflineMedia.get(this);StreamResolver.extraBackend=(item,attempt,request,progress)->MediaSources.resolve(this,item,attempt,request,progress);}
}
