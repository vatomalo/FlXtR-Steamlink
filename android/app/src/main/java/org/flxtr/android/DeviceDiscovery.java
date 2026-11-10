package org.flxtr.android;

import android.app.*;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.widget.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

/** FlXtR announcements on local Wi-Fi/Ethernet and the embedded tailnet. */
final class DeviceDiscovery {
    private static final int PORT=43929;
    private static DatagramSocket announcer;
    private static WifiManager.MulticastLock multicast;

    static synchronized void startLocal(Context context){
        if(announcer!=null&&!announcer.isClosed())return;
        try{
            DatagramSocket server=new DatagramSocket(null);server.setReuseAddress(true);server.bind(new InetSocketAddress(PORT));announcer=server;
            try{WifiManager wifi=(WifiManager)context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);multicast=wifi.createMulticastLock("FlXtR devices");multicast.setReferenceCounted(false);multicast.acquire();}catch(Exception unavailable){multicast=null;}
            Thread worker=new Thread(()->{
                try{
                    String id=TailscaleRuntime.identity(context);
                    while(!server.isClosed()){
                        byte[] bytes=new byte[1024];DatagramPacket packet=new DatagramPacket(bytes,bytes.length);server.receive(packet);
                        if(!FlxtrDevice.lan(packet.getAddress().getHostAddress())||packet.getLength()>512)continue;
                        try{
                            JSONObject query=new JSONObject(new String(bytes,0,packet.getLength(),StandardCharsets.UTF_8));String nonce=query.optString("nonce");
                            if(!query.optString("app").equals("FlXtR")||query.optInt("protocol")!=1||!query.optString("op").equals("discover")||!nonce.matches("[0-9a-f]{32}"))continue;
                            JSONObject reply=new JSONObject().put("app","FlXtR").put("protocol",1).put("nonce",nonce).put("id",id).put("name",Build.MODEL).put("platform","android").put("roles",new JSONArray().put("sender"));
                            try(DatagramSocket route=new DatagramSocket()){
                                route.connect(packet.getAddress(),packet.getPort());JSONObject receiver=TailscaleRuntime.localAnnouncement(route.getLocalAddress().getHostAddress());
                                if(receiver!=null)reply=receiver.put("nonce",nonce).put("name",Build.MODEL);
                            }catch(Exception unavailable){}
                            byte[] data=reply.toString().getBytes(StandardCharsets.UTF_8);server.send(new DatagramPacket(data,data.length,packet.getAddress(),packet.getPort()));
                        }catch(JSONException malformed){}
                    }
                }catch(Exception closed){}
            },"FlXtR-LAN-discovery");worker.setDaemon(true);worker.start();
        }catch(SocketException unavailable){}
    }
    static synchronized void stopLocal(){
        if(announcer!=null){announcer.close();announcer=null;}
        if(multicast!=null){if(multicast.isHeld())multicast.release();multicast=null;}
    }
    private static void add(Map<String,FlxtrDevice> devices,FlxtrDevice device,String self){
        if(device.id.equals(self))return;
        devices.compute(device.id,(key,previous)->previous==null||device.transport.equals("Tailscale")?device:previous);
    }
    private static void local(Map<String,FlxtrDevice> devices,String self){
        String nonce=UUID.randomUUID().toString().replace("-","");
        try(DatagramSocket socket=new DatagramSocket()){
            socket.setBroadcast(true);socket.setSoTimeout(250);
            byte[] query=new JSONObject().put("app","FlXtR").put("protocol",1).put("op","discover").put("nonce",nonce).toString().getBytes(StandardCharsets.UTF_8);
            LinkedHashSet<String> targets=new LinkedHashSet<>();targets.add("255.255.255.255");
            try{
                Enumeration<NetworkInterface> all=NetworkInterface.getNetworkInterfaces();
                while(all!=null&&all.hasMoreElements())for(InterfaceAddress a:all.nextElement().getInterfaceAddresses())if(a.getBroadcast()!=null&&FlxtrDevice.lan(a.getAddress().getHostAddress()))targets.add(a.getBroadcast().getHostAddress());
            }catch(Exception unavailable){}
            // Android 10+ restricts this file. Broadcast discovery remains active.
            try{targets.addAll(FlxtrDevice.arpNeighbors(new String(Files.readAllBytes(new File("/proc/net/arp").toPath()),StandardCharsets.US_ASCII)));}catch(Exception restricted){}
            for(String address:targets){try{socket.send(new DatagramPacket(query,query.length,InetAddress.getByName(address),PORT));}catch(IOException unavailable){}}
            long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(2);
            while(System.nanoTime()<deadline&&!Thread.currentThread().isInterrupted()){
                try{
                    byte[] data=new byte[4096];DatagramPacket packet=new DatagramPacket(data,data.length);socket.receive(packet);
                    JSONObject reply=new JSONObject(new String(data,0,packet.getLength(),StandardCharsets.UTF_8));
                    if(!nonce.equals(reply.optString("nonce")))continue;
                    add(devices,FlxtrDevice.announcement(reply,packet.getAddress().getHostAddress(),false),self);
                }catch(SocketTimeoutException timeout){}catch(Exception invalid){}
            }
        }catch(Exception unavailable){}
    }
    static List<FlxtrDevice> scan(Context context)throws Exception{
        String self=TailscaleRuntime.identity(context);
        Map<String,FlxtrDevice> found=new ConcurrentHashMap<>();ExecutorService probes=Executors.newFixedThreadPool(4);
        try{
            probes.submit(()->local(found,self));
            try{
                JSONObject status=TailscaleRuntime.status(context);JSONArray peers=status.optJSONArray("peers");
                if(status.optString("state").equals("Running")&&peers!=null)for(int i=0;i<Math.min(peers.length(),128);i++){
                    JSONObject peer=peers.getJSONObject(i);String host=peer.optString("ip");if(!peer.optBoolean("online")||!FlxtrDevice.tailnet(host))continue;
                    probes.submit(()->{try{URI base=new URI("http",null,host,43927,"/",null,null);JSONObject reply=TailscaleRuntime.request(base,".well-known/flxtr",null);add(found,FlxtrDevice.announcement(reply,host,true),self);}catch(Exception notFlxtr){}});
                }
            }catch(Exception disconnected){}
            probes.shutdown();probes.awaitTermination(8,TimeUnit.SECONDS);
        }finally{probes.shutdownNow();}
        ArrayList<FlxtrDevice> result=new ArrayList<>(found.values());result.sort(Comparator.comparing(d->d.name.toLowerCase(Locale.ROOT)));return result;
    }
    static void remember(Context context,FlxtrDevice device){
        context.getSharedPreferences("tv-handoff",0).edit().putString("endpoint",device.endpoint.toString()).putString("device-id",device.id).putString("device-name",device.name).apply();
    }
    static String refreshSaved(Context context,String fallback){
        String id=context.getSharedPreferences("tv-handoff",0).getString("device-id","");if(id.isEmpty())return fallback;
        try{for(FlxtrDevice device:scan(context))if(device.id.equals(id)&&device.receiver){remember(context,device);return device.endpoint.toString();}}catch(Exception unavailable){}
        return fallback;
    }
    static void show(Activity activity,Runnable accepted){
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("FlXtR devices").setMessage("Finding FlXtR on your local network and Tailscale…").setNegativeButton("Cancel",null).show();Gamepad.dialog(progress);
        TvHandoff.NETWORK.execute(()->{
            List<FlxtrDevice> devices;
            try{devices=scan(activity);}catch(Exception unavailable){devices=new ArrayList<>();}
            List<FlxtrDevice> available=devices;
            activity.runOnUiThread(()->{
                if(activity.isFinishing()||activity.isDestroyed()||!progress.isShowing())return;progress.dismiss();
                AlertDialog.Builder builder=new AlertDialog.Builder(activity).setTitle("FlXtR devices").setNegativeButton("Close",null).setPositiveButton("Refresh",(d,w)->show(activity,accepted));
                String saved=activity.getSharedPreferences("tv-handoff",0).getString("device-id","");
                if(available.isEmpty())builder.setMessage("No FlXtR devices found. Keep FlXtR running on the other device and use the same local network or connect built-in Tailscale. Devices without FlXtR are hidden.");
                else{
                    String[] labels=new String[available.size()];for(int i=0;i<labels.length;i++){FlxtrDevice device=available.get(i);labels[i]=(device.id.equals(saved)?"★ ":"")+device.name+" · "+device.transport+(device.receiver?"":" · sender");}
                    ArrayAdapter<String> adapter=new ArrayAdapter<String>(activity,android.R.layout.select_dialog_item,labels){@Override public boolean areAllItemsEnabled(){return false;}@Override public boolean isEnabled(int position){return available.get(position).receiver;}};
                    builder.setAdapter(adapter,(d,index)->{FlxtrDevice selected=available.get(index);remember(activity,selected);if(accepted!=null)accepted.run();else Toast.makeText(activity,"Saved "+selected.name,Toast.LENGTH_LONG).show();});
                }
                builder.setNeutralButton("Options",(d,w)->TailscaleSetup.show(activity));Gamepad.dialog(builder.show());
            });
        });
    }
}
