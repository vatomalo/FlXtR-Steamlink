package org.flxtr.android;

import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.widget.*;
import java.util.concurrent.*;
import org.json.JSONObject;

/** Account setup for the node embedded in FlXtR. */
final class TailscaleSetup {
    static String actionLabel(Context context){return "Built-in Tailscale";}
    static void show(Activity activity){
        Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Network and devices")
            .setItems(new String[]{"Available FlXtR devices","Built-in Tailscale","Secret of Mana AI bridge","Saved device","ADB through Tailscale"},(d,i)->{
                if(i==0)DeviceDiscovery.show(activity,null);else if(i==1)open(activity);else if(i==2)SomAiBridge.show(activity);else if(i==3)savedTv(activity);else AdbSetup.show(activity);
            }).setNegativeButton("Close",null).show());
    }
    static void savedTv(Activity activity){
        android.content.SharedPreferences prefs=activity.getSharedPreferences("tv-handoff",0);
        if(prefs.getString("endpoint","").isEmpty()){DeviceDiscovery.show(activity,null);return;}
        Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Saved device: "+prefs.getString("device-name","TV"))
            .setItems(new String[]{"Check connection","Choose another device","Forget device"},(d,i)->{
                if(i==0)checkTv(activity);else if(i==1)DeviceDiscovery.show(activity,null);
                else{prefs.edit().remove("endpoint").remove("device-id").remove("device-name").apply();Toast.makeText(activity,"Saved device removed",Toast.LENGTH_LONG).show();}
            }).setNegativeButton("Close",null).show());
    }
    static void open(Activity activity){
        TextView text=new TextView(activity);text.setPadding(32,24,32,24);text.setText("Checking built-in Tailscale…");
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("Built-in Tailscale").setView(text)
            .setPositiveButton("Connect",null).setNeutralButton("Options",null).setNegativeButton("Close",null).create();
        Handler ui=new Handler(Looper.getMainLooper());String[] auth={""};boolean[] busy={false};
        Runnable[] poll={null};
        poll[0]=()->{
            if(!dialog.isShowing()||activity.isFinishing()||activity.isDestroyed())return;
            if(busy[0]){ui.postDelayed(poll[0],1000);return;}busy[0]=true;
            TailscaleRuntime.IO.execute(()->{
                String message;String url="";
                try{JSONObject state=TailscaleRuntime.status(activity);url=state.optString("authUrl");message="Status: "+state.optString("state")+"\n\nConnect and sign in with the same Tailscale account as your TV and SkyLoft. This connection is built into FlXtR. Your saved device stays remembered while offline.";}
                catch(Exception|LinkageError error){message="Could not start Tailscale: "+error.getMessage();}
                String update=message,login=url;
                ui.post(()->{busy[0]=false;if(!dialog.isShowing())return;auth[0]=login;text.setText(update);dialog.getButton(-1).setText(login.isEmpty()?"Connect":"Sign in");ui.postDelayed(poll[0],3000);});
            });
        };
        dialog.setOnShowListener(d->{
            dialog.getButton(-1).setOnClickListener(v->{
                if(!auth[0].isEmpty()){login(activity,auth[0]);return;}
                text.setText("Connecting…");TailscaleRuntime.IO.execute(()->{try{TailscaleRuntime.connect(activity);}catch(Exception|LinkageError error){ui.post(()->text.setText(error.getMessage()));}});
            });
            dialog.getButton(-3).setOnClickListener(v->Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Tailscale options")
                .setItems(new String[]{"Disconnect","Sign out"},(o,i)->TailscaleRuntime.IO.execute(()->{try{TailscaleRuntime.disconnect(activity,i==1);}catch(Exception error){ui.post(()->text.setText(error.getMessage()));}})).setNegativeButton("Cancel",null).show()));
            poll[0].run();
        });
        dialog.setOnDismissListener(d->ui.removeCallbacksAndMessages(null));dialog.show();Gamepad.dialog(dialog);
    }
    private static void login(Activity activity,String value){
        Uri url=Uri.parse(value);String host=url.getHost();
        if(!"https".equals(url.getScheme())||host==null||!(host.equals("tailscale.com")||host.endsWith(".tailscale.com"))||url.getUserInfo()!=null)return;
        try{activity.startActivity(new Intent(Intent.ACTION_VIEW,url));}
        catch(ActivityNotFoundException unavailable){
            ((ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Tailscale sign-in",value));
            Toast.makeText(activity,"Sign-in link copied. Open it in a browser.",Toast.LENGTH_LONG).show();
        }
    }
    private static void checkTv(Activity activity){
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("Saved device").setMessage("Checking connection…").setNegativeButton("Close",null).show();Gamepad.dialog(progress);
        TvHandoff.NETWORK.execute(()->{
            String message;
            try{String old=activity.getSharedPreferences("tv-handoff",0).getString("endpoint","");String address=DeviceDiscovery.refreshSaved(activity,old);JSONObject caps=TvHandoff.request(TvHandoff.endpoint(address),"capabilities",null);message=caps.optBoolean("armed")?"Your device is ready.":"Your device is reachable. Open FlXtR on it to receive a session.";}
            catch(Exception error){message="Device is offline or unreachable. Your selection is still saved.";}
            String result=message;activity.runOnUiThread(()->{if(!progress.isShowing()||activity.isFinishing())return;progress.dismiss();Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Saved device").setMessage(result).setPositiveButton("OK",null).show());});
        });
    }
}
