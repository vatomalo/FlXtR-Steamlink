package org.flxtr.android;
import org.jsoup.Jsoup;
import org.jsoup.nodes.*;
import org.json.*;
import java.util.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.regex.*;
import javax.crypto.*;
import javax.crypto.spec.*;

/** Public catalogue/episode adapters matching the native KissAnime backend. */
final class KissAnime {
    static final String BASE="https://kissanime.com.cv";
    static CatalogPage list(int page,String query,String genre,String order,StreamResolver.Request request) throws Exception {
        String url=query.isEmpty()?BASE+"/anime/?page="+page+"&order="+MediaHttp.encode(order)+(genre.isEmpty()?"":"&genre%5B%5D="+MediaHttp.encode(genre)):BASE+"/page/"+page+"/?s="+MediaHttp.encode(query);
        return cards(MediaHttp.text(url,request),page);
    }
    static CatalogPage cards(String html,int page) throws IOException {
        Document doc=Jsoup.parse(html,BASE);Element area=doc.selectFirst(".listupd");if(area==null)throw new IOException("Anime catalogue unavailable");
        ArrayList<CatalogItem> rows=new ArrayList<>();
        for(Element a:area.select("article a[rel][title]")){
            String href=a.absUrl("href");if(!href.startsWith(BASE+"/anime/"))continue;
            int id=integer(a.attr("rel"));if(id<=0)continue;Element image=a.selectFirst("img");String poster=image==null?"":image.absUrl("src");
            rows.add(new CatalogItem("kiss",id,0,0,a.attr("title"),"KissAnime · episodes","",CatalogItem.safePoster(poster)));
            if(rows.size()==40)break;
        }
        boolean more=doc.select(".hpage a").stream().anyMatch(a->a.text().toLowerCase(Locale.ROOT).contains("next")||a.hasClass("r"));
        return new CatalogPage(rows,page+(more?1:0));
    }
    static CatalogPage episodes(int id,int page,StreamResolver.Request request) throws Exception {return episodesHtml(MediaHttp.text(BASE+"/?post_type=anime&p="+id,request),id,page);}
    static CatalogPage episodesHtml(String html,int parent,int page){
        Document doc=Jsoup.parse(html,BASE);Element meta=doc.selectFirst("meta[property=og:description],meta[name=description]");String overview=meta==null?"":meta.attr("content");Element synopsis=doc.selectFirst(".synp,.entry-content p");if(synopsis!=null)overview=synopsis.text();
        TreeMap<Double,CatalogItem> all=new TreeMap<>();
        for(Element a:doc.select("a.ep-item[data-id][data-number]")){
            int id=integer(a.attr("data-id"));double number;try{number=Double.parseDouble(a.attr("data-number"));}catch(Exception invalid){continue;}
            if(id<=0||number<0||all.size()>=5000)continue;
            all.put(number,new CatalogItem("kiss-episode",id,parent,(int)number,"Episode "+a.attr("data-number"),"KissAnime · SUB / DUB",overview,""));
        }
        List<CatalogItem> sorted=new ArrayList<>(all.values());int from=page<=0?0:Math.min(sorted.size(),Math.max(0,page-1)*20),to=page<=0?sorted.size():Math.min(sorted.size(),from+20);
        return new CatalogPage(new ArrayList<>(sorted.subList(from,to)),Math.max(1,(sorted.size()+19)/20));
    }
    static StreamProtocol.Stream resolve(CatalogItem item,int attempt,boolean dub,StreamResolver.Request request) throws Exception {
        if(attempt>1)throw new IOException("Anime servers exhausted");
        String referer=BASE+"/?p="+item.id;Document doc=Jsoup.parse(MediaHttp.text(referer,request),BASE);
        Element frame=doc.selectFirst("#pembed iframe[src]");if(frame==null)throw new IOException("Episode player is not available yet");
        String url=frame.absUrl("src");boolean useDub=attempt==0?dub:!dub;
        if(url.startsWith("https://gogoanime.com.by/"))url=url.replaceAll("([?&]type=)[^&]*","$1"+(useDub?"dub":"sub"));
        for(int hop=0;hop<3;hop++){
            if(!embed(url))throw new IOException("Unsupported anime video host");
            Map<String,String> headers=Collections.singletonMap("Referer",referer);
            String html=MediaHttp.text(url,headers,2*1024*1024,request);Document player=Jsoup.parse(html,url);referer=url;
            String direct=direct(html);
            StreamProtocol.Stream stream=null;
            if(!direct.isEmpty())stream=new StreamProtocol.Stream(direct,useDub?"DUB":"SUB");
            else if(url.startsWith("https://megaplay.buzz/")){
                Element element=player.selectFirst("#megaplay-player[data-id]");if(element==null)throw new IOException("Anime server is not ready");
                String id=element.attr("data-id");if(!id.matches("[0-9]+"))throw new IOException("Invalid anime source identity");
                Map<String,String> ajax=new HashMap<>();ajax.put("Referer",referer);ajax.put("Origin","https://megaplay.buzz");ajax.put("X-Requested-With","XMLHttpRequest");ajax.put("Accept","application/json");
                for(String route:new String[]{"getSourcesNew","getSources"}){
                    try{
                        JSONObject root=new JSONObject(MediaHttp.text("https://megaplay.buzz/stream/"+route+"?id="+id+(route.equals("getSourcesNew")?"&platform=OTHER":""),ajax,65536,request));
                        stream=mega(root,System.currentTimeMillis()/1000);if(stream!=null)break;
                    }catch(Exception failure){request.check();}
                }
            }
            if(stream!=null){stream.headers.clear();stream.headers.put("User-Agent",StreamProtocol.UA);stream.headers.put("Referer",url.startsWith("https://megaplay.buzz/")?"https://megaplay.buzz/":referer);stream.headers.put("Origin",url.startsWith("https://megaplay.buzz/")?"https://megaplay.buzz":BASE);return stream;}
            frame=player.selectFirst("iframe[src]");if(frame==null)break;url=frame.absUrl("src");
        }throw new IOException("No anime source is currently available");
    }
    static boolean embed(String url){return url.startsWith("https://gogoanime.com.by/")||url.startsWith("https://megaplay.buzz/");}
    static String direct(String html){
        Document doc=Jsoup.parse(html);for(Element e:doc.select("source[src],video[src]")){String value=e.attr("src");if(media(value))return value;}
        Matcher m=Pattern.compile("(?:[\"']?(?:file|src)[\"']?\\s*:)\\s*[\"']([^\"']+)[\"']").matcher(html);
        while(m.find()){String value=org.jsoup.parser.Parser.unescapeEntities(m.group(1).replace("\\/","/"),false);if(media(value))return value;}return "";
    }
    private static boolean media(String value){return CatalogItem.validStream(value)&&(value.contains(".m3u8")||value.contains(".mp4"));}
    static StreamProtocol.Stream mega(JSONObject root,long now) throws Exception {
        Object sources=root.opt("sources");String enc=root.optString("enc","");
        if(!enc.isEmpty()){
            byte[] key=Arrays.copyOf("i?LMTAx0Q6,:}50U".getBytes(StandardCharsets.US_ASCII),32);
            Cipher cipher=Cipher.getInstance("AES/CBC/PKCS5Padding");cipher.init(Cipher.DECRYPT_MODE,new SecretKeySpec(key,"AES"),new IvParameterSpec("W0;27ToaUpl_P%'c".getBytes(StandardCharsets.US_ASCII)));
            String plain=new String(cipher.doFinal(decode(enc)),StandardCharsets.UTF_8);sources=new JSONTokener(plain).nextValue();
        }
        String file=sources instanceof String?(String)sources:sources instanceof JSONObject?((JSONObject)sources).optString("file",((JSONObject)sources).optString("url","")):"";
        if(!media(file))return null;StreamProtocol.Stream stream=new StreamProtocol.Stream(token(file,now),"KissAnime");
        MediaCaptions.read(root,stream);if(sources instanceof JSONObject)MediaCaptions.read((JSONObject)sources,stream);return stream;
    }
    static byte[] decode(String text){return Base64.getDecoder().decode(text.replace('-','+').replace('_','/'));}
    static String token(String url,long now) throws Exception {
        if(url.contains("?token=")||url.contains("&token="))return url;
        Matcher match=Pattern.compile("/([a-fA-F0-9]{32}/[a-fA-F0-9]{32})/").matcher(url);if(!match.find())return url;
        String payload=(now+90)+"|"+match.group(1).toLowerCase(Locale.ROOT);Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec("MpCdnT0k3n!9f2K#xQ7vL5mR8wN1pY4s".getBytes(StandardCharsets.UTF_8),"HmacSHA256"));
        return url+(url.contains("?")?"&":"?")+"token="+Base64.getUrlEncoder().withoutPadding().encodeToString(payload.getBytes(StandardCharsets.UTF_8))+"."+Base64.getUrlEncoder().withoutPadding().encodeToString(mac.doFinal(payload.getBytes(StandardCharsets.UTF_8)));
    }
    private static int integer(String value){try{return Integer.parseInt(value);}catch(Exception invalid){return 0;}}
}
