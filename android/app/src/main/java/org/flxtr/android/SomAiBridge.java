package org.flxtr.android;

import android.app.*;
import android.content.*;
import android.widget.*;
import java.io.File;
import org.json.*;

final class SomAiBridge {
    private SomAiBridge(){}
    static void show(Activity activity){
        EditText peer=new EditText(activity);peer.setSingleLine(true);peer.setText(activity.getSharedPreferences("som-ai-bridge",0).getString("peer","100.69.7.81"));peer.setHint("SkyLoft Tailscale IPv4");
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("Secret of Mana AI bridge").setMessage("Allow one SkyLoft Tailscale peer to read live WRAM and send allowlisted controls. The private endpoint is copied after setup.").setView(peer).setNegativeButton("Cancel",null).setPositiveButton("Configure",null).create();
        dialog.setOnShowListener(unused->dialog.getButton(-1).setOnClickListener(view->{String allowed=peer.getText().toString().trim();dialog.getButton(-1).setEnabled(false);TailscaleRuntime.IO.execute(()->{try{
            TailscaleRuntime.connect(activity);flxtrnet.Client client=TailscaleRuntime.get(activity);client.setSOMDirectory(new File(activity.getFilesDir(),"som-ai").getCanonicalPath(),allowed);activity.getSharedPreferences("som-ai-bridge",0).edit().putString("peer",allowed).apply();
            JSONObject state=new JSONObject(client.status()),bridge=new JSONObject(client.somStatus());JSONArray ips=state.optJSONArray("ips");String host="";if(ips!=null)for(int i=0;i<ips.length();i++)if(ips.optString(i).matches("100\\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\\..*")){host=ips.optString(i);break;}if(host.isEmpty())throw new java.io.IOException("Tailscale is not connected yet");String endpoint="http://"+host+":43927/"+bridge.getString("token")+"/som";
            ((ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("FlXtR SoM AI endpoint",endpoint));String message="SkyLoft endpoint copied:\n\n"+endpoint+"\n\nSet SOM_AI_HTTP_ENDPOINT to this value and restart flxtr-som-ai.service.";activity.runOnUiThread(()->{dialog.dismiss();Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("AI bridge ready").setMessage(message).setPositiveButton("OK",null).show());});
        }catch(Exception|LinkageError error){activity.runOnUiThread(()->{dialog.getButton(-1).setEnabled(true);Toast.makeText(activity,error.getMessage(),Toast.LENGTH_LONG).show();});}});}));dialog.show();Gamepad.dialog(dialog);
    }
}
