package org.flxtr.android;
/** Layout follows the usable window rather than a device model or pixel count. */
final class LibraryLayout {
 static boolean compact(int choice,int widthDp,int heightDp){return choice==1||(choice!=2&&(widthDp<600||heightDp<480));}
 static int columns(int widthDp,boolean compact){return Math.max(2,Math.min(compact?8:6,widthDp/(compact?145:175)));}
}
