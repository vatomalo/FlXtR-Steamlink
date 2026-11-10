package org.flxtr.android;

import android.content.Context;
import android.util.AtomicFile;
import java.io.*;
import java.util.zip.*;
import java.net.*;
import java.security.MessageDigest;

final class CoreAssets {
    static synchronized void install(Context context,String core)throws IOException {
        if(!core.equals("ppsspp")&&!core.equals("dolphin")&&!core.equals("scummvm"))return;
        String archiveName=core.equals("ppsspp")?"PPSSPP":core.equals("dolphin")?"Dolphin":"ScummVM";
        String expected=core.equals("ppsspp")?"113a1b7fc5ed7efaf6916733006250dddfcb9052689bd3d5d9a40759689528b6":core.equals("dolphin")?"485f623230dd0c271c24fe7635484c4de48d3e9d25a5872827dd3fb654942c58":"097c802265451fd29cc903816006591deec1bc5b08838e8dfc98d7a65247374e";
        File root=new File(context.getFilesDir(),"games/system"),marker=new File(root,".assets-"+core+"-"+expected);
        if(marker.isFile())return;
        if(!root.isDirectory()&&!root.mkdirs())throw new IOException("Cannot create engine asset folder");
        File source=download(context,archiveName,expected);long total=0;int count=0;
        try(ZipInputStream archive=new ZipInputStream(new FileInputStream(source))){
            ZipEntry entry;byte[] buffer=new byte[32768];
            while((entry=archive.getNextEntry())!=null){
                if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Asset installation cancelled");
                if(++count>20000)throw new IOException("Too many engine assets");
                File target=new File(root,entry.getName());
                if(!target.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("Invalid engine asset path");
                if(entry.isDirectory()){if(!target.isDirectory()&&!target.mkdirs())throw new IOException("Cannot create asset directory");continue;}
                if(!target.getParentFile().isDirectory()&&!target.getParentFile().mkdirs())throw new IOException("Cannot create asset directory");
                AtomicFile file=new AtomicFile(target);FileOutputStream out=file.startWrite();long size=0;
                try{int read;while((read=archive.read(buffer))!=-1){size+=read;total+=read;if(size>64L*1024*1024||total>512L*1024*1024)throw new IOException("Engine assets exceed size limit");out.write(buffer,0,read);}file.finishWrite(out);}catch(IOException failure){file.failWrite(out);throw failure;}
            }
        }
        if(count==0)throw new IOException("Engine asset archive is empty");
        if(!marker.createNewFile())throw new IOException("Cannot mark engine assets installed");
        source.delete();
    }
    private static File download(Context context,String name,String expected)throws IOException {
        File target=new File(context.getCacheDir(),"engine-"+name+".zip");
        if(target.isFile()&&target.length()<=96L*1024*1024&&expected.equals(hash(target)))return target;
        HttpURLConnection connection=(HttpURLConnection)new URL("https://github.com/vatomalo/FlXtR-Steamlink/releases/download/android-engines-2026.11.04/"+name+".zip").openConnection();
        connection.setConnectTimeout(15000);connection.setReadTimeout(15000);
        try{if(connection.getResponseCode()!=200)throw new IOException("Cannot download "+name+" assets");
            MessageDigest digest=MessageDigest.getInstance("SHA-256");long length=0;
            try(InputStream input=connection.getInputStream();OutputStream output=new FileOutputStream(target)){
                byte[] buffer=new byte[32768];int read;while((read=input.read(buffer))!=-1){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Asset download cancelled");length+=read;if(length>96L*1024*1024)throw new IOException("Engine download exceeds limit");digest.update(buffer,0,read);output.write(buffer,0,read);}
            }
            StringBuilder hash=new StringBuilder();for(byte value:digest.digest())hash.append(String.format(java.util.Locale.ROOT,"%02x",value&255));
            if(!expected.equals(hash.toString()))throw new IOException("Engine assets changed upstream; an app update is required");return target;
        }catch(java.security.NoSuchAlgorithmException unavailable){target.delete();throw new IOException("SHA-256 unavailable",unavailable);}catch(IOException failure){target.delete();throw failure;}finally{connection.disconnect();}
    }
    private static String hash(File file)throws IOException {try{MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream input=new FileInputStream(file)){byte[] buffer=new byte[32768];int read;while((read=input.read(buffer))!=-1){if(Thread.currentThread().isInterrupted())throw new InterruptedIOException("Asset check cancelled");digest.update(buffer,0,read);}}StringBuilder result=new StringBuilder();for(byte value:digest.digest())result.append(String.format(java.util.Locale.ROOT,"%02x",value&255));return result.toString();}catch(java.security.NoSuchAlgorithmException unavailable){throw new IOException(unavailable);}}
}
