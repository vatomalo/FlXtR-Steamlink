package org.flxtr.android;

import android.app.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import java.nio.charset.StandardCharsets;
import org.json.*;

/** Copy a ROM into the receiving device's system folder, independently of states. */
final class RomTransfer {
    static final long LIMIT=64L*1024*1024;
    static final String[] SYSTEMS={"NES","SNES","GB","GBC","GBA"};
    static String system(String name){
        String core=GameFormats.core(name);
        switch(core){case "fceumm":return "NES";case "snes9x":return "SNES";case "mgba":return "GBA";case "gambatte":return name.toLowerCase(Locale.ROOT).endsWith(".gbc")?"GBC":"GB";default:return "";}
    }
    static File archive(android.content.Context context,File rom,String name)throws Exception{
        String system=system(name);if(system.isEmpty())throw new IOException("Send to currently supports NES, SNES, GB, GBC and GBA ROM files");
        if(!rom.isFile()||rom.length()<=0||rom.length()>LIMIT)throw new IOException("ROM transfers support files up to 64 MiB");
        JSONObject metadata=new JSONObject().put("protocol",1).put("system",system).put("name",name).put("size",rom.length()).put("sha256",TvHandoff.hash(rom));
        File archive=File.createTempFile("send-rom-",".zip",context.getCacheDir());boolean complete=false;
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(archive))){
            zip.setLevel(0);zip.putNextEntry(new ZipEntry("rom.json"));zip.write(metadata.toString().getBytes(StandardCharsets.UTF_8));zip.closeEntry();
            zip.putNextEntry(new ZipEntry("rom.bin"));try(InputStream input=new FileInputStream(rom)){byte[] bytes=new byte[65536];int count;while((count=input.read(bytes))!=-1)zip.write(bytes,0,count);}zip.closeEntry();complete=true;
        }finally{if(!complete)archive.delete();}
        return archive;
    }
    static void send(Activity activity,GameLibrary library,JSONObject game){
        String name=game.optString("name"),system=system(name);
        if(system.isEmpty()){message(activity,"Send to","Send to currently supports single-file NES, SNES, GB, GBC and GBA ROMs");return;}
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("Send to device").setMessage("Finding ROM receivers over your local network and Tailscale…").setNegativeButton("Cancel",null).show();Gamepad.dialog(progress);
        TvHandoff.NETWORK.execute(()->{
            List<FlxtrDevice> receivers=new ArrayList<>();String error="";
            try{for(FlxtrDevice device:DeviceDiscovery.scan(activity))if(device.romReceiver)receivers.add(device);}catch(Exception unavailable){error=unavailable.getMessage();}
            String problem=error;
            activity.runOnUiThread(()->{
                if(!progress.isShowing()||activity.isFinishing()||activity.isDestroyed())return;progress.dismiss();
                if(receivers.isEmpty()){message(activity,"Send to device",problem.isEmpty()?"No ROM receivers found. Keep FlXtR open and updated on the target, using the same local network or Tailscale account.":problem);return;}
                String[] names=new String[receivers.size()];for(int i=0;i<names.length;i++)names[i]=receivers.get(i).name+" · "+receivers.get(i).transport;
                Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Send "+name+" to").setItems(names,(d,i)->transfer(activity,library,game,receivers.get(i))).setNegativeButton("Cancel",null).show());
            });
        });
    }
    private static void transfer(Activity activity,GameLibrary library,JSONObject game,FlxtrDevice device){
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("Send to "+device.name).setMessage("Preparing ROM…").setCancelable(false).show();Gamepad.dialog(progress);
        TvHandoff.NETWORK.execute(()->{File bundle=null;try{
            JSONObject capabilities=TvHandoff.request(device.endpoint,"capabilities",null);String name=game.getString("name"),system=system(name);
            if(capabilities.optInt("romTransfer")!=1)throw new IOException("Update FlXtR on the receiving device to support ROM files");
            JSONArray systems=capabilities.optJSONArray("romSystems");boolean supported=false;if(systems!=null)for(int i=0;i<systems.length();i++)if(system.equals(systems.optString(i)))supported=true;
            if(!supported)throw new IOException("This device does not accept "+system+" ROMs");
            File rom=library.stage(game);if(rom.length()>capabilities.optLong("maxRomBytes",0))throw new IOException("ROM exceeds this device's transfer limit");
            String checksum=TvHandoff.hash(rom);bundle=archive(activity,rom,name);
            activity.runOnUiThread(()->progress.setMessage("Sending "+name+" to "+device.name+" / "+system+"…"));
            JSONObject receipt=TvHandoff.request(device.endpoint,"roms",bundle);
            if(!"stored".equals(receipt.optString("status"))||!checksum.equals(receipt.optString("sha256"))||receipt.optLong("size")!=rom.length()||!system.equals(receipt.optString("system")))throw new IOException("The receiver did not confirm the complete ROM");
            String result=(receipt.optBoolean("alreadyPresent")?"Already on ":"Sent to ")+device.name+"\n"+receipt.optString("path",system+"/"+name);
            activity.runOnUiThread(()->{progress.dismiss();if(!activity.isFinishing())message(activity,"ROM stored",result);});
        }catch(Exception|OutOfMemoryError failure){activity.runOnUiThread(()->{progress.dismiss();if(!activity.isFinishing())message(activity,"Could not send ROM",failure.getMessage());});}
        finally{if(bundle!=null)bundle.delete();}});
    }
    private static void message(Activity activity,String title,String text){Gamepad.dialog(new AlertDialog.Builder(activity).setTitle(title).setMessage(text).setPositiveButton("OK",null).show());}
}
