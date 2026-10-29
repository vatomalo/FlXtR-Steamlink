package org.flxtr.android;

import org.json.JSONObject;
import java.io.*;
import java.net.URL;
import javax.net.ssl.HttpsURLConnection;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.*;

final class StreamResolver {
    interface Progress {void update(String message);}
    static final class Request {
        volatile boolean cancelled;
        volatile HttpsURLConnection connection;
        final long deadline=System.nanoTime()+90_000_000_000L;
        void cancel(){cancelled=true;HttpsURLConnection active=connection;if(active!=null)active.disconnect();}
        void check() throws IOException {if(cancelled||Thread.currentThread().isInterrupted())throw new InterruptedIOException("Cancelled");if(System.nanoTime()>deadline)throw new IOException("Stream lookup timed out");}
    }
    static final class Session {
        private final CatalogItem item;
        private List<String> servers;
        private int next;
        Session(CatalogItem item){this.item=item;}
        StreamProtocol.Stream next(Request request,Progress progress) throws Exception {
            String path=StreamProtocol.path(item);
            if(servers==null){progress.update("Finding available servers…");servers=StreamProtocol.servers(lookup(path,"",request));}
            while(next<servers.size()){
                request.check();String server=servers.get(next++);progress.update("Opening server "+next+" of "+servers.size()+"…");
                try{StreamProtocol.Stream stream=StreamProtocol.stream(lookup(path,server,request),server);probe(stream,request);request.check();return stream;}
                catch(Exception unavailable){request.check();/* Try the next provider; never log signed media URLs. */}
            }
            throw new IOException("No playable stream was returned. Please retry or choose another title.");
        }
    }
    private static JSONObject lookup(String path,String server,Request request) throws Exception {
        request.check();
        synchronized(StreamDecoder.LOCK){
            request.check();
            try{
                String key=StreamDecoder.nativeBegin();
                if(key==null||key.length()!=64)throw new IOException("Stream decoder unavailable");
                JSONObject clock=new JSONObject(text(StreamProtocol.BASE+"/api/time",Collections.emptyMap(),request));
                long time=clock.optLong("timestamp",0);if(time<=0)throw new IOException("Provider clock unavailable");
                String timestamp=Long.toString(time);byte[] random=new byte[16];new SecureRandom().nextBytes(random);
                String nonce=Base64.getEncoder().withoutPadding().encodeToString(random).replace("/","").replace("+","");
                Map<String,String> headers=new HashMap<>();headers.put("Origin","https://flixer.gd");headers.put("Referer","https://flixer.gd/");headers.put("Accept","text/plain");
                headers.put("x-fingerprint-lite","b4f8a1fc72e905d63e");headers.put("X-Api-Key",key);headers.put("X-Request-Timestamp",timestamp);headers.put("X-Request-Nonce",nonce);
                headers.put("X-Request-Signature",StreamProtocol.signature(key,timestamp,nonce,path));headers.put("X-Client-Fingerprint",StreamProtocol.fingerprint());
                if(server.isEmpty())headers.put("bW90aGFmYWth","1");else{if(!StreamProtocol.validServer(server))throw new IOException("Invalid server");headers.put("X-Only-Sources","1");headers.put("X-Server",server);}
                String raw=text(StreamProtocol.BASE+path,headers,request);request.check();
                String decoded=StreamDecoder.nativeDecode(raw,key);request.check();return new JSONObject(decoded);
            }finally{StreamDecoder.nativeEnd();}
        }
    }
    private static HttpsURLConnection connect(String url,Map<String,String> headers,Request request) throws IOException {
        request.check();if(!CatalogItem.validStream(url))throw new IOException("Invalid HTTPS media URL");
        HttpsURLConnection connection=(HttpsURLConnection)new URL(url).openConnection();request.connection=connection;
        connection.setConnectTimeout(7000);connection.setReadTimeout(12000);connection.setInstanceFollowRedirects(false);connection.setRequestProperty("User-Agent",StreamProtocol.UA);
        for(Map.Entry<String,String> header:headers.entrySet())connection.setRequestProperty(header.getKey(),header.getValue());
        request.check();return connection;
    }
    private static String text(String url,Map<String,String> headers,Request request) throws IOException {
        HttpsURLConnection connection=connect(url,headers,request);
        try{
            if(connection.getResponseCode()!=200)throw new IOException("Provider request failed");
            if(connection.getContentLengthLong()>2*1024*1024)throw new IOException("Provider response is too large");
            try(InputStream in=connection.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){
                byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1){request.check();if(out.size()+n>2*1024*1024)throw new IOException("Provider response is too large");out.write(buffer,0,n);}
                return new String(out.toByteArray(),StandardCharsets.UTF_8);
            }
        }finally{request.connection=null;connection.disconnect();}
    }
    private static void probe(StreamProtocol.Stream stream,Request request) throws IOException {
        String url=stream.url;Map<String,String> headers=new HashMap<>(stream.headers);headers.put("Range","bytes=0-65535");
        for(int redirect=0;redirect<=3;redirect++){
            HttpsURLConnection connection=connect(url,headers,request);
            try{
                int code=connection.getResponseCode();
                if(code>=300&&code<400){String location=connection.getHeaderField("Location");if(location==null)throw new IOException("Missing stream redirect");url=new URL(new URL(url),location).toString();continue;}
                if(code!=200&&code!=206)throw new IOException("Stream is unavailable");
                String type=connection.getContentType();
                try(InputStream in=connection.getInputStream()){
                    byte[] prefix=new byte[1024];int used=0,n;while(used<prefix.length&&(n=in.read(prefix,used,prefix.length-used))>0){request.check();used+=n;}
                    String head=new String(prefix,0,used,StandardCharsets.UTF_8).trim();
                    if(head.startsWith("#EXTM3U")||(type!=null&&type.toLowerCase(Locale.ROOT).contains("mpegurl")))stream.mime="application/x-mpegURL";
                    else if(used<1||head.startsWith("<!DOCTYPE")||head.startsWith("<html")||(type!=null&&type.contains("text/html")))throw new IOException("Server returned a web page instead of video");
                    return;
                }
            }finally{request.connection=null;connection.disconnect();}
        }
        throw new IOException("Too many media redirects");
    }
}
