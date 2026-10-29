package org.flxtr.android;
import android.net.Uri;import androidx.media3.common.C;import androidx.media3.datasource.*;import java.io.*;import java.util.*;import java.util.regex.*;
/** Google media endpoints require finite byte ranges, including the first read.
 * Expose one continuous source while fetching bounded chunks; preserve seeks. */
@androidx.annotation.OptIn(markerClass=androidx.media3.common.util.UnstableApi.class)
final class RangeDataSource implements DataSource {
 static final long CHUNK=1024*1024;private final DataSource upstream;private DataSpec original;private long position,remaining,chunkRemaining,total=-1;private boolean ranged,opened;
 static final class Factory implements DataSource.Factory {private final DataSource.Factory upstream;Factory(DataSource.Factory upstream){this.upstream=upstream;}public DataSource createDataSource(){return new RangeDataSource(upstream.createDataSource());}}
 RangeDataSource(DataSource upstream){this.upstream=upstream;}
 public void addTransferListener(TransferListener listener){upstream.addTransferListener(listener);}
 public long open(DataSpec spec)throws IOException {original=spec;position=spec.position;remaining=spec.length;String host=spec.uri.getHost();ranged="/videoplayback".equals(spec.uri.getPath())&&host!=null&&(host.equals("googlevideo.com")||host.endsWith(".googlevideo.com"));if(!ranged){opened=true;return upstream.open(spec);}total=-1;try{String length=spec.uri.getQueryParameter("clen");if(length!=null)total=Long.parseLong(length);}catch(Exception ignored){}openChunk();return remaining!=C.LENGTH_UNSET?remaining:total>=0?Math.max(0,total-position):C.LENGTH_UNSET;}
 private void openChunk()throws IOException {
  long length=CHUNK;if(remaining!=C.LENGTH_UNSET)length=Math.min(length,remaining);if(total>=0)length=Math.min(length,total-position);if(length<=0){chunkRemaining=0;return;}
  // Query ranges are finite resources; do not apply a second HTTP byte offset.
  DataSpec spec=original.buildUpon().setUri(Uri.parse(MediaRange.url(original.uri.toString(),position,length))).setPosition(0).setLength(C.LENGTH_UNSET).build();opened=true;upstream.open(spec);chunkRemaining=length;
  for(Map.Entry<String,List<String>> header:upstream.getResponseHeaders().entrySet())if(header.getKey()!=null&&header.getKey().equalsIgnoreCase("Content-Range")&&!header.getValue().isEmpty()){Matcher match=Pattern.compile("bytes ([0-9]+)-[0-9]+/([0-9]+)").matcher(header.getValue().get(0));if(match.matches()){if(Long.parseLong(match.group(1))!=position)throw new IOException("Media range mismatch: expected "+position+", got "+match.group(1));total=Long.parseLong(match.group(2));}}
 }
 public int read(byte[] buffer,int offset,int length)throws IOException {
  if(!ranged)return upstream.read(buffer,offset,length);if(length==0)return 0;if(remaining==0||(total>=0&&position>=total))return C.RESULT_END_OF_INPUT;
  if(chunkRemaining==0){upstream.close();opened=false;openChunk();}
  int wanted=(int)Math.min(length,chunkRemaining);if(total>=0)wanted=(int)Math.min(wanted,total-position);if(wanted<=0)return C.RESULT_END_OF_INPUT;
  int n=upstream.read(buffer,offset,wanted);if(n==C.RESULT_END_OF_INPUT){if(total>=0&&position<total)throw new EOFException("Media range ended early");return n;}position+=n;chunkRemaining-=n;if(remaining!=C.LENGTH_UNSET)remaining-=n;return n;
 }
 public Uri getUri(){return upstream.getUri();}public Map<String,List<String>> getResponseHeaders(){return upstream.getResponseHeaders();}public void close()throws IOException {if(opened)upstream.close();opened=false;original=null;}
}
