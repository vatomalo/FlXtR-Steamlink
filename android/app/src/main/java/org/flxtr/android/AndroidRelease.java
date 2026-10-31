package org.flxtr.android;

import org.json.JSONObject;
import java.io.IOException;
import java.net.URI;

/** The Android preview channel is independent of desktop release tags. */
final class AndroidRelease {
    static final String ROOT="https://github.com/vatomalo/FlXtR-Steamlink/releases/download/";
    static final String CHANNEL=ROOT+"android-latest/android.json";
    final int code,minSdk;
    final long size;
    final String name,url,sha256;
    AndroidRelease(JSONObject json) throws Exception {
        code=json.getInt("versionCode");minSdk=json.getInt("minSdk");size=json.getLong("size");
        name=json.getString("versionName");url=json.getString("url");sha256=json.getString("sha256");
        if(!json.getString("package").equals("org.flxtr.android")||code<1||minSdk<26||size<1||size>128L*1024*1024||name.length()>80||!sha256.matches("[a-f0-9]{64}")||!validUrl(url))throw new IOException("Invalid Android release");
    }
    static boolean validUrl(String value){
        try{URI u=new URI(value);return value.startsWith(ROOT+"android-")&&"https".equals(u.getScheme())&&"github.com".equals(u.getHost())&&u.getPort()==-1&&u.getUserInfo()==null&&u.getQuery()==null&&u.getFragment()==null&&u.getPath().endsWith(".apk")&&!u.getPath().contains("..");}catch(Exception invalid){return false;}
    }
    boolean newer(int installed,int sdk){return code>installed&&sdk>=minSdk;}
}
