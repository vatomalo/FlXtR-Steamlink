package org.flxtr.android;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import java.io.*;
import java.nio.charset.StandardCharsets;

public final class AdventureDocumentsProvider extends DocumentsProvider {
    static volatile int generation=1;
    private static final String[] COLUMNS={"document_id","_display_name","mime_type","_size","last_modified","flags"};
    private void row(MatrixCursor cursor,String id,String name,boolean directory){cursor.addRow(new Object[]{id,name,directory?DocumentsContract.Document.MIME_TYPE_DIR:"application/octet-stream",directory?0:bytes(id).length,generation*1000L,0});}
    private static byte[] bytes(String id){return (id+":"+generation).getBytes(StandardCharsets.UTF_8);}
    @Override public boolean onCreate(){return true;}
    @Override public boolean isChildDocument(String parent,String child){return parent.equals("root")||child.startsWith(parent+"/");}
    @Override public Cursor queryRoots(String[] projection){MatrixCursor cursor=new MatrixCursor(new String[]{"root_id","document_id","title","flags"});cursor.addRow(new Object[]{"root","root","Adventure fixture",0});return cursor;}
    @Override public Cursor queryDocument(String id,String[] projection){MatrixCursor cursor=new MatrixCursor(COLUMNS);row(cursor,id,id.equals("root")?"Adventure fixture":id.substring(id.lastIndexOf('/')+1),id.equals("root")||id.equals("audio"));return cursor;}
    @Override public Cursor queryChildDocuments(String id,String[] projection,String order){MatrixCursor cursor=new MatrixCursor(COLUMNS);if(id.equals("root")){row(cursor,"audio","audio",true);row(cursor,"sky.dnr","sky.dnr",false);row(cursor,"sky.dsk","sky.dsk",false);}else if(id.equals("audio"))row(cursor,"audio/voice.dat","voice.dat",false);return cursor;}
    @Override public ParcelFileDescriptor openDocument(String id,String mode,CancellationSignal signal)throws FileNotFoundException {if(!mode.equals("r"))throw new FileNotFoundException();try{ParcelFileDescriptor[] pipe=ParcelFileDescriptor.createPipe();new Thread(()->{try(OutputStream output=new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])){output.write(bytes(id));}catch(IOException ignored){}},"adventure-fixture").start();return pipe[0];}catch(IOException failure){throw new FileNotFoundException(failure.toString());}}
}
