package org.flxtr.android;
import android.app.*;
import android.view.*;
import java.util.*;
final class Gamepad {
    interface Actions {boolean button(int key);void direction(int key);}
    private final HashMap<Integer,DirectionRepeater> repeats=new HashMap<>();
    private final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());private float heldX,heldY;private int heldDevice;private Actions actions;
    private final Runnable tick=new Runnable(){public void run(){if(actions==null)return;DirectionRepeater repeat=repeats.get(heldDevice);if(repeat!=null)direction(repeat.update(heldX,heldY,android.os.SystemClock.uptimeMillis()),actions);handler.postDelayed(this,80);}};
    void reset(){handler.removeCallbacks(tick);actions=null;repeats.clear();}
    private void direction(int direction,Actions listener){if(direction!=0)listener.direction(direction==1?KeyEvent.KEYCODE_DPAD_LEFT:direction==2?KeyEvent.KEYCODE_DPAD_RIGHT:direction==3?KeyEvent.KEYCODE_DPAD_UP:KeyEvent.KEYCODE_DPAD_DOWN);}

    boolean key(KeyEvent event,Actions listener){
        int code=event.getKeyCode();boolean button=code>=KeyEvent.KEYCODE_BUTTON_A&&code<=KeyEvent.KEYCODE_BUTTON_MODE;
        if(!button)return false;
        if(event.getAction()==KeyEvent.ACTION_UP)return handled(code);
        if(event.getAction()!=KeyEvent.ACTION_DOWN||event.getRepeatCount()>0)return handled(code);
        return listener.button(code);
    }
    private static boolean handled(int code){return code==96||code==97||code==99||code==100||code==102||code==103||code==108||code==109;}
    boolean motion(MotionEvent event,Actions listener){
        if(!event.isFromSource(InputDevice.SOURCE_JOYSTICK)||event.getAction()!=MotionEvent.ACTION_MOVE)return false;
        float x=event.getAxisValue(MotionEvent.AXIS_HAT_X),y=event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if(Math.abs(x)<0.5f&&Math.abs(y)<0.5f){x=event.getAxisValue(MotionEvent.AXIS_X);y=event.getAxisValue(MotionEvent.AXIS_Y);}
        DirectionRepeater repeat=repeats.get(event.getDeviceId());if(repeat==null){repeat=new DirectionRepeater();repeats.put(event.getDeviceId(),repeat);}
        direction(repeat.update(x,y,event.getEventTime()),listener);handler.removeCallbacks(tick);heldX=x;heldY=y;heldDevice=event.getDeviceId();actions=Math.max(Math.abs(x),Math.abs(y))<0.5f?null:listener;if(actions!=null)handler.postDelayed(tick,80);return true;
    }
    static void send(View view,int key){view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN,key));view.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP,key));}
    static void dialog(AlertDialog dialog){
        if(dialog.getListView()!=null)dialog.getListView().requestFocus();else if(dialog.getButton(AlertDialog.BUTTON_POSITIVE)!=null)dialog.getButton(AlertDialog.BUTTON_POSITIVE).requestFocus();
        Gamepad pad=new Gamepad();View decor=dialog.getWindow().getDecorView();Actions listener=new Actions(){
            public boolean button(int key){if(key==KeyEvent.KEYCODE_BUTTON_A){send(decor,KeyEvent.KEYCODE_DPAD_CENTER);return true;}if(key==KeyEvent.KEYCODE_BUTTON_B||key==KeyEvent.KEYCODE_BUTTON_START){dialog.dismiss();return true;}return false;}
            public void direction(int key){send(decor,key);}
        };
        dialog.setOnDismissListener(d->pad.reset());dialog.setOnKeyListener((d,key,event)->pad.key(event,listener));decor.setOnGenericMotionListener((view,event)->pad.motion(event,listener));
    }
}
