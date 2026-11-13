package org.flxtr.android;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.os.Bundle;
import android.view.*;
import android.widget.*;

/** Shared Steam Link presentation, sized for both a phone and a controller. */
final class ConsoleMenu {
    final Activity activity;
    final LibraryTheme theme;
    ConsoleMenu(Activity activity){this.activity=activity;theme=new LibraryTheme(activity,1);activity.getWindow().setStatusBarColor(Color.BLACK);activity.getWindow().setNavigationBarColor(Color.BLACK);}
    int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    TextView text(String label,int size,int color){return theme.text(activity,label,size,color);}
    Button button(String label,Runnable action){Button b=new Button(activity);b.setText(label);b.setTypeface(theme.font);b.setAllCaps(false);b.setTextSize(12);b.setTextColor(theme.ink);b.setBackground(theme.focus());b.setPadding(dp(8),dp(6),dp(8),dp(6));b.setOnClickListener(v->action.run());b.setMinHeight(dp(44));b.setMinimumWidth(0);return b;}
    void divider(LinearLayout root){View line=new View(activity);line.setBackgroundColor(theme.muted);root.addView(line,new LinearLayout.LayoutParams(-1,dp(1)));}
    LinearLayout row(String label,String value,Runnable action){LinearLayout row=new LinearLayout(activity);row.setGravity(Gravity.CENTER_VERTICAL);row.setPadding(dp(10),dp(8),dp(10),dp(8));row.setClickable(true);row.setFocusable(true);row.setBackground(theme.focus());TextView key=text(label,15,theme.ink),selected=text(value,15,theme.accent);row.addView(key,new LinearLayout.LayoutParams(0,dp(36),.38f));row.addView(selected,new LinearLayout.LayoutParams(0,dp(36),.62f));key.setGravity(Gravity.CENTER_VERTICAL);selected.setGravity(Gravity.CENTER_VERTICAL);row.setOnClickListener(v->action.run());row.setContentDescription(label+": "+value);return row;}
    AlertDialog choices(String title,String[] values,java.util.function.IntConsumer selected){AlertDialog dialog=new AlertDialog.Builder(activity,R.style.FlxtrConsoleDialog).setTitle(title).setItems(values,(d,i)->selected.accept(i)).setNegativeButton("B BACK",null).show();Gamepad.dialog(dialog);return dialog;}
static String coreName(String core){switch(core){case "ppsspp":return "PPSSPP";case "dolphin":return "DOLPHIN";case "scummvm":return "SCUMMVM";case "gambatte":return "GAMBATTE";case "fceumm":return "FCEUMM";case "snes9x":return "SNES9X";case "som_ai":return "SECRET OF MANA AI";case "mgba":return "MGBA";case "pcsx_rearmed":return "PCSX REARMED";default:return "AUTOMATIC";}}

    /** Connects extra navigation on the embedded Moonlight layouts. */
    static void installMoonlightNavigation(Application app){app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
        private final java.util.IdentityHashMap<Activity,Gamepad> pads=new java.util.IdentityHashMap<>();
        private boolean moonlight(Activity a){return a.getClass().getName().startsWith("com.limelight.")&&!a.getClass().getName().equals("com.limelight.Game");}
        private void wire(Activity a){View back=a.findViewById(R.id.flxtr_console_back);if(back!=null&&back.getTag()==null){back.setTag(Boolean.TRUE);back.setOnClickListener(v->a.finish());}View settings=a.findViewById(R.id.flxtr_console_settings);if(settings!=null&&settings.getTag()==null){settings.setTag(Boolean.TRUE);settings.setOnClickListener(v->a.startActivity(new Intent(a,com.limelight.preferences.StreamSettings.class)));}}
        private void keys(Activity a){if(!(a instanceof com.limelight.PcView)&&!(a instanceof com.limelight.AppView)&&!(a instanceof com.limelight.preferences.StreamSettings)&&!(a instanceof com.limelight.preferences.AddComputerManually))return;Gamepad pad=pads.get(a);if(pad==null){pad=new Gamepad();pads.put(a,pad);}Gamepad controlsPad=pad;View decor=a.getWindow().getDecorView();Gamepad.Actions controls=new Gamepad.Actions(){public boolean button(int key){View focus=a.getCurrentFocus();if(key==KeyEvent.KEYCODE_BUTTON_B){a.onBackPressed();return true;}if(key==KeyEvent.KEYCODE_BUTTON_START){if(a instanceof com.limelight.PcView||a instanceof com.limelight.AppView)a.startActivity(new Intent(a,com.limelight.preferences.StreamSettings.class));else a.onBackPressed();return true;}if(key==KeyEvent.KEYCODE_BUTTON_X&&a instanceof com.limelight.PcView){a.startActivity(new Intent(a,com.limelight.preferences.AddComputerManually.class));return true;}if(key==KeyEvent.KEYCODE_BUTTON_Y||key==KeyEvent.KEYCODE_BUTTON_X){View list=a.findViewById(com.limelight.R.id.fragmentView);if(list instanceof AbsListView){View selected=((AbsListView)list).getSelectedView();if(selected!=null)a.openContextMenu(selected);}return true;}if(key==KeyEvent.KEYCODE_BUTTON_A){if(focus instanceof AbsListView){AbsListView list=(AbsListView)focus;int selected=list.getSelectedItemPosition();if(selected<0){list.setSelection(0);selected=0;}View child=list.getSelectedView();if(child!=null&&list.getAdapter()!=null&&selected<list.getAdapter().getCount())list.performItemClick(child,selected,list.getAdapter().getItemId(selected));}else if(focus!=null)focus.performClick();return true;}return false;}public void direction(int direction){Gamepad.send(decor,direction);}};bind(decor,controlsPad,controls);}
        private void bind(View view,Gamepad pad,Gamepad.Actions controls){if(view.isFocusable()&&view.getTag(R.id.flxtr_controller_bound)!=pad){view.setTag(R.id.flxtr_controller_bound,pad);view.setOnKeyListener((v,key,event)->pad.key(event,controls));view.setOnGenericMotionListener((v,event)->pad.motion(event,controls));}if(view instanceof android.view.ViewGroup){android.view.ViewGroup group=(android.view.ViewGroup)view;for(int i=0;i<group.getChildCount();i++)bind(group.getChildAt(i),pad,controls);}}
        public void onActivityCreated(Activity a,Bundle state){if(moonlight(a)){a.getWindow().setStatusBarColor(Color.BLACK);a.getWindow().setNavigationBarColor(Color.BLACK);a.getWindow().getDecorView().getViewTreeObserver().addOnGlobalLayoutListener(()->{wire(a);keys(a);});}}
        public void onActivityResumed(Activity a){if(moonlight(a))a.getWindow().getDecorView().post(()->wire(a));}
        public void onActivityStarted(Activity a){}public void onActivityPaused(Activity a){Gamepad pad=pads.get(a);if(pad!=null)pad.reset();}public void onActivityStopped(Activity a){}public void onActivitySaveInstanceState(Activity a,Bundle state){}public void onActivityDestroyed(Activity a){Gamepad pad=pads.remove(a);if(pad!=null)pad.reset();}
    });}
}
