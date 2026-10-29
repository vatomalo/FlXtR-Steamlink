package org.flxtr.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.*;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;
import android.util.LruCache;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.*;
import java.util.concurrent.*;

/** Android's touch-first movie/series browser. SDL stays available internally. */
public class LibraryActivity extends Activity {
    private static final int BG=0xff0b1115,PANEL=0xff152129,INK=0xffe4eee8,GREEN=0xff87edac,MUTED=0xff91a69d;
    private final ExecutorService network=Executors.newSingleThreadExecutor();
    private final ThreadPoolExecutor images=new ThreadPoolExecutor(3,3,20,TimeUnit.SECONDS,new ArrayBlockingQueue<>(24),new ThreadPoolExecutor.DiscardOldestPolicy());
    private final LruCache<String,Bitmap> art=new LruCache<String,Bitmap>(8*1024*1024){@Override protected int sizeOf(String key,Bitmap value){return value.getByteCount();}};
    private final ArrayList<CatalogItem> rows=new ArrayList<>();
    private final ArrayDeque<JSONObject> history=new ArrayDeque<>();
    private SharedPreferences prefs;
    private CatalogClient client;
    private CatalogClient.Request request;
    private Future<?> pending;
    private GridView grid;
    private EditText search;
    private TextView heading,status,pageLabel;
    private Button previous,next,retry;
    private CatalogAdapter adapter;
    private String kind="movie",query="",title="Movies";
    private int page=1,pages=1,id,season,restorePosition;
    private volatile int generation;
    private volatile boolean destroyed;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        prefs=getSharedPreferences("touch-library",MODE_PRIVATE);client=new CatalogClient(getCacheDir());
        if(state!=null){try{restore(new JSONObject(state.getString("navigation","{}")));JSONArray stack=new JSONArray(state.getString("history","[]"));for(int i=0;i<stack.length();i++)history.addLast(stack.getJSONObject(i));restorePosition=state.getInt("scroll",0);}catch(Exception invalid){history.clear();}}
        build();load();
    }
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView label(String text,int size,int color){TextView v=new TextView(this);v.setText(text);v.setTextSize(size);v.setTextColor(color);v.setTypeface(Typeface.MONOSPACE);return v;}
    private Button button(String text,Runnable action){Button v=new Button(this);v.setText(text);v.setTextSize(14);v.setAllCaps(false);v.setTextColor(INK);v.setMinHeight(dp(48));v.setOnClickListener(view->action.run());return v;}
    private void build(){
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(16),dp(6),dp(16),0);root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((view,insets)->{root.setPadding(dp(16)+insets.getSystemWindowInsetLeft(),dp(6)+insets.getSystemWindowInsetTop(),dp(16)+insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;});
        LinearLayout header=new LinearLayout(this);header.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(button("‹",this::goBack),new LinearLayout.LayoutParams(dp(52),dp(50)));
        TextView brand=label("FLXTR",24,GREEN);brand.setPadding(dp(10),0,dp(14),0);brand.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);header.addView(brand);
        heading=label(title,16,INK);heading.setSingleLine(true);heading.setEllipsize(android.text.TextUtils.TruncateAt.END);header.addView(heading,new LinearLayout.LayoutParams(0,dp(48),1));heading.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(button("More",this::more));root.addView(header);
        LinearLayout tabs=new LinearLayout(this);
        for(String[] tab:new String[][]{{"Movies","movie"},{"Series","tv"},{"Favorites","favorites"}}){tabs.addView(button(tab[0],()->root(tab[1],tab[0])),new LinearLayout.LayoutParams(0,dp(48),1));}
        root.addView(tabs);
        search=new EditText(this);search.setTextColor(INK);search.setHintTextColor(MUTED);search.setTextSize(16);search.setSingleLine(true);search.setSelectAllOnFocus(true);search.setHint("Search movies or series");search.setText(query);search.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        search.setInputType(android.text.InputType.TYPE_CLASS_TEXT);search.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(80)});
        search.setOnEditorActionListener((view,action,event)->{if(action==EditorInfo.IME_ACTION_SEARCH||(event!=null&&event.getAction()==KeyEvent.ACTION_DOWN&&event.getKeyCode()==KeyEvent.KEYCODE_ENTER)){doSearch();return true;}return false;});
        LinearLayout searchRow=new LinearLayout(this);searchRow.addView(search,new LinearLayout.LayoutParams(0,dp(50),1));searchRow.addView(button("Search",this::doSearch));root.addView(searchRow);
        LinearLayout info=new LinearLayout(this);info.setGravity(Gravity.CENTER_VERTICAL);
        status=label("",12,MUTED);status.setMaxLines(2);info.addView(status,new LinearLayout.LayoutParams(0,dp(42),1));
        retry=button("Retry",this::load);retry.setVisibility(View.GONE);info.addView(retry);root.addView(info);
        grid=new GridView(this);grid.setVerticalSpacing(dp(14));grid.setHorizontalSpacing(dp(14));grid.setClipToPadding(false);grid.setPadding(0,dp(4),0,dp(12));grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);grid.setSelector(android.R.color.transparent);
        adapter=new CatalogAdapter();grid.setAdapter(adapter);grid.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{int columns=Math.max(2,Math.min(6,(r-l)/dp(prefs.getBoolean("compact",false)?135:175)));if(grid.getNumColumns()!=columns){grid.setNumColumns(columns);adapter.notifyDataSetChanged();}});
        grid.setOnItemClickListener((parent,view,position,key)->open(rows.get(position)));
        grid.setOnItemLongClickListener((parent,view,position,key)->{details(rows.get(position));return true;});
        GestureDetector gestures=new GestureDetector(this,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent e){return true;}
            @Override public boolean onFling(MotionEvent start,MotionEvent end,float vx,float vy){
                if(start!=null&&Math.abs(end.getX()-start.getX())>dp(90)&&Math.abs(vx)>Math.abs(vy)*1.8f){changePage(vx<0?1:-1);return true;}return false;
            }
        });
        grid.setOnTouchListener((view,event)->gestures.onTouchEvent(event)&&event.getAction()==MotionEvent.ACTION_UP);
        root.addView(grid,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout pager=new LinearLayout(this);pager.setGravity(Gravity.CENTER_VERTICAL);
        previous=button("Previous",()->changePage(-1));next=button("Next",()->changePage(1));pageLabel=label("",14,INK);pageLabel.setGravity(Gravity.CENTER);
        pager.addView(previous);pager.addView(pageLabel,new LinearLayout.LayoutParams(0,dp(52),1));pager.addView(next);root.addView(pager);
        setContentView(root);
    }
    private JSONObject navigation(){try{return new JSONObject().put("kind",kind).put("query",query).put("title",title).put("page",page).put("id",id).put("season",season).put("scroll",grid==null?0:grid.getFirstVisiblePosition());}catch(Exception impossible){throw new IllegalStateException(impossible);}}
    private void restore(JSONObject state){kind=state.optString("kind","movie");query=state.optString("query","");title=state.optString("title","Movies");page=Math.max(1,state.optInt("page",1));id=state.optInt("id");season=state.optInt("season");restorePosition=state.optInt("scroll",0);}
    private void root(String type,String label){history.clear();kind=type;title=label;query="";page=1;id=season=restorePosition=0;search.setText("");hideKeyboard();load();}
    private void doSearch(){String input=search.getText().toString().trim();if(input.isEmpty()){root(kind.equals("movie")?"movie":"tv",kind.equals("movie")?"Movies":"Series");return;}history.clear();kind=kind.equals("movie")?"movie":"tv";title=kind.equals("movie")?"Movies":"Series";query=input;page=1;id=season=restorePosition=0;hideKeyboard();load();}
    private void hideKeyboard(){search.clearFocus();((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(search.getWindowToken(),0);}
    private void open(CatalogItem item){
        if(item.kind.equals("tv")||item.kind.equals("season")){
            if(history.size()==10)history.removeFirst();history.addLast(navigation());
            kind=item.kind.equals("tv")?"season":"episode";title=item.kind.equals("tv")?item.title:title+" · "+item.title;id=item.id;season=item.season;query="";page=1;restorePosition=0;search.setText("");load();
        }else details(item);
    }
    private void changePage(int step){if(page+step<1||page+step>pages||kind.equals("favorites"))return;page+=step;restorePosition=0;load();}
    private void goBack(){if(!history.isEmpty()){restore(history.removeLast());search.setText(query);load();}else if(!kind.equals("movie")||!query.isEmpty()||page!=1){root("movie","Movies");}else finish();}
    @Override public void onBackPressed(){goBack();}
    private void load(){
        if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);
        final int token=++generation;images.getQueue().clear();rows.clear();adapter.notifyDataSetChanged();heading.setText(title);retry.setVisibility(View.GONE);
        status.setText("Loading…");previous.setEnabled(false);next.setEnabled(false);pageLabel.setText("Page "+page);
        if(kind.equals("favorites")){rows.addAll(favorites());pages=1;finishPage(false);return;}
        final String type=kind,term=query;final int requestedPage=page,show=id,sn=season;
        CatalogClient.Request job=new CatalogClient.Request();request=job;
        pending=network.submit(()->{
            try{CatalogPage result=client.load(type,requestedPage,term,show,sn,job);runOnUiThread(()->{if(destroyed||token!=generation)return;rows.addAll(result.items);pages=result.pages;finishPage(result.cached);});}
            catch(Exception error){runOnUiThread(()->{if(destroyed||token!=generation)return;status.setText("Could not load this page. Check your connection and try again.");retry.setVisibility(View.VISIBLE);previous.setEnabled(page>1);});}
        });
    }
    private void finishPage(boolean cached){
        adapter.notifyDataSetChanged();grid.setSelection(restorePosition);restorePosition=0;
        status.setText(rows.isEmpty()?(kind.equals("favorites")?"No favorites yet. Hold a poster to add one.":"No titles found."):
            (cached?"Saved page · ":"")+"Tap to open · hold for details");
        previous.setEnabled(page>1);next.setEnabled(page<pages);pageLabel.setText("Page "+page+" of "+pages);
    }
    private List<CatalogItem> favorites(){ArrayList<CatalogItem> items=new ArrayList<>();try{JSONArray array=new JSONArray(prefs.getString("favorites","[]"));for(int i=0;i<array.length();i++)items.add(CatalogItem.saved(array.getJSONObject(i)));}catch(Exception invalid){status.setText("Saved favorites could not be read.");}return items;}
    private boolean isFavorite(CatalogItem item){for(CatalogItem saved:favorites())if(saved.key().equals(item.key()))return true;return false;}
    private void toggleFavorite(CatalogItem item){
        List<CatalogItem> items=favorites();boolean removed=items.removeIf(saved->saved.key().equals(item.key()));if(!removed&&items.size()<200)items.add(0,item);
        JSONArray array=new JSONArray();try{for(CatalogItem saved:items)array.put(saved.json());prefs.edit().putString("favorites",array.toString()).apply();}catch(Exception invalid){Toast.makeText(this,"Could not save favorite",Toast.LENGTH_SHORT).show();}
        if(kind.equals("favorites"))load();else adapter.notifyDataSetChanged();
    }
    private void details(CatalogItem item){
        ScrollView scroll=new ScrollView(this);LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);scroll.addView(content);TextView description=label(item.meta+"\n\n"+(item.overview.isEmpty()?"No description available.":item.overview)+
            (item.kind.equals("movie")||item.kind.equals("episode")?"\n\nPlayback uses a direct HTTPS media link you provide.":""),16,INK);description.setPadding(dp(22),dp(10),dp(22),dp(12));content.addView(description);
        AlertDialog.Builder dialog=new AlertDialog.Builder(this).setTitle(item.title).setView(scroll).setNegativeButton("Close",null)
            .setNeutralButton(isFavorite(item)?"Remove favorite":"Favorite",(d,w)->toggleFavorite(item));
        if(item.kind.equals("tv")||item.kind.equals("season"))dialog.setPositiveButton("Browse",(d,w)->open(item));
        else dialog.setPositiveButton(prefs.contains("stream:"+item.key())?"Play saved stream":"Link stream",(d,w)->{
            String url=prefs.getString("stream:"+item.key(),"");if(CatalogItem.validStream(url))play(url,item.title,item.key());else link(item);
        });
        AlertDialog shown=dialog.show();
        if(prefs.contains("stream:"+item.key()))content.addView(button("Change saved stream",()->{shown.dismiss();link(item);}));
    }
    private void link(CatalogItem item){
        EditText input=new EditText(this);input.setSingleLine(true);input.setTextColor(INK);input.setHint("https://… direct media URL");input.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setText(prefs.getString("stream:"+item.key(),""));input.setPadding(dp(22),dp(10),dp(22),dp(10));
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Stream for "+item.title).setView(input).setNegativeButton("Cancel",null).setPositiveButton("Save & play",null).create();
        dialog.setOnShowListener(event->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view->{String value=input.getText().toString().trim();if(!CatalogItem.validStream(value)){input.setError("Enter a direct HTTPS media URL");return;}prefs.edit().putString("stream:"+item.key(),value).apply();dialog.dismiss();play(value,item.title,item.key());}));dialog.show();
    }
    private void play(String url,String label,String key){startActivity(new Intent(this,PlayerActivity.class).putExtra("url",url).putExtra("title",label).putExtra("resumeKey",key));}
    private void more(){
        new AlertDialog.Builder(this).setTitle("FlXtR").setItems(new String[]{"Open a direct video URL","Play Big Buck Bunny demo","Refresh this page",prefs.getBoolean("compact",false)?"Larger posters":"Smaller posters"},(dialog,which)->{
            if(which==0)link(new CatalogItem("stream",0,0,0,"Direct video","","",""));
            if(which==1)play("https://storage.googleapis.com/exoplayer-test-media-0/BigBuckBunny_320x180.mp4","Big Buck Bunny","demo");
            if(which==2){/* Bypass only this screen's cached response. */try{new java.io.File(client.jsonDir,CatalogClient.hash(CatalogClient.path(kind,page,query,id,season))+".json").delete();}catch(Exception ignored){}load();}
            if(which==3){prefs.edit().putBoolean("compact",!prefs.getBoolean("compact",false)).apply();int width=grid.getWidth();grid.setNumColumns(Math.max(2,Math.min(6,width/dp(prefs.getBoolean("compact",false)?135:175))));adapter.notifyDataSetChanged();}
        }).setNegativeButton("Close",null).show();
    }
    private final class CatalogAdapter extends BaseAdapter {
        @Override public int getCount(){return rows.size();}
        @Override public Object getItem(int index){return rows.get(index);}
        @Override public long getItemId(int index){return index;}
        @Override public View getView(int index,View reusable,android.view.ViewGroup parent){
            CatalogItem item=rows.get(index);LinearLayout card=new LinearLayout(LibraryActivity.this);card.setOrientation(LinearLayout.VERTICAL);card.setPadding(dp(7),dp(7),dp(7),dp(9));
            GradientDrawable surface=new GradientDrawable();surface.setColor(PANEL);surface.setCornerRadius(dp(12));card.setBackground(surface);
            int width=Math.max(dp(100),(grid.getWidth()-dp(14)*(grid.getNumColumns()-1))/Math.max(1,grid.getNumColumns())-dp(14));
            FrameLayout poster=new FrameLayout(LibraryActivity.this);TextView placeholder=label(item.kind.equals("season")?"SEASON "+item.season:"FLXTR",20,GREEN);placeholder.setGravity(Gravity.CENTER);poster.addView(placeholder,new FrameLayout.LayoutParams(-1,-1));
            ImageView image=new ImageView(LibraryActivity.this);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setContentDescription(item.title);poster.addView(image,new FrameLayout.LayoutParams(-1,-1));card.addView(poster,new LinearLayout.LayoutParams(-1,width*3/2));
            TextView name=label(item.title,15,INK);name.setMaxLines(2);name.setMinLines(2);name.setEllipsize(android.text.TextUtils.TruncateAt.END);name.setPadding(0,dp(8),0,0);card.addView(name);
            TextView meta=label(item.meta,11,MUTED);meta.setSingleLine(true);meta.setEllipsize(android.text.TextUtils.TruncateAt.END);card.addView(meta);
            if(!item.poster.isEmpty()){
                Bitmap bitmap=art.get(item.poster);if(bitmap!=null)image.setImageBitmap(bitmap);
                else{final int token=generation;images.execute(()->{if(token!=generation||destroyed)return;try{Bitmap loaded=client.poster(item.poster);if(loaded!=null){art.put(item.poster,loaded);runOnUiThread(()->{if(token==generation&&!destroyed)image.setImageBitmap(loaded);});}}catch(Exception ignored){/* Keep the labeled placeholder. */}});}
            }
            return card;
        }
    }
    @Override protected void onSaveInstanceState(Bundle state){state.putString("navigation",navigation().toString());JSONArray stack=new JSONArray();for(JSONObject entry:history)stack.put(entry);state.putString("history",stack.toString());state.putInt("scroll",grid.getFirstVisiblePosition());super.onSaveInstanceState(state);}
    @Override protected void onDestroy(){destroyed=true;++generation;if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);network.shutdownNow();images.shutdownNow();client.trimImages();super.onDestroy();}
}
