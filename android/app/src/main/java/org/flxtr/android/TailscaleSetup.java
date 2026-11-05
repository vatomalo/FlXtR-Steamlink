package org.flxtr.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.widget.Toast;
import org.json.JSONObject;

/** Uses the official Android VPN app; installation is distinct from reachability. */
final class TailscaleSetup {
    private static final String PACKAGE="com.tailscale.ipn";

    static boolean installed(Context context){
        try{context.getPackageManager().getPackageInfo(PACKAGE,0);return true;}
        catch(PackageManager.NameNotFoundException absent){return false;}
    }

    static String actionLabel(Context context){return installed(context)?"Open Tailscale":"Install Tailscale";}

    static void show(Activity activity){
        boolean present=installed(activity);
        String message=present
            ?"Tailscale is installed. Open it, sign in to the same account as your Steam Link and SkyLoft, and connect. Then return to FlXtR."
            :"Install the official Tailscale app, sign in to the same account as your Steam Link and SkyLoft, and approve Android's VPN prompt. Then return to FlXtR.";
        message+="\n\nInstallation alone does not confirm a connection. Check your paired TV to verify that FlXtR can reach it.";
        boolean paired=!activity.getSharedPreferences("tv-handoff",0).getString("endpoint","").isEmpty();
        Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("Tailscale")
            .setMessage(message).setPositiveButton(actionLabel(activity),(d,w)->open(activity))
            .setNeutralButton(paired?"Check TV connection":"Pair TV",(d,w)->{
                if(paired)checkTv(activity);else TvHandoff.pair(activity,null,null);
            }).setNegativeButton("Close",null).show());
    }

    static void open(Activity activity){
        Intent launch=activity.getPackageManager().getLaunchIntentForPackage(PACKAGE);
        if(launch!=null){
            try{activity.startActivity(launch);return;}
            catch(ActivityNotFoundException|SecurityException unavailable){
                Toast.makeText(activity,"Tailscale could not be opened. Check that it is enabled in Android Settings.",Toast.LENGTH_LONG).show();return;
            }
        }
        if(installed(activity)){
            try{activity.startActivity(new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,Uri.parse("package:"+PACKAGE)));return;}
            catch(ActivityNotFoundException|SecurityException unavailable){
                Toast.makeText(activity,"Enable Tailscale in Android Settings, then return to FlXtR.",Toast.LENGTH_LONG).show();return;
            }
        }
        try{activity.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("market://details?id="+PACKAGE)).setPackage("com.android.vending"));}
        catch(ActivityNotFoundException|SecurityException noStore){
            try{activity.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("https://tailscale.com/download/android")));}
            catch(ActivityNotFoundException|SecurityException noBrowser){Toast.makeText(activity,"Install the official Tailscale app from tailscale.com/download/android",Toast.LENGTH_LONG).show();}
        }
    }

    private static void checkTv(Activity activity){
        String address=activity.getSharedPreferences("tv-handoff",0).getString("endpoint","");
        AlertDialog progress=new AlertDialog.Builder(activity).setTitle("TV connection")
            .setMessage("Checking your paired TV…").setNegativeButton("Close",null).show();
        Gamepad.dialog(progress);
        TvHandoff.NETWORK.execute(()->{
            String result;
            try{
                JSONObject capabilities=TvHandoff.request(TvHandoff.endpoint(address),"capabilities",null);
                if(capabilities.optInt("protocol")!=1)throw new java.io.IOException("Unsupported TV response");
                result=capabilities.optBoolean("armed")?"Your paired TV is reachable and ready.":"Your paired TV is reachable. Open Continue on TV on the TV to receive a session.";
            }catch(Exception unavailable){result="Could not reach your paired TV. Connect Tailscale on both devices using the same account, open Continue on TV on the TV, and retry. Pair the TV again if its address has changed.";}
            String message=result;
            activity.runOnUiThread(()->{
                if(activity.isFinishing()||activity.isDestroyed()||!progress.isShowing())return;
                progress.dismiss();
                Gamepad.dialog(new AlertDialog.Builder(activity).setTitle("TV connection").setMessage(message)
                    .setPositiveButton("OK",null).setNeutralButton(actionLabel(activity),(d,w)->open(activity)).show());
            });
        });
    }
}
