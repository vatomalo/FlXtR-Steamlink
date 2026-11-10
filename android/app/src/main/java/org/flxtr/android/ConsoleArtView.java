package org.flxtr.android;

import android.content.Context;
import android.graphics.*;
import android.view.View;

/** Locally drawn console/cartridge art, matching Steam Link's fallback covers. */
final class ConsoleArtView extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final String code;
    private final boolean folder;
    private final Typeface font;
    ConsoleArtView(Context context,String code,boolean folder,Typeface font){super(context);this.code=code;this.folder=folder;this.font=font;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    @Override protected void onDraw(Canvas c){super.onDraw(c);float w=getWidth(),h=getHeight(),unit=Math.min(w,h)/100f;paint.setColor(0xff08170e);c.drawColor(0xff08170e);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2*unit);paint.setColor(0xff427e4d);
        if(folder){c.drawRect(w*.15f,h*.27f,w*.85f,h*.75f,paint);c.drawLine(w*.15f,h*.27f,w*.15f,h*.19f,paint);c.drawLine(w*.15f,h*.19f,w*.43f,h*.19f,paint);c.drawLine(w*.43f,h*.19f,w*.5f,h*.27f,paint);}else{c.drawRect(w*.2f,h*.12f,w*.8f,h*.86f,paint);c.drawRect(w*.27f,h*.2f,w*.73f,h*.56f,paint);c.drawLine(w*.3f,h*.77f,w*.7f,h*.77f,paint);}
        paint.setStyle(Paint.Style.FILL);paint.setTypeface(font);paint.setTextAlign(Paint.Align.CENTER);paint.setColor(0xff74ff84);paint.setTextSize(Math.min(24*unit,w/(Math.max(2,code.length())*.8f)));c.drawText(code,w/2,h*.52f,paint);
    }
}
