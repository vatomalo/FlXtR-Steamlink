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
    private int BG=0xff0b1115,PANEL=0xff152129,INK=0xffe4eee8,GREEN=0xff87edac,MUTED=0xff91a69d;
    private final ExecutorService network=Executors.newSingleThreadExecutor();
    private final ThreadPoolExecutor images=new ThreadPoolExecutor(3,3,20,TimeUnit.SECONDS,new ArrayBlockingQueue<>(24),new ThreadPoolExecutor.DiscardOldestPolicy());
    private final LruCache<String,Bitmap> art=new LruCache<String,Bitmap>(8*1024*1024){@Override protected int sizeOf(String key,Bitmap value){return value.getByteCount();}};
    private final ArrayList<CatalogItem> rows=new ArrayList<>();
    private final ArrayDeque<JSONObject> history=new ArrayDeque<>();
    private SharedPreferences prefs;
    private CatalogClient client;
    private MediaRepository.Job request;
    private MediaRepository repository;
    private LibraryTheme theme;
    private boolean compactLayout;
    private SharedPreferences settings;
    private final Gamepad gamepad=new Gamepad();
    private HorizontalScrollView flow; private LinearLayout covers; private int selected;
    private String link=""; private int genre,order; private boolean refreshing;
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
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN|WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        prefs=getSharedPreferences("touch-library",MODE_PRIVATE);settings=getSharedPreferences("android-settings",MODE_PRIVATE);theme=new LibraryTheme(this,settings.getInt("theme",0));
        android.content.res.Configuration window=getResources().getConfiguration();compactLayout=LibraryLayout.compact(settings.getInt("layout",0),window.screenWidthDp,window.screenHeightDp);
        BG=theme.bg;PANEL=theme.panel;INK=theme.ink;GREEN=theme.accent;MUTED=theme.muted;client=new CatalogClient(getCacheDir());repository=new MediaRepository(this,client);
        if(state!=null){try{restore(new JSONObject(state.getString("navigation","{}")));JSONArray stack=new JSONArray(state.getString("history","[]"));for(int i=0;i<stack.length();i++)history.addLast(stack.getJSONObject(i));restorePosition=state.getInt("scroll",0);}catch(Exception invalid){history.clear();}}
        if(getIntent().getStringExtra("section")!=null){kind=getIntent().getStringExtra("section");title="Downloads";}build();load();
    }
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView label(String text,int size,int color){TextView v=new TextView(this);v.setText(text);v.setTextSize(size);v.setTextColor(color);v.setTypeface(theme.font);return v;}
    private Button button(String text,Runnable action){Button v=new Button(this);v.setText(text);v.setTextSize(compactLayout?13:14);v.setMinWidth(dp(48));v.setMinimumWidth(dp(48));v.setPadding(dp(8),0,dp(8),0);v.setAllCaps(false);v.setTextColor(INK);v.setMinHeight(dp(48));v.setTypeface(theme.font);v.setBackground(theme.focus());v.setOnClickListener(view->action.run());return v;}
    private void build(){
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(compactLayout?8:16),dp(6),dp(compactLayout?8:16),0);root.setBackground(theme.background(this));
        root.setOnApplyWindowInsetsListener((view,insets)->{root.setPadding(dp(compactLayout?8:16)+insets.getSystemWindowInsetLeft(),dp(6)+insets.getSystemWindowInsetTop(),dp(compactLayout?8:16)+insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;});
        LinearLayout header=new LinearLayout(this);header.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(button("‹",this::goBack),new LinearLayout.LayoutParams(dp(52),dp(50)));
        TextView brand=label("FLXTR",24,GREEN);brand.setPadding(dp(10),0,dp(14),0);brand.setTypeface(Typeface.MONOSPACE,Typeface.BOLD);if(!compactLayout)header.addView(brand);
        heading=label(title,16,INK);heading.setSingleLine(true);heading.setEllipsize(android.text.TextUtils.TruncateAt.END);header.addView(heading,new LinearLayout.LayoutParams(0,dp(48),1));heading.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(button("Library",this::sections));header.addView(button(compactLayout?"⋮":"More",this::more));root.addView(header);
        LinearLayout tabs=new LinearLayout(this);
        for(String[] tab:new String[][]{{"Movies","movie"},{"Series","tv"},{"Favorites","favorites"}}){tabs.addView(button(tab[0],()->root(tab[1],tab[0])),new LinearLayout.LayoutParams(0,dp(48),1));}
        if(!compactLayout)root.addView(tabs);
        search=new EditText(this);search.setTextColor(INK);search.setHintTextColor(MUTED);search.setTextSize(16);search.setSingleLine(true);search.setSelectAllOnFocus(true);search.setHint(kind.equals("music")?"Artist, song, YouTube / Spotify playlist":kind.startsWith("kiss")?"Search anime":"Search this library");search.setText(query);search.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        search.setInputType(android.text.InputType.TYPE_CLASS_TEXT);search.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(80)});
        search.setOnEditorActionListener((view,action,event)->{if(action==EditorInfo.IME_ACTION_SEARCH||(event!=null&&event.getAction()==KeyEvent.ACTION_DOWN&&event.getKeyCode()==KeyEvent.KEYCODE_ENTER)){doSearch();return true;}return false;});
        LinearLayout searchRow=new LinearLayout(this);searchRow.addView(search,new LinearLayout.LayoutParams(0,dp(50),1));searchRow.addView(button("Search",this::doSearch));root.addView(searchRow);
        LinearLayout info=new LinearLayout(this);info.setGravity(Gravity.CENTER_VERTICAL);
        status=label("",12,MUTED);status.setMaxLines(2);info.addView(status,new LinearLayout.LayoutParams(0,dp(compactLayout?30:42),1));
        retry=button("Retry",this::load);retry.setVisibility(View.GONE);info.addView(retry);root.addView(info);
        grid=new GridView(this);grid.setVerticalSpacing(dp(14));grid.setHorizontalSpacing(dp(14));grid.setClipToPadding(false);grid.setPadding(0,dp(4),0,dp(12));grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);grid.setSelector(theme.selection());grid.setDrawSelectorOnTop(true);grid.setFocusable(true);grid.setFocusableInTouchMode(true);
        adapter=new CatalogAdapter();grid.setAdapter(adapter);grid.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{int columns=LibraryLayout.columns(Math.round((r-l)/getResources().getDisplayMetrics().density),compactLayout);if(grid.getNumColumns()!=columns){grid.setNumColumns(columns);adapter.notifyDataSetChanged();}});
        grid.setOnItemClickListener((parent,view,position,key)->open(rows.get(position)));
        grid.setOnItemLongClickListener((parent,view,position,key)->{details(rows.get(position));return true;});
        GestureDetector gestures=new GestureDetector(this,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent e){return true;}
            @Override public boolean onFling(MotionEvent start,MotionEvent end,float vx,float vy){
                if(start!=null&&Math.abs(end.getX()-start.getX())>dp(90)&&Math.abs(vx)>Math.abs(vy)*1.8f){changePage(vx<0?1:-1);return true;}return false;
            }
        });
        grid.setOnTouchListener((view,event)->gestures.onTouchEvent(event)&&event.getAction()==MotionEvent.ACTION_UP);
        if(theme.original()&&!compactLayout){
            LinearLayout body=new LinearLayout(this);ScrollView menu=new ScrollView(this);LinearLayout sidebar=new LinearLayout(this);sidebar.setOrientation(LinearLayout.VERTICAL);menu.addView(sidebar);
            for(String[] section:SECTIONS)sidebar.addView(button(section[0],()->root(section[1],section[0])),new LinearLayout.LayoutParams(-1,dp(48)));
            body.addView(menu,new LinearLayout.LayoutParams(dp(154),-1));flow=new HorizontalScrollView(this);flow.setFillViewport(true);covers=new LinearLayout(this);covers.setGravity(Gravity.CENTER_VERTICAL);covers.setPadding(dp(32),dp(12),dp(32),dp(12));flow.addView(covers,new HorizontalScrollView.LayoutParams(-2,-1));body.addView(flow,new LinearLayout.LayoutParams(0,-1,1));root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        }else root.addView(grid,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout pager=new LinearLayout(this);pager.setGravity(Gravity.CENTER_VERTICAL);
        previous=button("Previous",()->changePage(-1));next=button("Next",()->changePage(1));pageLabel=label("",14,INK);pageLabel.setGravity(Gravity.CENTER);
        pager.addView(previous);pager.addView(pageLabel,new LinearLayout.LayoutParams(0,dp(52),1));pager.addView(next);root.addView(pager);
        root.setFocusableInTouchMode(true);setContentView(root);root.requestFocus();
    }
    private JSONObject navigation(){try{return new JSONObject().put("link",link).put("genre",genre).put("order",order).put("kind",kind).put("query",query).put("title",title).put("page",page).put("id",id).put("season",season).put("scroll",grid==null?0:(covers==null?grid.getFirstVisiblePosition():selected));}catch(Exception impossible){throw new IllegalStateException(impossible);}}
    private void restore(JSONObject state){link=state.optString("link","");genre=state.optInt("genre");order=state.optInt("order");kind=state.optString("kind","movie");query=state.optString("query","");title=state.optString("title","Movies");page=Math.max(1,state.optInt("page",1));id=state.optInt("id");season=state.optInt("season");restorePosition=state.optInt("scroll",0);}
    private void root(String type,String label){history.clear();link="";genre=order=0;kind=type;title=label;query="";page=1;id=season=restorePosition=0;search.setText("");hideKeyboard();load();}
    private void doSearch(){query=search.getText().toString().trim();if(kind.startsWith("kiss"))kind="kiss";else if(kind.startsWith("archive")){kind="archive";id=0;}else if(!kind.equals("movie")&&!kind.equals("tv")&&!kind.equals("music")){kind="movie";title="Movies";}history.clear();page=1;restorePosition=0;hideKeyboard();load();}
    private void hideKeyboard(){search.clearFocus();((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(search.getWindowToken(),0);}
    private void open(CatalogItem item){
        String target=null;if(item.kind.equals("tv"))target="season";if(item.kind.equals("season"))target="episode";if(item.kind.equals("kiss"))target="kiss-episodes";if(item.kind.equals("archive-folder"))target="archive";if(item.kind.equals("archive-item"))target="archive-files";
        if(target!=null){if(history.size()==10)history.removeFirst();history.addLast(navigation());kind=target;title=item.title;id=item.id;season=item.season;link=item.link;query="";page=1;restorePosition=0;search.setText("");load();}else details(item);
    }
    private void changePage(int step){if(page+step<1||page+step>pages||kind.equals("favorites"))return;page+=step;restorePosition=0;load();}
    private void goBack(){if(!history.isEmpty()){restore(history.removeLast());search.setText(query);load();}else if(!kind.equals("movie")||!query.isEmpty()||page!=1){root("movie","Movies");}else finish();}
    @Override public void onBackPressed(){goBack();}
    private void load(){
        if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);
        final int token=++generation;images.getQueue().clear();rows.clear();adapter.notifyDataSetChanged();heading.setText(title);search.setHint(kind.equals("music")?"Artist, song, YouTube / Spotify playlist":kind.startsWith("kiss")?"Search anime":"Search this library");retry.setVisibility(View.GONE);
        status.setText("Loading…");previous.setEnabled(false);next.setEnabled(false);pageLabel.setText("Page "+page);
        if(kind.equals("favorites")){rows.addAll(favorites());pages=1;finishPage(false);return;}
        final String type=kind,term=query;final int requestedPage=page,show=id,sn=season;
        MediaRepository.Job job=new MediaRepository.Job();request=job;
        final MediaRepository.Query selection=new MediaRepository.Query(type,requestedPage,term,show,sn,link,genre,order);final boolean fresh=refreshing;refreshing=false;
        pending=network.submit(()->{
            try{CatalogPage result=repository.load(selection,job,fresh);runOnUiThread(()->{if(destroyed||token!=generation)return;rows.addAll(result.items);pages=result.pages;finishPage(result.cached);});}
            catch(Exception error){runOnUiThread(()->{if(destroyed||token!=generation)return;status.setText("Could not load this page. Check your connection and try again.");retry.setVisibility(View.VISIBLE);previous.setEnabled(page>1);});}
        });
    }
    private void finishPage(boolean cached){
        adapter.notifyDataSetChanged();renderFlow();grid.setSelection(restorePosition);if(covers==null&&!search.hasFocus())grid.requestFocus();restorePosition=0;
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
        if(java.util.Arrays.asList("tv","season","kiss","archive-folder","archive-item").contains(item.kind)){open(item);return;}
        ScrollView scroll=new ScrollView(this);LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);scroll.addView(content);TextView description=label(item.meta+"\n\n"+(item.overview.isEmpty()?"No description available.":item.overview),16,INK);description.setPadding(dp(22),dp(10),dp(22),dp(12));content.addView(description);
        AlertDialog.Builder dialog=dialog().setTitle(item.title).setView(scroll).setNegativeButton("Close",null)
            .setNeutralButton(isFavorite(item)?"Remove favorite":"Favorite",(d,w)->toggleFavorite(item));
        if(item.kind.equals("tv")||item.kind.equals("season"))dialog.setPositiveButton("Browse",(d,w)->open(item));
        else dialog.setPositiveButton("Play",(d,w)->playCatalogue(item));
        AlertDialog shown=dialog.show();
        Gamepad.dialog(shown);
        if(item.kind.equals("offline")){
            shown.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(OfflineMedia.complete(this,item.link));
            content.addView(button("Retry download",()->{CatalogItem original=OfflineMedia.original(this,item.link);if(original!=null){OfflineMedia.queue(this,original);shown.dismiss();}}));
            content.addView(button("Remove download",()->{OfflineMedia.remove(this,item.link);shown.dismiss();load();}));
        }else if(!item.kind.equals("local")&&!item.kind.endsWith("channel")&&!item.kind.equals("tv")&&!item.kind.equals("season")&&!item.kind.equals("kiss"))content.addView(button("Download for offline playback",()->{if(android.os.Build.VERSION.SDK_INT>=33&&checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)!=android.content.pm.PackageManager.PERMISSION_GRANTED)requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS},78);OfflineMedia.queue(this,item);Toast.makeText(this,"Added to Downloads",Toast.LENGTH_SHORT).show();shown.dismiss();}));
    }

    private void link(CatalogItem item){
        EditText input=new EditText(this);input.setSingleLine(true);input.setTextColor(INK);input.setHint("https://… direct media URL");input.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_VARIATION_URI);
        input.setText(prefs.getString("stream:"+item.key(),""));input.setPadding(dp(22),dp(10),dp(22),dp(10));
        AlertDialog dialog=dialog().setTitle("Stream for "+item.title).setView(input).setNegativeButton("Cancel",null).setPositiveButton("Save & play",null).create();
        dialog.setOnShowListener(event->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view->{String value=input.getText().toString().trim();if(!CatalogItem.validStream(value)){input.setError("Enter a direct HTTPS media URL");return;}prefs.edit().putString("stream:"+item.key(),value).apply();dialog.dismiss();play(value,item.title,item.key());}));dialog.show();
    }
    private void playCatalogue(CatalogItem item){try{startActivity(new Intent(this,PlayerActivity.class).putExtra("mediaKind",item.kind).putExtra("mediaId",item.id)
        .putExtra("season",item.season).putExtra("episode",item.episode).putExtra("title",item.title).putExtra("resumeKey",item.key()).putExtra("item",item.json().toString()).putExtra("queue",queue(item)));}catch(Exception bad){Toast.makeText(this,"Could not open this title",Toast.LENGTH_SHORT).show();}}
    private String queue(CatalogItem item){JSONArray array=new JSONArray();if(item.kind.equals("music")||item.kind.equals("music-query")||item.kind.equals("local"))try{for(CatalogItem row:rows)if(row.kind.equals(item.kind))array.put(row.json());}catch(Exception ignored){}return array.toString();}
    private void play(String url,String label,String key){startActivity(new Intent(this,PlayerActivity.class).putExtra("url",url).putExtra("title",label).putExtra("resumeKey",key));}
    private static final String[][] SECTIONS={{"Movies","movie"},{"Series","tv"},{"KissAnime","kiss"},{"Music","music"},{"MTV","mtv"},{"TV channels","channels"},{"Archive","archive-home"},{"Continue","continue"},{"History","history"},{"Favorites","favorites"},{"Downloads","downloads"},{"Local media","local"}};
    private AlertDialog.Builder dialog(){return new AlertDialog.Builder(this,theme.id==0?android.R.style.Theme_Material_Light_Dialog_Alert:android.R.style.Theme_Material_Dialog_Alert);}
    private void choices(String title,String[] options,java.util.function.IntConsumer action){AlertDialog d=dialog().setTitle(title).setItems(options,(v,i)->action.accept(i)).setNegativeButton("Close",null).show();Gamepad.dialog(d);}
    private void sections(){String[] names=new String[SECTIONS.length];for(int i=0;i<names.length;i++)names[i]=SECTIONS[i][0];choices("Media library",names,i->root(SECTIONS[i][1],SECTIONS[i][0]));}
    private void more(){choices("FlXtR",new String[]{"Choose theme","Genre / order","Add local media","Anime: "+(settings.getBoolean("anime-dub",false)?"DUB":"SUB"),"Playback settings","TV schedule / selection","MTV mix settings","Open a direct video URL","Refresh this page","Update music engine","Controller help","Screen layout"},i->{switch(i){
        case 0:choices("Theme",LibraryTheme.NAMES,n->{settings.edit().putInt("theme",n).apply();recreate();});break;
        case 1:choices("Browse filter",new String[]{"Genre","Order"},n->choices(n==0?"Genre":"Order",n==0?MediaRepository.GENRES:MediaRepository.ORDERS,v->{if(n==0)genre=v;else order=v;page=1;load();}));break;
        case 2:Intent picker=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"video/*","audio/*"}).putExtra(Intent.EXTRA_ALLOW_MULTIPLE,true).addCategory(Intent.CATEGORY_OPENABLE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);startActivityForResult(picker,77);break;
        case 3:settings.edit().putBoolean("anime-dub",!settings.getBoolean("anime-dub",false)).apply();break;
        case 4:playbackSettings();break;case 5:tvSettings();break;case 6:mtvSettings();break;
        case 7:link(new CatalogItem("stream",0,0,0,"Direct video","","",""));break;
        case 8:refreshing=true;load();break;
        case 9:Toast.makeText(this,"Updating music engine…",Toast.LENGTH_LONG).show();network.submit(()->{try{MusicEngine.update(this);runOnUiThread(()->Toast.makeText(this,"Music engine updated",Toast.LENGTH_LONG).show());}catch(Exception failure){runOnUiThread(()->Toast.makeText(this,"Update unavailable. Try again later.",Toast.LENGTH_LONG).show());}});break;
        case 10:Gamepad.dialog(dialog().setTitle("Controller").setMessage("D-pad / left stick: navigate\nA: select / play\nB: back\nX: search\nY: details / playback options\nLB / RB: page (seek in player)\nStart: menu / pause\n\nTouch controls remain available.").setPositiveButton("OK",null).show());break;
        case 11:choices("Screen layout",new String[]{"Automatic","Phone / compact","Tablet / carousel"},n->{settings.edit().putInt("layout",n).apply();recreate();});break;
    }});}
    private void playbackSettings(){choices("Playback",new String[]{"Autoplay episodes: "+settings.getBoolean("autoplay",true),"Subtitle language","Subtitle size","Wi-Fi downloads only: "+settings.getBoolean("wifi-downloads",true)},i->{if(i==0)settings.edit().putBoolean("autoplay",!settings.getBoolean("autoplay",true)).apply();if(i==1)choices("Subtitle language",new String[]{"English","Norwegian","Spanish","Off"},n->settings.edit().putString("subtitle-language",new String[]{"en","no","es",""}[n]).apply());if(i==2)choices("Subtitle size",new String[]{"Small","Normal","Large"},n->settings.edit().putFloat("subtitle-size",new float[]{0.04f,0.0533f,0.075f}[n]).apply());if(i==3)settings.edit().putBoolean("wifi-downloads",!settings.getBoolean("wifi-downloads",true)).apply();});}
    private void number(String title,String key,int value,int min,int max){EditText input=new EditText(this);input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);input.setText(String.valueOf(settings.getInt(key,value)));AlertDialog d=dialog().setTitle(title+" ("+min+"–"+max+")").setView(input).setNegativeButton("Cancel",null).setPositiveButton("Save",(v,w)->{try{int n=Integer.parseInt(input.getText().toString());if(n<min||n>max)throw new NumberFormatException();settings.edit().putInt(key,n).apply();}catch(Exception bad){Toast.makeText(this,"Value outside range",Toast.LENGTH_SHORT).show();}}).show();Gamepad.dialog(d);}
    private void tvSettings(){String[] entries=new String[14];for(int i=0;i<10;i++)entries[i]=String.format(java.util.Locale.US,"%02d:00 · %s",settings.getInt("tv-hour-"+i,TvChannels.HOURS[i]),TvChannels.GENRES[settings.getInt("tv-genre-"+i,TvChannels.DEFAULT_GENRES[i])]);entries[10]="Rating weight";entries[11]="Popularity weight";entries[12]="Variety weight";entries[13]="Marathon: "+settings.getBoolean("tv-marathon",true);choices("Scheduled TV · Europe/Oslo",entries,i->{if(i<10)choices("Schedule block",new String[]{"Start hour","Genre"},n->{if(n==0){if(i==0)Toast.makeText(this,"First block starts at midnight",Toast.LENGTH_SHORT).show();else number("Start hour (keep blocks increasing)","tv-hour-"+i,TvChannels.HOURS[i],1,23);}else choices("Genre",TvChannels.GENRES,g->settings.edit().putInt("tv-genre-"+i,g).apply());});else if(i==13)settings.edit().putBoolean("tv-marathon",!settings.getBoolean("tv-marathon",true)).apply();else number(entries[i],new String[]{"tv-rating","tv-popularity","tv-variety"}[i-10],new int[]{5,2,4}[i-10],0,20);});}
    private void mtvSettings(){String[] options=new String[18];System.arraycopy(MtvPolicy.NAMES,0,options,0,16);options[16]="Fusion style";options[17]="Custom query";choices("MTV mix weights",options,i->{if(i<16)number(options[i],"mtv-weight-"+i,1,0,100);else if(i==16)choices("Fusion style",MtvPolicy.NAMES,n->settings.edit().putInt("mtv-fusion",n).apply());else{EditText input=new EditText(this);input.setText(settings.getString("mtv-custom",""));Gamepad.dialog(dialog().setTitle("Custom music video search").setView(input).setPositiveButton("Save",(d,w)->settings.edit().putString("mtv-custom",input.getText().toString()).apply()).setNegativeButton("Cancel",null).show());}});}
    private void renderFlow(){if(covers==null)return;covers.removeAllViews();selected=Math.max(0,Math.min(restorePosition,rows.size()-1));for(int i=0;i<rows.size();i++){final int pos=i;View card=adapter.getView(i,null,covers);card.setFocusable(true);card.setFocusableInTouchMode(true);card.setOnFocusChangeListener((v,focus)->{if(focus){selected=pos;styleFlow();flow.smoothScrollTo(Math.max(0,v.getLeft()-flow.getWidth()/2+v.getWidth()/2),0);}});card.setOnClickListener(v->open(rows.get(pos)));card.setOnLongClickListener(v->{details(rows.get(pos));return true;});LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(dp(180),-2);size.setMargins(dp(8),0,dp(8),0);covers.addView(card,size);}styleFlow();if(covers.getChildCount()>selected)covers.getChildAt(selected).requestFocus();}
    private void styleFlow(){if(covers==null)return;for(int i=0;i<covers.getChildCount();i++){View card=covers.getChildAt(i);int distance=i-selected;card.setScaleX(distance==0?1:0.86f);card.setScaleY(distance==0?1:0.86f);card.setRotationY(distance==0?0:distance<0?18:-18);card.setBackground(theme.surface(distance==0));}}
    private final Gamepad.Actions controls=new Gamepad.Actions(){public boolean button(int key){switch(key){case KeyEvent.KEYCODE_BUTTON_A:View focus=getCurrentFocus();if(focus!=null&&(focus.isClickable()||focus instanceof android.widget.AbsListView))Gamepad.send(focus,KeyEvent.KEYCODE_DPAD_CENTER);else if(!rows.isEmpty())open(rows.get(selected));return true;case KeyEvent.KEYCODE_BUTTON_B:goBack();return true;case KeyEvent.KEYCODE_BUTTON_X:search.requestFocus();((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).showSoftInput(search,InputMethodManager.SHOW_IMPLICIT);return true;case KeyEvent.KEYCODE_BUTTON_Y:if(!rows.isEmpty())details(rows.get(covers==null?Math.max(0,grid.getSelectedItemPosition()):selected));return true;case KeyEvent.KEYCODE_BUTTON_L1:changePage(-1);return true;case KeyEvent.KEYCODE_BUTTON_R1:changePage(1);return true;case KeyEvent.KEYCODE_BUTTON_START:more();return true;default:return false;}}public void direction(int key){Gamepad.send(getWindow().getDecorView(),key);}};
    @Override public boolean dispatchKeyEvent(KeyEvent e){return gamepad.key(e,controls)||super.dispatchKeyEvent(e);}
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e){return gamepad.motion(e,controls)||super.dispatchGenericMotionEvent(e);}
    @Override protected void onActivityResult(int code,int result,Intent data){super.onActivityResult(code,result,data);if(code==77&&result==RESULT_OK&&data!=null){LocalMedia.add(this,data);root("local","Local media");}}
    private final class CatalogAdapter extends BaseAdapter {
        @Override public int getCount(){return rows.size();}
        @Override public Object getItem(int index){return rows.get(index);}
        @Override public long getItemId(int index){return index;}
        @Override public View getView(int index,View reusable,android.view.ViewGroup parent){
            CatalogItem item=rows.get(index);LinearLayout card=new LinearLayout(LibraryActivity.this);card.setOrientation(LinearLayout.VERTICAL);card.setPadding(dp(7),dp(7),dp(7),dp(9));
            GradientDrawable surface=new GradientDrawable();surface.setColor(PANEL);surface.setCornerRadius(dp(12));card.setBackground(theme.focus());
            int width=covers!=null?dp(166):Math.max(dp(100),(grid.getWidth()-dp(14)*(grid.getNumColumns()-1))/Math.max(1,grid.getNumColumns())-dp(14));
            FrameLayout poster=new FrameLayout(LibraryActivity.this);TextView placeholder=label(item.kind.equals("season")?"SEASON "+item.season:"FLXTR",20,GREEN);placeholder.setGravity(Gravity.CENTER);poster.addView(placeholder,new FrameLayout.LayoutParams(-1,-1));
            ImageView image=new ImageView(LibraryActivity.this);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setContentDescription(item.title);poster.addView(image,new FrameLayout.LayoutParams(-1,-1));card.addView(poster,new LinearLayout.LayoutParams(-1,width*3/2));
            TextView name=label(item.title,compactLayout?13:15,INK);name.setMaxLines(2);name.setMinLines(2);name.setEllipsize(android.text.TextUtils.TruncateAt.END);name.setPadding(0,dp(8),0,dp(4));card.addView(name);
            TextView meta=label(item.meta,compactLayout?10:11,MUTED);meta.setSingleLine(true);meta.setEllipsize(android.text.TextUtils.TruncateAt.END);card.addView(meta);
            if(!item.poster.isEmpty()){
                Bitmap bitmap=art.get(item.poster);if(bitmap!=null)image.setImageBitmap(bitmap);
                else{final int token=generation;images.execute(()->{if(token!=generation||destroyed)return;try{Bitmap loaded=client.poster(item.poster);if(loaded!=null){art.put(item.poster,loaded);runOnUiThread(()->{if(token==generation&&!destroyed)image.setImageBitmap(loaded);});}}catch(Exception ignored){/* Keep the labeled placeholder. */}});}
            }
            return card;
        }
    }
    @Override protected void onSaveInstanceState(Bundle state){state.putString("navigation",navigation().toString());JSONArray stack=new JSONArray();for(JSONObject entry:history)stack.put(entry);state.putString("history",stack.toString());state.putInt("scroll",(covers==null?grid.getFirstVisiblePosition():selected));super.onSaveInstanceState(state);}
    @Override protected void onStop(){gamepad.reset();super.onStop();}
    @Override protected void onDestroy(){destroyed=true;++generation;if(request!=null)request.cancel();if(pending!=null)pending.cancel(true);network.shutdownNow();images.shutdownNow();client.trimImages();super.onDestroy();}
}
