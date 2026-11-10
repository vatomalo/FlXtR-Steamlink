package org.flxtr.android;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import javax.net.ssl.HttpsURLConnection;

/** HTTPS only, bounded responses, explicit redirects, cancellable connections. */
final class MediaHttp {
    static byte[] bytes(String address,Map<String,String> headers,int limit,StreamResolver.Request request) throws IOException {
        String url=address;
        for(int hop=0;hop<6;hop++){
            request.check();if(!CatalogItem.validStream(url))throw new IOException("Invalid media address");
            HttpsURLConnection c=(HttpsURLConnection)new URL(url).openConnection();request.connection=c;
            c.setConnectTimeout(8000);c.setReadTimeout(15000);c.setInstanceFollowRedirects(false);c.setRequestProperty("User-Agent","FlXtR-Android/0.4");
            for(Map.Entry<String,String> h:headers.entrySet())c.setRequestProperty(h.getKey(),h.getValue());
            try{
                int code=c.getResponseCode();if(code>=300&&code<400){String location=c.getHeaderField("Location");if(location==null)throw new IOException("Missing redirect");url=new URL(new URL(url),location).toString();continue;}
                if(code!=200&&code!=206)throw new IOException("Media request failed ("+code+")");
                if(c.getContentLengthLong()>limit)throw new IOException("Response is too large");
                try(InputStream in=c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){
                    byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))>0){request.check();if(out.size()+n>limit)throw new IOException("Response is too large");out.write(buffer,0,n);}return out.toByteArray();
                }
            }finally{request.connection=null;c.disconnect();}
        }throw new IOException("Too many redirects");
    }
    static String text(String url,Map<String,String> headers,int limit,StreamResolver.Request request) throws IOException {return new String(bytes(url,headers,limit,request),StandardCharsets.UTF_8);}
    static String text(String url,StreamResolver.Request request) throws IOException {return text(url,Collections.emptyMap(),2*1024*1024,request);}
    static String encode(String value){try{return URLEncoder.encode(value,"UTF-8").replace("+","%20");}catch(Exception impossible){throw new IllegalStateException(impossible);}}
}
