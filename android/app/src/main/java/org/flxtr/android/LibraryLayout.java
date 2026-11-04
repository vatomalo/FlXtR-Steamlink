package org.flxtr.android;
/** Layout follows the usable window rather than a device model or pixel count. */
final class LibraryLayout {
 static boolean compact(int choice,int widthDp,int heightDp){return choice==1||(choice!=2&&(widthDp<600||heightDp<480));}
 static int columns(int widthDp,boolean compact){return Math.max(2,Math.min(compact?8:6,widthDp/(compact?145:175)));}
 static boolean landscape(int widthDp,int heightDp){return widthDp>heightDp;}
 static int columns(int widthDp,int heightDp,boolean compact){return compact&&Math.min(widthDp,heightDp)<600?Math.max(3,Math.min(12,widthDp/96)):columns(widthDp,compact);}
 static int posterHeight(int widthDp,int windowWidthDp,int heightDp){return Math.min(windowWidthDp,heightDp)<600&&landscape(windowWidthDp,heightDp)?Math.min(widthDp*3/2,Math.max(64,heightDp*3/10)):widthDp*3/2;}
}
