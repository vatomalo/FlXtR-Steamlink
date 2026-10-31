package org.flxtr.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import org.json.JSONObject;
import java.io.*;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import javax.net.ssl.HttpsURLConnection;

/** Foreground checks and verified APK downloads; Android owns install consent. */
final class AppUpdates {
    static final int INSTALL_PERMISSION=81;
    private static final ExecutorService worker=Executors.newSingleThreadExecutor();
    private static final java.util.concurrent.atomic.AtomicBoolean busy=new java.util.concurrent.atomic.AtomicBoolean();
    private final Activity activity;
    private final SharedPreferences prefs;
    AppUpdates(Activity activity){this.activity=activity;prefs=activity.getSharedPreferences("app-updates",0);}
    private boolean alive(){return !activity.isFinishing()&&!activity.isDestroyed();}
    private void ui(Runnable action){activity.runOnUiThread(()->{if(alive())action.run();});}
    private void message(String text){ui(()->Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("App updates").setMessage(text).setPositiveButton("OK",null).show()));}
    void menu(){
        String installed;try{installed=installed().versionName;}catch(Exception unavailable){installed="Android preview";}
        Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("FlXtR "+installed).setItems(new String[]{"Check for updates","Automatic checks: "+(prefs.getBoolean("automatic",true)?"On":"Off")},(d,i)->{if(i==0)check(true);else{prefs.edit().putBoolean("automatic",!prefs.getBoolean("automatic",true)).apply();menu();}}).setNegativeButton("Close",null).show());
    }
    void automatic(){long now=System.currentTimeMillis(),last=prefs.getLong("checked",0);if(prefs.getBoolean("automatic",true)&&(now<last||now-last>=TimeUnit.DAYS.toMillis(1)))check(false);}
    private PackageInfo installed() throws Exception{return activity.getPackageManager().getPackageInfo(activity.getPackageName(),PackageManager.GET_SIGNATURES);}
    void check(boolean manual){
        if(!busy.compareAndSet(false,true)){if(manual)message("An update check or download is already running.");return;}
        if(manual)android.widget.Toast.makeText(activity,"Checking for updates…",android.widget.Toast.LENGTH_SHORT).show();
        prefs.edit().putLong("checked",System.currentTimeMillis()).apply();
        worker.execute(()->{try{
            AndroidRelease release=new AndroidRelease(new JSONObject(MediaHttp.text(AndroidRelease.CHANNEL,Collections.emptyMap(),16384,new StreamResolver.Request())));
            if(release.newer(installed().versionCode,Build.VERSION.SDK_INT))ui(()->offer(release));
            else if(manual)message(release.code>installed().versionCode?"The latest update requires a newer Android version. Your installed version is kept.":"You're up to date.");
        }catch(Exception unavailable){if(manual)message("Couldn't check for updates. Please try again later. Your installed version is kept.");}finally{busy.set(false);}});
    }
    private void offer(AndroidRelease release){Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("FlXtR update available").setMessage(release.name+"\nDownload: "+Math.round(release.size/1048576.0)+" MB\n\nDownload and open Android's installer? Your library and settings will be kept.").setPositiveButton("Download",(d,i)->download(release)).setNegativeButton("Later",null).show());}
    private File directory(){File folder=new File(activity.getFilesDir(),"updates");if(!folder.isDirectory()&&!folder.mkdirs())throw new IllegalStateException("Cannot create update folder");return folder;}
    private void download(AndroidRelease release){
        if(!busy.compareAndSet(false,true))return;
        StreamResolver.Request request=new StreamResolver.Request(600);
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("Downloading FlXtR").setMessage("Please keep FlXtR open. Android will ask before installing.").setNegativeButton("Cancel",(d,i)->request.cancel()).create();
        dialog.setOnCancelListener(d->request.cancel());dialog.show();Gamepad.dialog(dialog);
        worker.execute(()->{File partial=null;try{
            File folder=directory();partial=new File(folder,"update.part");File apk=new File(folder,"update.apk");
            fetch(release,partial,request);verify(partial,release);
            if(apk.exists()&&!apk.delete())throw new IOException("Cannot replace old update");if(!partial.renameTo(apk))throw new IOException("Cannot save update");
            try(FileOutputStream out=new FileOutputStream(new File(folder,"release.json"))){out.write(new JSONObject().put("package","org.flxtr.android").put("versionCode",release.code).put("versionName",release.name).put("minSdk",release.minSdk).put("size",release.size).put("url",release.url).put("sha256",release.sha256).toString().getBytes(StandardCharsets.UTF_8));}
            request.check();ui(this::install);
        }catch(Exception failure){if(!request.cancelled)message("The update couldn't be downloaded or verified. Please try again. Your installed version is kept.");}finally{if(partial!=null)partial.delete();busy.set(false);ui(dialog::dismiss);}});
    }
    private void fetch(AndroidRelease release,File destination,StreamResolver.Request request)throws Exception{
        String address=release.url;
        for(int hop=0;hop<6;hop++){
            URL url=new URL(address);if(!"https".equals(url.getProtocol()))throw new IOException("Insecure update redirect");
            HttpsURLConnection c=(HttpsURLConnection)url.openConnection();request.connection=c;c.setConnectTimeout(15000);c.setReadTimeout(15000);c.setInstanceFollowRedirects(false);
            try{request.check();int status=c.getResponseCode();if(status>=300&&status<400){String location=c.getHeaderField("Location");if(location==null)throw new IOException("Missing redirect");address=new URL(url,location).toString();continue;}if(status!=200)throw new IOException("Update unavailable");
                try(InputStream in=c.getInputStream();OutputStream out=new FileOutputStream(destination)){byte[] buffer=new byte[65536];long count=0;int n;while((n=in.read(buffer))!=-1){request.check();count+=n;if(count>release.size)throw new IOException("Update is too large");out.write(buffer,0,n);}if(count!=release.size)throw new IOException("Incomplete update");}return;
            }finally{request.connection=null;c.disconnect();}
        }throw new IOException("Too many redirects");
    }
    private void verify(File apk,AndroidRelease release)throws Exception{
        if(apk.length()!=release.size)throw new IOException("Incomplete update");MessageDigest digest=MessageDigest.getInstance("SHA-256");
        try(InputStream in=new FileInputStream(apk)){byte[] buffer=new byte[65536];int n;while((n=in.read(buffer))!=-1)digest.update(buffer,0,n);}
        StringBuilder hash=new StringBuilder();for(byte b:digest.digest())hash.append(String.format(Locale.US,"%02x",b&255));if(!release.sha256.equals(hash.toString()))throw new IOException("Update checksum mismatch");
        PackageInfo next=activity.getPackageManager().getPackageArchiveInfo(apk.getAbsolutePath(),PackageManager.GET_SIGNATURES),current=installed();
        if(next==null||!activity.getPackageName().equals(next.packageName)||next.versionCode!=release.code||!release.newer(current.versionCode,Build.VERSION.SDK_INT)||next.applicationInfo==null||next.applicationInfo.minSdkVersion>Build.VERSION.SDK_INT||!sameSignatures(current.signatures,next.signatures))throw new IOException("Incompatible update");
    }
    private static boolean sameSignatures(Signature[] a,Signature[] b){return a!=null&&b!=null&&a.length>0&&new HashSet<>(Arrays.asList(a)).equals(new HashSet<>(Arrays.asList(b)));}
    void install(){
        try{File folder=directory(),apk=new File(folder,"update.apk");AndroidRelease release=new AndroidRelease(new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(folder,"release.json").toPath()),StandardCharsets.UTF_8)));verify(apk,release);
            if(!activity.getPackageManager().canRequestPackageInstalls()){
                Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Allow FlXtR updates").setMessage("Enable ‘Allow from this source’ for FlXtR on the next screen, then return to install the update.").setPositiveButton("Settings",(d,i)->{try{activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+activity.getPackageName())),INSTALL_PERMISSION);}catch(Exception unavailable){message("Open Android Settings and allow FlXtR to install apps, then check for updates again.");}}).setNegativeButton("Later",null).show());return;
            }
            Uri uri=FileProvider.getUriForFile(activity,activity.getPackageName()+".updates",apk);
            activity.startActivity(new Intent(Intent.ACTION_VIEW).setDataAndType(uri,"application/vnd.android.package-archive").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION));
        }catch(Exception failure){message("Couldn't open the verified update. Check for updates to try again.");}
    }
}
