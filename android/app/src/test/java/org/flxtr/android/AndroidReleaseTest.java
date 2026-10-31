package org.flxtr.android;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;

public class AndroidReleaseTest {
    private JSONObject valid()throws Exception{return new JSONObject().put("package","org.flxtr.android").put("versionCode",7).put("versionName","0.7-android-preview").put("minSdk",26).put("size",42000000).put("sha256",new String(new char[64]).replace('\0','a')).put("url",AndroidRelease.ROOT+"android-7/FlXtR-Android.apk");}
    @Test public void updatesRequireNewerCompatibleVersion()throws Exception{AndroidRelease r=new AndroidRelease(valid());assertTrue(r.newer(6,26));assertFalse(r.newer(7,26));assertFalse(r.newer(8,26));assertFalse(r.newer(6,25));}
    @Test public void acceptsOnlyAndroidRepositoryReleaseAssets(){assertTrue(AndroidRelease.validUrl(AndroidRelease.ROOT+"android-7/app.apk"));for(String bad:new String[]{"http://github.com/vatomalo/FlXtR-Steamlink/releases/download/android-7/app.apk",AndroidRelease.ROOT+"linux-latest/app.apk",AndroidRelease.ROOT+"android-7/../app.apk",AndroidRelease.ROOT+"android-7/app.apk?x=1",AndroidRelease.ROOT+"android-7/app.apk#x","https://github.com.evil.test/app.apk","https://other.test/app.apk"})assertFalse(bad,AndroidRelease.validUrl(bad));}
    @Test public void rejectsWrongPackageChecksumAndUnboundedSize()throws Exception{for(String field:new String[]{"package","sha256","size","versionCode"}){JSONObject j=valid();j.put(field,field.equals("size")?200000000:field.equals("versionCode")?0:"invalid");try{new AndroidRelease(j);fail(field);}catch(java.io.IOException expected){}}}
}
