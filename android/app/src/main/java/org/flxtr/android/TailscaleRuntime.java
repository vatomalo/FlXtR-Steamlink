package org.flxtr.android;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.os.Build;
import android.os.Bundle;
import flxtrnet.Client;
import flxtrnet.Flxtrnet;
import flxtrnet.Platform;
import go.Seq;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

/** Owns FlXtR's embedded node. No external VPN app or system-wide VPN is used. */
final class TailscaleRuntime {
    static final ScheduledExecutorService IO=Executors.newScheduledThreadPool(2);
    private static volatile Client client;
    private static Context application;
    private static ConnectivityManager.NetworkCallback callback;

    static boolean enabledForApp(){return application!=null&&enabled(application);}
    static boolean enabled(Context context){return context.getSharedPreferences("flxtr-network",0).getBoolean("tailnet-enabled",false);}
    static synchronized String identity(Context context)throws IOException{
        File dir=new File(context.getNoBackupFilesDir(),"tailnet");if(!dir.isDirectory()&&!dir.mkdirs())throw new IOException("Cannot create private network storage");
        File file=new File(dir,"flxtr-device-id");String id=file.isFile()?new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8).trim():"";
        if(!id.matches("[0-9a-f]{32}")){id=UUID.randomUUID().toString().replace("-","");try(FileOutputStream out=new FileOutputStream(file)){out.write((id+"\n").getBytes(StandardCharsets.UTF_8));}}
        return id;
    }
    static synchronized Client get(Context context)throws Exception{
        if(client!=null)return client;
        Context app=context.getApplicationContext();application=app;Seq.setContext(app);
        String id=identity(app),model=Build.MODEL.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9-]+","-");if(model.length()>32)model=model.substring(0,32);
        File dir=new File(app.getNoBackupFilesDir(),"tailnet");
        android.content.SharedPreferences network=app.getSharedPreferences("flxtr-network",0);
        // A native abort cannot run Java finally. Remember initialization until
        // it finishes, so the next process can recover without clearing data.
        network.edit().putBoolean("tailnet-starting",true).commit();
        try{client=Flxtrnet.newClient(dir.getAbsolutePath(),"flxtr-android-"+model+"-"+id.substring(0,6),new Platform(){
            public String interfaces(){return interfacesJson();}
            public String defaultInterface(){
                try{ConnectivityManager cm=(ConnectivityManager)app.getSystemService(Context.CONNECTIVITY_SERVICE);LinkProperties props=cm.getLinkProperties(cm.getActiveNetwork());return props==null||props.getInterfaceName()==null?"":props.getInterfaceName();}catch(Exception unavailable){return "";}
            }
        });}finally{network.edit().putBoolean("tailnet-starting",false).commit();}
        ConnectivityManager cm=(ConnectivityManager)app.getSystemService(Context.CONNECTIVITY_SERVICE);
        callback=new ConnectivityManager.NetworkCallback(){
            private void changed(){Client active=client;if(active!=null)IO.execute(()->{try{active.networkChanged();}catch(Exception ignored){}});}
            @Override public void onAvailable(Network network){changed();}
            @Override public void onLost(Network network){changed();}
            @Override public void onLinkPropertiesChanged(Network network,LinkProperties properties){changed();}
        };
        try{cm.registerDefaultNetworkCallback(callback);}catch(Exception unavailable){callback=null;}
        return client;
    }
    static String interfacesJson(){
        JSONArray result=new JSONArray();
        try{Enumeration<NetworkInterface> all=NetworkInterface.getNetworkInterfaces();while(all!=null&&all.hasMoreElements()){
            NetworkInterface n=all.nextElement();JSONArray addresses=new JSONArray();boolean broadcast=false;
            for(InterfaceAddress a:n.getInterfaceAddresses()){String host=a.getAddress().getHostAddress();if(host==null||a.getNetworkPrefixLength()<0)continue;int zone=host.indexOf('%');if(zone>=0)host=host.substring(0,zone);addresses.put(host+"/"+a.getNetworkPrefixLength());if(a.getBroadcast()!=null)broadcast=true;}
            int flags=(n.isUp()?1:0)+(broadcast?2:0)+(n.isLoopback()?4:0)+(n.isPointToPoint()?8:0)+(n.supportsMulticast()?16:0);
            result.put(new JSONObject().put("name",n.getName()).put("index",n.getIndex()).put("mtu",n.getMTU()).put("flags",flags).put("addresses",addresses));
        }}catch(Exception unavailable){}
        return result.toString();
    }
    static JSONObject status(Context context)throws Exception{
        if(!enabled(context))return new JSONObject().put("state","Disconnected").put("authUrl","").put("peers",new JSONArray());
        return new JSONObject(get(context).status());
    }
    static void connect(Context context)throws Exception{
        context.getSharedPreferences("flxtr-network",0).edit().putBoolean("tailnet-enabled",true).apply();
        Client active=get(context);active.connect();JSONObject state=new JSONObject(active.status());
        if(state.optString("state").equals("NeedsLogin")&&state.optString("authUrl").isEmpty())active.login();
    }
    static void disconnect(Context context,boolean logout)throws Exception{
        if(client!=null){if(logout)client.logout();else client.disconnect();}
        context.getSharedPreferences("flxtr-network",0).edit().putBoolean("tailnet-enabled",false).apply();
    }
    static JSONObject request(URI base,String route,File upload)throws Exception{
        Context context=application;
        if(context==null||!enabled(context))throw new IOException("Connect built-in Tailscale in FlXtR's network options");
        return new JSONObject(get(context).request(upload==null?"GET":"POST",base.toString()+route,upload==null?"":upload.getAbsolutePath()));
    }
    static void install(Application app){
        application=app;
        android.content.SharedPreferences network=app.getSharedPreferences("flxtr-network",0);
        if(network.getBoolean("tailnet-starting",false))network.edit().putBoolean("tailnet-enabled",false).putBoolean("tailnet-starting",false).commit();
        if(enabled(app))IO.execute(()->{try{connect(app);}catch(Exception ignored){}});
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
            private int started;
            public void onActivityStarted(Activity a){if(started++==0)DeviceDiscovery.startLocal(app);}
            public void onActivityStopped(Activity a){if(--started<=0){started=0;DeviceDiscovery.stopLocal();}}
            public void onActivityCreated(Activity a,Bundle b){}public void onActivityResumed(Activity a){}public void onActivityPaused(Activity a){}public void onActivitySaveInstanceState(Activity a,Bundle b){}public void onActivityDestroyed(Activity a){}
        });
    }
}
