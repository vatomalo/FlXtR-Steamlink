package org.flxtr.android;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.*;
import android.view.*;
import android.widget.*;
final class LibraryTheme {
    static final String[] NAMES={"Original · White Metal","Original · Green / Black","Touch · Current"};
    final int id,bg,panel,ink,accent,muted;final Typeface font;
    LibraryTheme(Context context,int choice){
        id=Math.max(0,Math.min(2,choice));
        bg=id==0?0xffe8edeb:id==1?0xff000000:0xff0b1115;panel=id==0?0xeef6f8f5:id==1?0xff08170e:0xff152129;
        ink=id==0?0xff18201e:id==1?0xffd4e2d6:0xffe4eee8;accent=id==0?0xff125b38:id==1?0xff74ff84:0xff87edac;muted=id==0?0xff3e4a48:id==1?0xff427e4d:0xff91a69d;
        Typeface pixel;try{pixel=Typeface.createFromAsset(context.getAssets(),"fonts/flxtr-pixel.ttf");}catch(Exception absent){pixel=Typeface.MONOSPACE;}
        font=id==2?Typeface.MONOSPACE:pixel;
    }
    boolean original(){return id!=2;}
    Drawable background(Context context){
        if(id==0){try(java.io.InputStream in=context.getAssets().open("theme/white-metal.png")){Bitmap bitmap=BitmapFactory.decodeStream(in);if(bitmap!=null){BitmapDrawable drawable=new BitmapDrawable(context.getResources(),bitmap);drawable.setGravity(Gravity.FILL);return drawable;}}catch(Exception ignored){}}
        return new ColorDrawable(bg);
    }
    Drawable surface(boolean focused){GradientDrawable d=new GradientDrawable();d.setColor(panel);d.setCornerRadius(id==2?12:0);d.setStroke(focused?4:1,focused?accent:(id==0?0xffb9c8bd:muted));return d;}
    Drawable selection(){GradientDrawable border=new GradientDrawable();border.setColor(android.graphics.Color.TRANSPARENT);border.setStroke(4,accent);border.setCornerRadius(id==2?12:0);return border;}
    Drawable focus(){StateListDrawable state=new StateListDrawable();state.addState(new int[]{android.R.attr.state_pressed},surface(true));state.addState(new int[]{android.R.attr.state_focused},surface(true));state.addState(new int[]{android.R.attr.state_selected},surface(true));state.addState(new int[]{},surface(false));return state;}
    TextView text(Context context,String value,int size,int color){TextView view=new TextView(context);view.setText(value);view.setTextSize(size);view.setTextColor(color);view.setTypeface(font);return view;}
}
