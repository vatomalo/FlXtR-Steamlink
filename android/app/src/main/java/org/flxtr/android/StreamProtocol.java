package org.flxtr.android;

import org.json.JSONArray;
import org.json.JSONObject;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.*;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** Wire format shared with src/resolver.c; no browser or executable workers. */
final class StreamProtocol {
    static final String BASE="https://plsdontscrapemelove.flixer.gd";
    static final String UA="FlXtR Steamlink native protocol research";
    static final String CANVAS="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==";
    static String path(CatalogItem item) throws IOException {
        if(item.id<=0)throw new IOException("Invalid title identity");
        if(item.kind.equals("movie"))return "/api/tmdb/movie/"+item.id+"/images";
        if(item.kind.equals("episode")&&item.season>=0&&item.episode>0)return "/api/tmdb/tv/"+item.id+"/season/"+item.season+"/episode/"+item.episode+"/images";
        throw new IOException("Select a movie or episode");
    }
    static String signature(String key,String timestamp,String nonce,String path) throws Exception {
        Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(key.getBytes(StandardCharsets.UTF_8),"HmacSHA256"));
        return Base64.getEncoder().encodeToString(mac.doFinal((key+":"+timestamp+":"+nonce+":"+path).getBytes(StandardCharsets.UTF_8)));
    }
    static String fingerprint(){
        String input="1920x1080:24:"+UA.substring(0,Math.min(50,UA.length()))+":Linux armv7l:en-US:0:"+CANVAS.substring(22,50);
        int hash=0;for(byte b:input.getBytes(StandardCharsets.UTF_8))hash=hash*31+(b&255);
        long value=Integer.toUnsignedLong(hash);if(hash<0)value=(-value)&0xffffffffL;
        return Long.toString(value,36);
    }
    static boolean validServer(String name){return name!=null&&name.matches("[A-Za-z0-9_-]{1,31}");}
    static List<String> servers(JSONObject root){
        TreeSet<String> names=new TreeSet<>();JSONArray list=root.optJSONArray("sources");
        if(list!=null)for(int i=0;i<list.length();i++){JSONObject row=list.optJSONObject(i);if(row!=null&&validServer(row.optString("server")))names.add(row.optString("server"));}
        JSONObject map=root.optJSONObject("servers");if(map!=null){Iterator<String> keys=map.keys();while(keys.hasNext()){String name=keys.next();if(validServer(name))names.add(name);}}
        return new ArrayList<>(names).subList(0,Math.min(8,names.size()));
    }
    static final class Stream {
        final String url,server;
        String mime;
        final Map<String,String> headers=new HashMap<>();
        Stream(String url,String server){this.url=url;this.server=server;headers.put("User-Agent",UA);headers.put("Origin","https://flixer.gd");headers.put("Referer","https://flixer.gd/");}
    }
    static Stream stream(JSONObject root,String server) throws IOException {
        JSONObject selected=null;JSONArray list=root.optJSONArray("sources");
        if(list!=null)for(int i=0;i<list.length();i++){JSONObject row=list.optJSONObject(i);if(row!=null&&server.equals(row.optString("server"))){selected=row;break;}}
        if(selected==null)selected=root.optJSONObject("sources");
        if(selected==null)throw new IOException("Server returned no stream");
        String url=selected.optString("url","");if(url.isEmpty())url=selected.optString("file","");
        if(!CatalogItem.validStream(url))throw new IOException("Server returned no compatible HTTPS stream");
        Stream result=new Stream(url,server);
        JSONObject headers=selected.optJSONObject("headers");if(headers==null)headers=root.optJSONObject("headers");
        if(headers!=null){Iterator<String> keys=headers.keys();while(keys.hasNext()){
            String key=keys.next(),value=headers.optString(key,"");String name=key.equalsIgnoreCase("referer")?"Referer":key.equalsIgnoreCase("origin")?"Origin":key.equalsIgnoreCase("user-agent")?"User-Agent":null;
            if(name!=null&&!value.isEmpty()&&value.length()<512&&value.indexOf('\r')<0&&value.indexOf('\n')<0)result.headers.put(name,value);
        }}
        return result;
    }
}
