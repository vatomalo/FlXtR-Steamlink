package org.flxtr.android;

import android.app.Activity;
import android.app.Application;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
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

/** Verified startup updates with a standard Android installer fallback. */
final class AppUpdates {
    static final int INSTALL_PERMISSION=81;
    private static final ExecutorService worker=Executors.newSingleThreadExecutor();
    private static final java.util.concurrent.atomic.AtomicBoolean busy=new java.util.concurrent.atomic.AtomicBoolean();
    private static final java.util.concurrent.atomic.AtomicBoolean installing=new java.util.concurrent.atomic.AtomicBoolean();
    private static java.lang.ref.WeakReference<Activity> foreground=new java.lang.ref.WeakReference<>(null);
    private final Activity activity;
    private final SharedPreferences prefs;
    AppUpdates(Activity activity){this.activity=activity;prefs=activity.getSharedPreferences("app-updates",0);}
    static void installStartup(Application app){app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
        private int started;private boolean startup=true,configuration;
        public void onActivityStarted(Activity a){if(started++==0&&!configuration)startup=true;configuration=false;}
        public void onActivityResumed(Activity a){foreground=new java.lang.ref.WeakReference<>(a);if(startup&&a instanceof LibraryActivity){startup=false;a.getWindow().getDecorView().postDelayed(()->{if(foreground.get()==a&&!a.isFinishing()&&!a.isDestroyed())new AppUpdates(a).automatic();},1500);}}
        public void onActivityPaused(Activity a){if(foreground.get()==a)foreground.clear();}
        public void onActivityStopped(Activity a){started=Math.max(0,started-1);configuration=a.isChangingConfigurations();}
        public void onActivityCreated(Activity a,Bundle b){}public void onActivitySaveInstanceState(Activity a,Bundle b){}public void onActivityDestroyed(Activity a){}
    });}
    private boolean alive(){return !activity.isFinishing()&&!activity.isDestroyed();}
    private void ui(Runnable action){activity.runOnUiThread(()->{if(alive())action.run();});}
    private void message(String text){ui(()->Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("App updates").setMessage(text).setPositiveButton("OK",null).show()));}
    void menu(){
        String installed;try{installed=installed().versionName;}catch(Exception unavailable){installed="Android preview";}
        Gamepad.dialog(new AlertDialog.Builder(activity,R.style.FlxtrConsoleDialog).setTitle("FlXtR "+installed).setItems(new String[]{"Check for updates","Startup updates: "+(prefs.getBoolean("automatic",true)?"On":"Off")},(d,i)->{if(i==0)check(true);else{prefs.edit().putBoolean("automatic",!prefs.getBoolean("automatic",true)).apply();menu();}}).setNegativeButton("Close",null).show());
    }
    void automatic(){if(prefs.getBoolean("automatic",true))check(false);}
    private PackageInfo installed() throws Exception{return activity.getPackageManager().getPackageInfo(activity.getPackageName(),PackageManager.GET_SIGNATURES);}
    void check(boolean manual){
        if(installing.get()||!busy.compareAndSet(false,true)){if(manual)message("An update check or installation is already running.");return;}
        if(manual)android.widget.Toast.makeText(activity,"Checking for updates…",android.widget.Toast.LENGTH_SHORT).show();
        prefs.edit().putLong("checked",System.currentTimeMillis()).apply();
        worker.execute(()->{AndroidRelease found=null;String note=null;try{
            AndroidRelease release=new AndroidRelease(new JSONObject(MediaHttp.text(AndroidRelease.CHANNEL+"?check="+System.currentTimeMillis(),Collections.singletonMap("Cache-Control","no-cache"),16384,new StreamResolver.Request())));
            if(release.newer(installed().versionCode,Build.VERSION.SDK_INT))found=release;
            else if(manual)note=release.code>installed().versionCode?"The latest update requires a newer Android version. Your installed version is kept.":"You're up to date.";
        }catch(Exception unavailable){if(manual)note="Couldn't check for updates. Please try again later. Your installed version is kept.";}finally{busy.set(false);}if(found!=null){AndroidRelease next=found;prefs.edit().putString("available-release",next.name).apply();if(manual)ui(()->offer(next));else ui(()->{if(foreground.get()==activity)download(next);});}else if(note!=null)message(note);});
    }
    private void offer(AndroidRelease release){Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("FlXtR update available").setMessage(release.name+"\nDownload: "+Math.round(release.size/1048576.0)+" MB\n\nDownload and open Android's installer? Your library and settings will be kept.").setPositiveButton("Download",(d,i)->download(release)).setNegativeButton("Later",null).show());}
    private File directory(){File folder=new File(activity.getFilesDir(),"updates");if(!folder.isDirectory()&&!folder.mkdirs())throw new IllegalStateException("Cannot create update folder");return folder;}
    private void download(AndroidRelease release){
        if(installing.get()||!busy.compareAndSet(false,true))return;
        StreamResolver.Request request=new StreamResolver.Request(600);
        AlertDialog dialog=new AlertDialog.Builder(activity,R.style.FlxtrConsoleDialog).setTitle("Updating FlXtR").setMessage("Downloading and verifying the signed update. FlXtR will restart after installation. If root is unavailable, Android will ask you to tap Install.").setNegativeButton("Cancel",(d,i)->request.cancel()).create();
        dialog.setOnCancelListener(d->request.cancel());dialog.show();Gamepad.dialog(dialog);
        worker.execute(()->{File partial=null;try{
            File folder=directory();partial=new File(folder,"update.part");File apk=new File(folder,"update.apk");
            fetch(release,partial,request);verify(partial,release);
            if(apk.exists()&&!apk.delete())throw new IOException("Cannot replace old update");if(!partial.renameTo(apk))throw new IOException("Cannot save update");
            try(FileOutputStream out=new FileOutputStream(new File(folder,"release.json"))){out.write(new JSONObject().put("package","org.flxtr.android").put("versionCode",release.code).put("versionName",release.name).put("minSdk",release.minSdk).put("size",release.size).put("url",release.url).put("sha256",release.sha256).toString().getBytes(StandardCharsets.UTF_8));}
            request.check();prefs.edit().putBoolean("ready",true).apply();ui(()->{if(foreground.get()==activity)install();});
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
        if(!installing.compareAndSet(false,true))return;
        AlertDialog progress=new AlertDialog.Builder(activity,R.style.FlxtrConsoleDialog).setTitle("Installing FlXtR").setMessage("Installing the verified update. FlXtR will restart automatically.").setCancelable(false).show();
        worker.execute(()->{try{File folder=directory(),apk=new File(folder,"update.apk");AndroidRelease release=new AndroidRelease(new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(folder,"release.json").toPath()),StandardCharsets.UTF_8)));verify(apk,release);prefs.edit().putBoolean("ready",false).apply();boolean installed=false;try{installed=RootInstaller.install(apk);}catch(Exception denied){/* Keep Android's installer available without root. */}if(!installed)ui(()->{progress.dismiss();androidInstall();});else ui(()->activity.getWindow().getDecorView().postDelayed(()->{if(alive()){progress.dismiss();message("The update did not finish. Your installed version is kept. Check for updates to try again.");}},30000));}catch(Exception failure){prefs.edit().putBoolean("ready",false).apply();ui(progress::dismiss);message("Couldn't install this update. Your installed version is kept. Check for updates to try again.");}finally{installing.set(false);}});
    }
    private void androidInstall(){
        try{File folder=directory(),apk=new File(folder,"update.apk");AndroidRelease release=new AndroidRelease(new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(folder,"release.json").toPath()),StandardCharsets.UTF_8)));verify(apk,release);
            if(!activity.getPackageManager().canRequestPackageInstalls()){
                Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Allow FlXtR updates").setMessage("Enable ‘Allow from this source’ for FlXtR on the next screen, then return to install the update.").setPositiveButton("Settings",(d,i)->{try{activity.startActivityForResult(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+activity.getPackageName())),INSTALL_PERMISSION);}catch(Exception unavailable){message("Open Android Settings and allow FlXtR to install apps, then check for updates again.");}}).setNegativeButton("Later",null).show());return;
            }
            Uri uri=FileProvider.getUriForFile(activity,activity.getPackageName()+".updates",apk);
            activity.startActivity(new Intent(Intent.ACTION_VIEW).setDataAndType(uri,"application/vnd.android.package-archive").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION));
        }catch(Exception failure){message("Couldn't open the verified update. Check for updates to try again.");}
    }
}
