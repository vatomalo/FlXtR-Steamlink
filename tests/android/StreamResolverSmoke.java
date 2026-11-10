package org.flxtr.android;
import java.net.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public final class StreamResolverSmoke {
    private static byte[] read(String url,StreamProtocol.Stream stream,int limit) throws Exception {
        javax.net.ssl.HttpsURLConnection c=(javax.net.ssl.HttpsURLConnection)new URL(url).openConnection();
        c.setConnectTimeout(10000);c.setReadTimeout(15000);for(Map.Entry<String,String> h:stream.headers.entrySet())c.setRequestProperty(h.getKey(),h.getValue());c.setRequestProperty("Range","bytes=0-65535");
        try(InputStream in=c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] b=new byte[1024];int n;while(out.size()<limit&&(n=in.read(b,0,Math.min(b.length,limit-out.size())))>0)out.write(b,0,n);return out.toByteArray();}finally{c.disconnect();}
    }
    public static void main(String[] args) throws Exception {
        synchronized(StreamDecoder.LOCK){
            try{
                String secret=StreamDecoder.nativeBegin();boolean rejected=false;
                try{new org.json.JSONObject(StreamDecoder.nativeDecode("invalid-response",secret));}
                catch(IOException|org.json.JSONException expected){rejected=true;}
                if(!rejected)throw new IOException("Malformed decoder response unexpectedly accepted");
            }finally{StreamDecoder.nativeEnd();}
        }
        CatalogItem item=args.length>0&&args[0].equals("episode")?new CatalogItem("episode",1416,1,1,"Episode test","","",""):new CatalogItem("movie",550,0,0,"Movie test","","","");
        StreamResolver.Session session=new StreamResolver.Session(item);
        StreamProtocol.Stream stream=session.next(new StreamResolver.Request(),System.out::println);
        System.out.println("Resolved "+item.kind+" server="+stream.server+" HLS="+"application/x-mpegURL".equals(stream.mime));
        String uri=stream.url;
        for(int depth=0;depth<4;depth++){
            byte[] bytes=read(uri,stream,65536);String text=new String(bytes,StandardCharsets.UTF_8);
            if(!text.startsWith("#EXTM3U")){if(bytes.length<100)throw new IOException("Media fragment is empty");System.out.println("Playable media fragment fetched: "+bytes.length+" bytes");return;}
            String next=null;for(String line:text.split("\r?\n")){if(!line.isEmpty()&&!line.startsWith("#")){next=line.trim();break;}}
            if(next==null)throw new IOException("Playlist has no media entries");uri=new URL(new URL(uri),next).toString();
        }
        throw new IOException("Playlist nesting exceeded test limit");
    }
}
