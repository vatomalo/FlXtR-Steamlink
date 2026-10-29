package org.flxtr.android;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.EditText;
import android.net.Uri;
import java.io.*;
import org.libsdl.app.SDLActivity;

public final class MainActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "main"}; }
    @Override protected void onCreate(Bundle state) {
        // Copy only packaged resources. User settings and favorites stay in app-private storage.
        try { copyAssets("runtime", getFilesDir()); }
        catch (IOException error) { throw new IllegalStateException("Cannot prepare FlXtR assets", error); }
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (mLayout == null) return;
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER);
        bar.setBackgroundColor(0xEB09110F);
        String[] labels = {"Back", "Left", "Right", "Up", "Down", "OK", "Settings", "URL"};
        int[] keys = {KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_F5};
        for (int i=0; i<labels.length; i++) {
            final int index=i;
            Button button=new Button(this); button.setText(labels[i]); button.setTextSize(12);
            button.setAllCaps(false); button.setPadding(0,0,0,0); button.setFocusable(false);
            button.setOnClickListener(view -> {
                if (index==7) { openUrl(); return; }
                onNativeKeyDown(keys[index]); onNativeKeyUp(keys[index]);
            });
            bar.addView(button,new LinearLayout.LayoutParams(0,LinearLayout.LayoutParams.MATCH_PARENT,1));
        }
        int height=(int)(48*getResources().getDisplayMetrics().density);
        RelativeLayout.LayoutParams position=new RelativeLayout.LayoutParams(-1,height);
        position.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        // Reserve space so touch controls never cover the native UI.
        mSurface.setPadding(0,0,0,0);
        RelativeLayout.LayoutParams surfacePosition=new RelativeLayout.LayoutParams(-1,-1);
        surfacePosition.bottomMargin=height;
        mSurface.setLayoutParams(surfacePosition);
        mLayout.addView(bar,position);
    }
    private void copyAssets(String source,File destination) throws IOException {
        String[] children=getAssets().list(source);
        if(children==null) throw new IOException("Missing asset: "+source);
        if(children.length>0) {
            if(!destination.isDirectory()&&!destination.mkdirs()) throw new IOException("Cannot create "+destination);
            for(String child:children) copyAssets(source+"/"+child,new File(destination,child));
        } else {
            try(InputStream in=getAssets().open(source); OutputStream out=new FileOutputStream(destination)) {
                byte[] buffer=new byte[16384]; int count;
                while((count=in.read(buffer))!=-1) out.write(buffer,0,count);
            }
        }
    }
    private void openUrl() {
        EditText input=new EditText(this); input.setSingleLine(true);
        input.setHint("https://... direct video or HLS URL");
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_URI);
        new AlertDialog.Builder(this).setTitle("Play a direct HTTPS stream").setView(input)
            .setNegativeButton("Cancel",null).setPositiveButton("Play",(dialog,which)-> {
                String url=input.getText().toString().trim();
                if(!"https".equalsIgnoreCase(Uri.parse(url).getScheme())) {
                    new AlertDialog.Builder(this).setMessage("Enter a direct HTTPS media URL.").setPositiveButton("OK",null).show(); return;
                }
                launchVideo(url,"Direct stream");
            }).show();
    }
    public void launchVideo(String url,String title) {
        runOnUiThread(()->startActivity(new Intent(this,PlayerActivity.class).putExtra("url",url).putExtra("title",title)));
    }
}
