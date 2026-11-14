package org.flxtr.android;

import android.content.Context;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import org.json.JSONObject;

/** Moves AI observations over FlXtR's private tailnet without Python on Android. */
final class SomAiRelay implements AutoCloseable {
    static final int PROTOCOL=2;
    private static final String ENDPOINT="http://"+AdbSetup.SKYLOFT+":43928/som-ai/v1/step";
    private static final Set<String> ACTIONS=new HashSet<>(Arrays.asList(
        "right","left","up","down","attack","open_ring","confirm","cancel",
        "next_ring","prev_ring","menu_x","step_right","step_left","step_up","step_down",
        "dash_right","dash_left","dash_up","dash_down","wait","block","charge",
        "ai_off","clone_enemy","undo_spawn","summon_familiar","dismiss_familiar"));
    private final Context context;
    private volatile boolean running;
    private Thread thread;
    SomAiRelay(Context context){this.context=context.getApplicationContext();}
    void start(){if(running)return;running=true;thread=new Thread(this::run,"FlXtR-SoM-AI");thread.start();}
    private void run(){
        boolean reported=false,announced=false;
        while(running){
            try{
                byte[] wram=RetroActivity.somSnapshot();
                if(wram==null){Thread.sleep(200);continue;}
                String status=new JSONObject(RetroActivity.somStatus()).put("protocol",PROTOCOL).toString();
                byte[] header=(status+"\n").getBytes(StandardCharsets.UTF_8);
                byte[] request=new byte[header.length+wram.length];
                System.arraycopy(header,0,request,0,header.length);
                System.arraycopy(wram,0,request,header.length,wram.length);
                String raw=TailscaleRuntime.get(context).requestBytes(ENDPOINT,request);
                JSONObject response=new JSONObject(raw);
                if(response.optInt("protocol")!=PROTOCOL)throw new java.io.IOException("Update the SkyLoft AI service");
                String action=response.optString("action","");
                String message=response.optString("message","");
                if(ACTIONS.contains(action))RetroActivity.queueSomCommand(action);
                if(!announced){RetroActivity.queueSomMessage("AI bridge v2|new planner active");announced=true;}
                else if(!message.isEmpty()&&message.length()<=95)RetroActivity.queueSomMessage(message);
                reported=false;
            }catch(InterruptedException stopped){Thread.currentThread().interrupt();break;}
            catch(Exception failure){
                if(!reported){RetroActivity.queueSomMessage("AI service unavailable|check SkyLoft and Tailscale");reported=true;}
                try{Thread.sleep(1000);}catch(InterruptedException stopped){Thread.currentThread().interrupt();break;}
            }
        }
    }
    @Override public void close(){running=false;Thread active=thread;if(active!=null){active.interrupt();try{active.join(1500);}catch(InterruptedException stopped){Thread.currentThread().interrupt();}}thread=null;}
}
