package org.flxtr.android;

import android.app.*;
import android.widget.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.zip.*;
import org.json.*;

/** Explicitly paired LAN transport, without changing global cleartext policy. */
final class TvHandoff {
    static final String SOURCE="gambatte:77f2567d718b8973d02caa2daa8dce4953faf811";
    static final ExecutorService NETWORK=Executors.newSingleThreadExecutor();
    interface Payload { File capture() throws Exception; }
    static URI endpoint(String value)throws Exception {
        URI uri=new URI(value.trim());String host=uri.getHost();
        if(!"http".equals(uri.getScheme())||host==null||uri.getUserInfo()!=null||uri.getQuery()!=null||uri.getFragment()!=null||uri.getPort()<1||uri.getPort()>65535||!uri.getPath().matches("/[0-9a-f]{32}/"))throw new IOException("Paste the full pairing address shown on the TV");
        if(!host.matches("[0-9]{1,3}(\\.[0-9]{1,3}){3}"))throw new IOException("Use the TV's LAN or Tailscale IP address");
        byte[] ip=InetAddress.getByName(host).getAddress();int a=ip[0]&255,b=ip[1]&255;
        if(!(a==10||a==192&&b==168||a==172&&b>=16&&b<=31||a==100&&b>=64&&b<=127||a==127))throw new IOException("Pairing requires a private LAN or Tailscale address");
        return uri;
    }
    static JSONObject request(URI base,String route,File upload)throws Exception {
        // Raw sockets are intentionally limited to the validated paired address.
        // HTTPS media and the rest of the app retain their existing policies.
        endpoint(base.toString());try(Socket socket=new Socket()){
            socket.connect(new InetSocketAddress(base.getHost(),base.getPort()),10000);socket.setSoTimeout(120000);
            OutputStream out=socket.getOutputStream();String host=base.getHost()+":"+base.getPort();
            String head=(upload==null?"GET ":"POST ")+base.getPath()+route+" HTTP/1.0\r\nHost: "+host+"\r\nConnection: close\r\n"+(upload==null?"":"Content-Type: application/octet-stream\r\nContent-Length: "+upload.length()+"\r\n")+"\r\n";
            out.write(head.getBytes(StandardCharsets.US_ASCII));if(upload!=null)try(InputStream input=new FileInputStream(upload)){byte[] buffer=new byte[65536];int count;while((count=input.read(buffer))!=-1){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException();out.write(buffer,0,count);}}out.flush();
            InputStream input=socket.getInputStream();ByteArrayOutputStream header=new ByteArrayOutputStream();int state=0;while(state!=4){int c=input.read();if(c<0||header.size()>8192)throw new IOException("Invalid TV response");header.write(c);state=(state==0||state==2)?c=='\r'?state+1:0:c=='\n'?state+1:0;}
            String status=header.toString("US-ASCII").split("\r\n",2)[0];ByteArrayOutputStream body=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int count;while((count=input.read(buffer))!=-1){if(body.size()+count>32768)throw new IOException("TV response too large");body.write(buffer,0,count);}
            JSONObject response=new JSONObject(body.toString("UTF-8"));if(!status.matches("HTTP/1\\.[01] 20[02] .*"))throw new IOException(response.optString("error","TV rejected the session"));return response;
        }
    }
    static String hash(File file)throws Exception {MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream input=new FileInputStream(file)){byte[] bytes=new byte[65536];int count;while((count=input.read(bytes))!=-1)digest.update(bytes,0,count);}return hex(digest.digest());}
    static String hex(byte[] bytes){StringBuilder value=new StringBuilder();for(byte b:bytes)value.append(String.format(Locale.ROOT,"%02x",b&255));return value.toString();}
    static File archive(android.content.Context context,JSONObject metadata,byte[] state)throws Exception {
        File result=File.createTempFile("tv-session-",".zip",context.getCacheDir());boolean complete=false;
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(result))){zip.setLevel(0);zip.putNextEntry(new ZipEntry("session.json"));zip.write(metadata.toString().getBytes(StandardCharsets.UTF_8));zip.closeEntry();if(state!=null){zip.putNextEntry(new ZipEntry("state.bin"));zip.write(state);zip.closeEntry();}complete=true;}finally{if(!complete)result.delete();}return result;
    }
    static void pair(Activity activity,String proposed,Runnable accepted){
        EditText input=new EditText(activity);input.setSingleLine(true);input.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_URI);input.setText(proposed==null?activity.getSharedPreferences("tv-handoff",0).getString("endpoint",""):proposed);input.setHint("http://TV-IP:PORT/pairing-code/");
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("Pair TV").setMessage("Open Settings > Services > Continue on TV on Steam Link. Scan its QR code or paste the full address here.").setView(input).setNegativeButton("Cancel",null).setPositiveButton("Pair",null).create();
        dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{String value=input.getText().toString();NETWORK.execute(()->{try{URI base=endpoint(value);JSONObject capabilities=request(base,"capabilities",null);if(capabilities.optInt("protocol")!=1||!capabilities.optBoolean("armed"))throw new IOException("Open Continue on TV on the TV first");activity.getSharedPreferences("tv-handoff",0).edit().putString("endpoint",base.toString()).apply();activity.runOnUiThread(()->{dialog.dismiss();if(accepted!=null)accepted.run();});}catch(Exception failure){activity.runOnUiThread(()->input.setError(failure.getMessage()));}});}));dialog.show();Gamepad.dialog(dialog);
    }
    static void send(Activity activity,String source,Payload capture,Runnable success,Runnable failure){
        String address=activity.getSharedPreferences("tv-handoff",0).getString("endpoint","");
        if(address.isEmpty()){pair(activity,null,()->send(activity,source,capture,success,failure));return;}
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("Continue on TV").setMessage("Connecting to TV…").setCancelable(false).create();progress.show();
        NETWORK.execute(()->{File payload=null;try{
            URI base=endpoint(address);JSONObject capabilities=request(base,"capabilities",null);if(!capabilities.optBoolean("armed"))throw new IOException("Open Continue on TV on the TV first");
            if(!source.isEmpty()){JSONArray sources=capabilities.optJSONArray("sources");boolean found=false;if(sources!=null)for(int i=0;i<sources.length();i++)if(source.equals(sources.optString(i)))found=true;if(!found)throw new IOException("This TV has no matching portable core. Your game stays here.");}
            payload=capture.capture();activity.runOnUiThread(()->progress.setMessage("Sending session…"));JSONObject response=request(base,"handoff",payload);String id=response.getString("id");if(!id.matches("[0-9a-f]{32}"))throw new IOException("Invalid session receipt");
            activity.runOnUiThread(()->progress.setMessage("Waiting for the TV to resume…"));long deadline=System.currentTimeMillis()+90000;
            while(System.currentTimeMillis()<deadline){JSONObject status=request(base,"status/"+id,null);if(status.optString("status").equals("started")){activity.runOnUiThread(()->{progress.dismiss();new AlertDialog.Builder(activity).setTitle("Playing on TV").setMessage("The TV confirmed playback. Pause or exit on this device?").setPositiveButton("Pause / exit here",(d,w)->success.run()).setNegativeButton("Keep playing here",(d,w)->failure.run()).show();});return;}if(status.optString("status").equals("failed"))throw new IOException(status.optString("error","TV could not resume"));Thread.sleep(700);}
            throw new IOException("The TV has not confirmed playback. Your session is kept here; check the TV before retrying.");
        }catch(Exception|OutOfMemoryError error){activity.runOnUiThread(()->{progress.dismiss();failure.run();new AlertDialog.Builder(activity).setTitle("Could not continue on TV").setMessage(error.getMessage()).setPositiveButton("OK",null).setNeutralButton("Pair another TV",(d,w)->pair(activity,null,null)).show();});}finally{if(payload!=null)payload.delete();}});
    }
}
