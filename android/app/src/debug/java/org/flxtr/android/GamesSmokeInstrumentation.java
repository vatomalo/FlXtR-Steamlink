package org.flxtr.android;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.*;
import android.graphics.Bitmap;
import android.view.PixelCopy;
import android.view.SurfaceView;
import java.io.*;
import java.util.concurrent.*;

/** Original ROM fixture; no commercial games or PC credentials. */
public final class GamesSmokeInstrumentation extends Instrumentation {
 private String mode;private long hold;
 @Override public void onCreate(Bundle args){super.onCreate(args);mode=args.getString("mode","retro");hold=Long.parseLong(args.getString("holdMs","0"));start();}
 @Override public void onStart(){Bundle result=new Bundle();try{getUiAutomation();Thread.sleep(500);
  if(mode.equals("moonlight")){
   android.app.Activity activity=startActivitySync(new Intent(getTargetContext(),com.limelight.PcView.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));waitForIdleSync();Thread.sleep(2000);result.putBoolean("nativeActivity",activity instanceof com.limelight.PcView);System.loadLibrary("moonlight-core");result.putString("nativeLibrary","loaded");com.limelight.binding.crypto.AndroidCryptoProvider crypto=new com.limelight.binding.crypto.AndroidCryptoProvider(getTargetContext());result.putBoolean("pairingCertificate",crypto.getClientCertificate()!=null&&crypto.getClientPrivateKey()!=null);if(!result.getBoolean("pairingCertificate"))throw new IOException("Pairing certificate unavailable");runOnMainSync(activity::finish);
  }else if(mode.equals("updates")){
   MainActivity activity=(MainActivity)startActivitySync(new Intent(getTargetContext(),MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));waitForIdleSync();Thread.sleep(1500);runOnMainSync(()->new AppUpdates(activity).menu());Thread.sleep(1500);android.view.accessibility.AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();result.putBoolean("checkAction",root!=null&&!root.findAccessibilityNodeInfosByText("Check for updates").isEmpty());result.putBoolean("automaticToggle",root!=null&&!root.findAccessibilityNodeInfosByText("Automatic checks").isEmpty());if(!result.getBoolean("checkAction")||!result.getBoolean("automaticToggle"))throw new IOException("Update menu missing");
  }else{
   File folder=new File(getTargetContext().getFilesDir(),"games");folder.mkdirs();File rom=new File(folder,"flxtr-test.gb");try(OutputStream out=new FileOutputStream(rom)){out.write(rom());}
   RetroActivity activity=(RetroActivity)startActivitySync(new Intent(getTargetContext(),RetroActivity.class).putExtra("rom",rom.getAbsolutePath()).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK));long end=System.currentTimeMillis()+15000;while(activity.framesPlayed<120&&System.currentTimeMillis()<end)Thread.sleep(100);
   result.putLong("frames",activity.framesPlayed);result.putLong("audioSamples",activity.samplesPlayed);if(activity.framesPlayed<120||activity.samplesPlayed<1000)throw new IOException("Game/audio did not advance");
   java.lang.reflect.Field view=RetroActivity.class.getDeclaredField("surface");view.setAccessible(true);SurfaceView surface=(SurfaceView)view.get(activity);Bitmap bitmap=Bitmap.createBitmap(160,144,Bitmap.Config.ARGB_8888);CountDownLatch copied=new CountDownLatch(1);int[] status={-1};runOnMainSync(()->PixelCopy.request(surface,bitmap,code->{status[0]=code;copied.countDown();},new Handler(Looper.getMainLooper())));if(!copied.await(5,TimeUnit.SECONDS)||status[0]!=PixelCopy.SUCCESS)throw new IOException("No game picture");int first=bitmap.getPixel(0,0);boolean pattern=false;for(int y=0;y<144&&!pattern;y++)for(int x=0;x<160;x++)if(bitmap.getPixel(x,y)!=first){pattern=true;break;}result.putBoolean("picturePattern",pattern);if(!pattern)throw new IOException("Game picture is blank");
   java.lang.reflect.Field queue=RetroActivity.class.getDeclaredField("commands");queue.setAccessible(true);CountDownLatch saved=new CountDownLatch(1);((java.util.Queue<Runnable>)queue.get(activity)).add(()->{byte[] state=RetroActivity.readMemory(true);result.putBoolean("saveRoundTrip",state!=null&&state.length>0&&RetroActivity.writeMemory(state,true));saved.countDown();});if(!saved.await(5,TimeUnit.SECONDS)||!result.getBoolean("saveRoundTrip"))throw new IOException("State round trip failed");
   sendKeySync(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN,96));java.lang.reflect.Field keys=RetroActivity.class.getDeclaredField("keys");keys.setAccessible(true);result.putBoolean("controllerA",(keys.getInt(activity)&(1<<8))!=0);sendKeySync(new android.view.KeyEvent(android.view.KeyEvent.ACTION_UP,96));if(!result.getBoolean("controllerA"))throw new IOException("Controller A not mapped");
   if(hold>0)Thread.sleep(Math.min(hold,30000));runOnMainSync(activity::finish);Thread.sleep(300);rom.delete();new File(rom+".auto").delete();new File(rom+".srm").delete();
  }finish(-1,result);
 }catch(Throwable failure){result.putString("error",failure.getClass().getSimpleName()+": "+failure.getMessage());finish(0,result);}}
 private byte[] rom(){
  byte[] rom=new byte[32768];rom[0x100]=(byte)0xc3;rom[0x101]=0x50;rom[0x102]=1;byte[] title="FLXTR TEST".getBytes(java.nio.charset.StandardCharsets.US_ASCII);System.arraycopy(title,0,rom,0x134,title.length);
  int[] code={0xf3,0x31,0xfe,0xff,0xaf,0xe0,0x40,0x3e,0xe4,0xe0,0x47,0x21,0,0x80,0x06,16,0x3e,0xaa,0x22,0x2f,0x05,0x20,0xfb,0x21,0,0x98,0x01,0,4,0xaf,0x22,0x0b,0x78,0xb1,0x20,0xf9,0x3e,0x80,0xe0,0x26,0x3e,0x77,0xe0,0x24,0x3e,0xff,0xe0,0x25,0x3e,0x80,0xe0,0x11,0x3e,0xf3,0xe0,0x12,0x3e,0,0xe0,0x13,0x3e,0x87,0xe0,0x14,0x3e,0x91,0xe0,0x40,0x18,0xfe};for(int i=0;i<code.length;i++)rom[0x150+i]=(byte)code[i];int checksum=0;for(int i=0x134;i<=0x14c;i++)checksum=(checksum-(rom[i]&255)-1)&255;rom[0x14d]=(byte)checksum;return rom;
 }
}
