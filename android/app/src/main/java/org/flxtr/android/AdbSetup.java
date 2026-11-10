package org.flxtr.android;

import android.app.*;
import android.text.InputType;
import android.widget.*;
import org.json.*;

/** Explicit, temporary access from the user's build server to Android adbd. */
final class AdbSetup {
    static final String SKYLOFT="100.69.7.81";
    static int port(String value,boolean optional){
        value=value.trim();if(optional&&value.isEmpty())return 0;
        if(!value.matches("[0-9]{4,5}"))throw new IllegalArgumentException("Enter the port number after the colon");
        int port=Integer.parseInt(value);if(port<1024||port>65535)throw new IllegalArgumentException("Port must be between 1024 and 65535");return port;
    }
    static void show(Activity activity){
        LinearLayout form=new LinearLayout(activity);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(32,16,32,16);
        TextView status=new TextView(activity);status.setText("Checking debugging access…");form.addView(status);
        EditText connect=new EditText(activity);connect.setSingleLine(true);connect.setInputType(InputType.TYPE_CLASS_NUMBER);connect.setHint("Connection port (main Wireless debugging screen)");form.addView(connect);
        EditText pair=new EditText(activity);pair.setSingleLine(true);pair.setInputType(InputType.TYPE_CLASS_NUMBER);pair.setHint("Pairing port (Pair device with pairing code)");form.addView(pair);
        TextView help=new TextView(activity);help.setText("Enter the two port numbers shown by Android. Leave pairing port blank if SkyLoft is already paired. The pairing code is entered on SkyLoft, not here.\n\nOnly SkyLoft can connect. Access closes after 15 minutes, on Tailscale disconnect, or when FlXtR exits. Keep FlXtR open during debugging.");form.addView(help);
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("ADB through Tailscale").setView(form)
            .setPositiveButton("Enable for 15 minutes",null).setNeutralButton("Stop access",null).setNegativeButton("Close",null).create();
        Runnable refresh=()->TailscaleRuntime.IO.execute(()->{
            String message;
            try{
                JSONObject state=TailscaleRuntime.status(activity),adb=state.optJSONObject("adb");JSONArray ips=state.optJSONArray("ips");
                if(!state.optString("state").equals("Running"))message="Connect built-in Tailscale first, then return here.";
                else if(adb!=null&&adb.optBoolean("active")&&ips!=null&&ips.length()>0){
                    String ip=ips.getString(0);message="SkyLoft can connect until "+android.text.format.DateFormat.getTimeFormat(activity).format(new java.util.Date(adb.getLong("expires")*1000))+"\nADB: "+ip+":43930";
                    if(adb.optBoolean("pairing"))message+="\nPairing: "+ip+":43931";
                }else message="Access is off. Enable it below after opening Android Wireless debugging.";
            }catch(Exception|LinkageError error){message="Could not check ADB access: "+error.getMessage();}
            String result=message;activity.runOnUiThread(()->{if(dialog.isShowing())status.setText(result);});
        });
        dialog.setOnShowListener(d->{
            dialog.getButton(-1).setOnClickListener(v->{
                final int connectPort,pairPort;
                try{connectPort=port(connect.getText().toString(),false);pairPort=port(pair.getText().toString(),true);if(pairPort==connectPort)throw new IllegalArgumentException("Connection and pairing use different ports");}
                catch(IllegalArgumentException invalid){status.setText(invalid.getMessage());return;}
                dialog.getButton(-1).setEnabled(false);status.setText("Enabling access from SkyLoft…");
                TailscaleRuntime.IO.execute(()->{
                    try{
                        if(!TailscaleRuntime.enabled(activity)||!TailscaleRuntime.status(activity).optString("state").equals("Running"))throw new IllegalStateException("Connect built-in Tailscale first");
                        TailscaleRuntime.get(activity).startADB(connectPort,pairPort,SKYLOFT);refresh.run();
                    }catch(Exception|LinkageError error){activity.runOnUiThread(()->status.setText("Could not enable ADB: "+error.getMessage()));}
                    finally{activity.runOnUiThread(()->{if(dialog.isShowing())dialog.getButton(-1).setEnabled(true);});}
                });
            });
            dialog.getButton(-3).setOnClickListener(v->TailscaleRuntime.IO.execute(()->{try{if(TailscaleRuntime.enabled(activity))TailscaleRuntime.get(activity).stopADB();refresh.run();}catch(Exception|LinkageError error){activity.runOnUiThread(()->status.setText(error.getMessage()));}}));
            refresh.run();
        });dialog.show();Gamepad.dialog(dialog);
    }
}
