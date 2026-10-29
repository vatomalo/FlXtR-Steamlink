package org.flxtr.android;
import java.net.*;import java.io.*;import java.util.*;
public final class MediaResolverSmoke {
 public static void main(String[] args) throws Exception {
  StreamResolver.Request request=new StreamResolver.Request();
  CatalogPage shows=KissAnime.list(1,"naruto","","popular",request);if(shows.items.isEmpty())throw new AssertionError("No anime search results");System.out.println("PASS: live anime search ("+shows.items.size()+" shows)");
  CatalogPage episodes=KissAnime.episodes(7365,1,request);if(episodes.items.isEmpty())throw new AssertionError("No anime episodes");System.out.println("PASS: live anime episode catalogue");
  CatalogItem episode=episodes.items.get(0);StreamProtocol.Stream stream=KissAnime.resolve(episode,0,false,request);
  System.out.println("Anime source host: "+new URL(stream.url).getHost()+"; captions: "+stream.subtitles.size());

  String playlist=MediaHttp.text(stream.url,stream.headers,1024*1024,request);if(!playlist.startsWith("#EXTM3U"))throw new AssertionError("Not HLS");String base=stream.url;
  for(int hop=0;hop<5;hop++){String media=null;for(String line:playlist.split("\\n")){line=line.trim();if(!line.isEmpty()&&!line.startsWith("#")){media=new URL(new URL(base),line).toString();break;}}if(media==null)throw new AssertionError("Empty HLS");Map<String,String> headers=new HashMap<>(stream.headers);headers.put("Range","bytes=0-65535");byte[] fragment=prefix(media,headers);String text=new String(fragment,java.nio.charset.StandardCharsets.UTF_8);if(!text.startsWith("#EXTM3U")){if(fragment.length<1000)throw new AssertionError("Media fragment too small");System.out.println("PASS: live anime HLS and "+fragment.length+"-byte media fragment");break;}playlist=text;base=media;}
  CatalogPage archive=ArchiveMedia.list(1,1,"",new StreamResolver.Request());if(archive.items.isEmpty())throw new AssertionError("Archive empty");System.out.println("PASS: live Internet Archive catalogue");
 }
 static byte[] prefix(String url,Map<String,String> headers) throws Exception {HttpURLConnection c=(HttpURLConnection)new URL(url).openConnection();c.setConnectTimeout(10000);c.setReadTimeout(15000);for(Map.Entry<String,String> h:headers.entrySet())c.setRequestProperty(h.getKey(),h.getValue());try(InputStream in=c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] b=new byte[8192];int n;while(out.size()<65536&&(n=in.read(b,0,Math.min(b.length,65536-out.size())))>0)out.write(b,0,n);return out.toByteArray();}finally{c.disconnect();}}
}