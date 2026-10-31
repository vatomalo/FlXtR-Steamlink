package org.flxtr.android;
import android.provider.DocumentsProvider;
import android.provider.DocumentsContract;
import android.database.*;
import android.os.*;
import java.io.*;

/** Debug-only, synthetic document tree. No access to user files. */
public final class GameTestDocumentsProvider extends DocumentsProvider {
 public static volatile int generation=1;
 private static final String[] DOC={"document_id","_display_name","mime_type","_size","last_modified","flags"};
 @Override public boolean onCreate(){return true;}
 @Override public boolean isChildDocument(String parent,String child){return parent.equals("root")||child.startsWith(parent+"/");}
 @Override public Cursor queryRoots(String[] projection){MatrixCursor c=new MatrixCursor(new String[]{"root_id","document_id","title","flags"});c.addRow(new Object[]{"root","root","FlXtR scan fixture",0});return c;}
 private void row(MatrixCursor c,String id,String name,boolean dir){c.addRow(new Object[]{id,name,dir?DocumentsContract.Document.MIME_TYPE_DIR:"application/octet-stream",dir?0:bytes(id).length,generation*1000L,0});}
 @Override public Cursor queryDocument(String id,String[] projection){MatrixCursor c=new MatrixCursor(DOC);row(c,id,id.equals("root")?"Fixture":id.substring(id.lastIndexOf('/')+1),id.equals("root")||id.equals("NES")||id.equals("PS"));return c;}
 @Override public Cursor queryChildDocuments(String id,String[] projection,String sortOrder)throws FileNotFoundException {if(generation==4)throw new FileNotFoundException("Test folder unavailable");MatrixCursor c=new MatrixCursor(DOC);if(id.equals("root")){row(c,"NES","NES",true);row(c,"PS","PS",true);row(c,"demo.gb","demo.gb",false);}else if(id.equals("NES")){row(c,"NES/demo.nes","demo.nes",false);if(generation>=2)row(c,"NES/new.gba","new.gba",false);}else if(id.equals("PS")){row(c,"PS/disc.cue","disc.cue",false);row(c,"PS/set.m3u","set.m3u",false);if(generation!=3)row(c,"PS/Track 01.bin","Track 01.bin",false);}return c;}
 private static byte[] bytes(String id){if(id.endsWith(".cue"))return "FILE \"Track 01.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n".getBytes(java.nio.charset.StandardCharsets.UTF_8);if(id.endsWith(".m3u"))return "disc.cue\n".getBytes(java.nio.charset.StandardCharsets.UTF_8);byte[] data=new byte[1024];java.util.Arrays.fill(data,(byte)generation);return data;}
 @Override public ParcelFileDescriptor openDocument(String id,String mode,CancellationSignal signal)throws FileNotFoundException {if(!mode.equals("r"))throw new FileNotFoundException();try{ParcelFileDescriptor[] pipe=ParcelFileDescriptor.createPipe();new Thread(()->{try(OutputStream out=new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])){out.write(bytes(id));}catch(IOException ignored){}},"fixture-doc").start();return pipe[0];}catch(IOException e){throw new FileNotFoundException(e.getMessage());}}
}
