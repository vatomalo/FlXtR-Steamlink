package org.flxtr.android;

import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.Bundle;
import android.widget.*;
import org.json.JSONObject;
import java.io.File;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Folder-backed console library. No background polling service. */
public final class GamesActivity extends Activity {
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private GameLibrary library;
    private boolean busy;
    private long lastScan;
    private TextView status;
    private LinearLayout rows;
    private final ArrayList<Button> actions=new ArrayList<>();
    @Override public void onCreate(Bundle state){super.onCreate(state);library=new GameLibrary(this);build();render();}
    private void build(){
        LibraryTheme theme=new LibraryTheme(this,getSharedPreferences("android-settings",0).getInt("theme",0));LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(16),dp(12),dp(16),dp(12));root.setBackground(theme.background(this));
        TextView title=new TextView(this);title.setText("Games");title.setTextSize(26);title.setTextColor(theme.ink);title.setTypeface(theme.font);root.addView(title);
        TextView info=new TextView(this);info.setText("GB · GBC · NES · SNES · GBA · PlayStation\nChoose a game folder once. New games are found when you open this library. Keep CUE/BIN tracks and M3U discs together; CHD, PBP and ISO also work. Extract cartridge ZIP files first.");info.setTextColor(theme.muted);root.addView(info);
        LinearLayout top=new LinearLayout(this);action(top,"Add game folder",this::pickFolder);action(top,"Scan now",()->scan());root.addView(top);
        LinearLayout more=new LinearLayout(this);action(more,"Add files",()->pickFiles(90));action(more,"Folders",this::folders);action(more,"PS BIOS",()->pickFiles(92));root.addView(more);
        Button streaming=new Button(this);streaming.setText("Stream PC games · Moonlight");streaming.setAllCaps(false);streaming.setOnClickListener(v->startActivity(new Intent(this,com.limelight.PcView.class)));root.addView(streaming);
        status=new TextView(this);status.setTextColor(theme.muted);root.addView(status);
        ScrollView scroll=new ScrollView(this);rows=new LinearLayout(this);rows.setOrientation(LinearLayout.VERTICAL);scroll.addView(rows);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        Button back=new Button(this);back.setText("Back to FlXtR");back.setOnClickListener(v->finish());root.addView(back);setContentView(root);
    }
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private void action(LinearLayout row,String label,Runnable click){Button b=new Button(this);b.setText(label);b.setTextSize(12);b.setAllCaps(false);b.setOnClickListener(v->click.run());row.addView(b,new LinearLayout.LayoutParams(0,dp(60),1));actions.add(b);}
    private void render(){rows.removeAllViews();try{List<JSONObject> games=library.games();for(JSONObject game:games){Button b=new Button(this);b.setAllCaps(false);b.setText(game.optString("name")+"  ·  "+GameFormats.label(game.optString("core")));b.setOnClickListener(v->play(game));rows.addView(b);}status.setText(library.scanMessage.isEmpty()?games.size()+" games · Add a folder to enable automatic scanning.":library.scanMessage);}catch(Exception error){status.setText(error.getMessage());}}
    @Override protected void onResume(){super.onResume();if(!busy&&(lastScan==0||android.os.SystemClock.elapsedRealtime()-lastScan>30000))scan();}
    private void pickFolder(){startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,Uri.parse("content://com.android.externalstorage.documents/document/primary%3AFlXtR%2FGames")).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION|Intent.FLAG_GRANT_PREFIX_URI_PERMISSION),91);}
    private void pickFiles(int code){startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_ALLOW_MULTIPLE,code==90).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION),code);}
    private void setBusy(boolean value){busy=value;for(Button b:actions)b.setEnabled(!value);for(int i=0;i<rows.getChildCount();i++)rows.getChildAt(i).setEnabled(!value);}
    private void scan(){if(busy)return;lastScan=android.os.SystemClock.elapsedRealtime();setBusy(true);status.setText("Scanning game folders…");worker.execute(()->{String error="";try{library.scan();}catch(Exception e){error=e.getMessage()==null?"Scan failed":e.getMessage();}final String problem=error;runOnUiThread(()->{if(isFinishing()||isDestroyed())return;setBusy(false);render();if(!problem.isEmpty())status.setText(problem);});});}
    private void folders(){try{List<String> folders=library.folders();if(folders.isEmpty()){new AlertDialog.Builder(this).setMessage("Add a game folder to enable automatic scanning.").setPositiveButton("Add folder",(d,i)->pickFolder()).setNegativeButton("Close",null).show();return;}String[] labels=new String[folders.size()];for(int i=0;i<labels.length;i++)labels[i]=Uri.decode(Uri.parse(folders.get(i)).getLastPathSegment());Gamepad.dialog(new AlertDialog.Builder(this).setTitle("Remove a scan folder").setItems(labels,(d,i)->{try{library.removeFolder(folders.get(i));render();}catch(Exception e){error(e);}}).setNegativeButton("Close",null).show());}catch(Exception e){error(e);}}
    @Override protected void onActivityResult(int code,int result,Intent data){super.onActivityResult(code,result,data);if(result!=RESULT_OK||data==null)return;
        ArrayList<Uri> uris=new ArrayList<>();if(data.getClipData()!=null)for(int i=0;i<Math.min(200,data.getClipData().getItemCount());i++)uris.add(data.getClipData().getItemAt(i).getUri());else if(data.getData()!=null)uris.add(data.getData());
        for(Uri uri:uris){try{getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(SecurityException ignored){}}
        setBusy(true);worker.execute(()->{ArrayList<String> problems=new ArrayList<>();for(Uri uri:uris)try{if(code==91)library.addFolder(uri);else if(code==90)library.addFile(uri);else if(code==92)library.importBios(uri);}catch(Exception e){problems.add(e.getMessage()==null?"Could not add file":e.getMessage());}try{library.scan();}catch(Exception e){problems.add(e.getMessage()==null?"Scan failed":e.getMessage());}runOnUiThread(()->{if(isFinishing()||isDestroyed())return;setBusy(false);render();if(!problems.isEmpty())new AlertDialog.Builder(this).setTitle("Game library").setMessage(String.join("\n",problems)).setPositiveButton("OK",null).show();else if(code==92)Toast.makeText(this,"PlayStation BIOS imported",Toast.LENGTH_SHORT).show();});});
    }
    private void play(JSONObject game){if(busy)return;setBusy(true);AtomicBoolean cancelled=new AtomicBoolean();ProgressDialog progress=new ProgressDialog(this);progress.setMessage("Preparing "+game.optString("name")+"…");progress.setCancelable(true);Future<?> job=worker.submit(()->{try{File rom=library.stage(game);String key=library.saveKey(game);if(Thread.currentThread().isInterrupted()||cancelled.get())return;runOnUiThread(()->{progress.dismiss();if(isFinishing()||isDestroyed()||cancelled.get())return;setBusy(false);startActivity(new Intent(this,RetroActivity.class).putExtra("rom",rom.getAbsolutePath()).putExtra("core",game.optString("core")).putExtra("saveKey",key));});}catch(Exception e){runOnUiThread(()->{progress.dismiss();if(isFinishing()||isDestroyed()||cancelled.get())return;setBusy(false);if(!(e instanceof java.io.InterruptedIOException))error(e);});}});progress.setOnCancelListener(d->{cancelled.set(true);job.cancel(true);setBusy(false);});progress.show();}
    private void error(Exception error){new AlertDialog.Builder(this).setTitle("Games").setMessage(error.getMessage()==null?"Could not open this game":error.getMessage()).setPositiveButton("OK",null).show();}
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
}
