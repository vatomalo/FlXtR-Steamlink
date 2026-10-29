package org.flxtr.android;
import org.json.*;
import java.util.*;
final class MediaCaptions {
    static final class Track {final String url,language,label,mime;Track(String url,String language,String label,String mime){this.url=url;this.language=language;this.label=label;this.mime=mime;}}
    static void read(JSONObject root,StreamProtocol.Stream stream){
        for(String key:new String[]{"tracks","subtitles","captions","subtitleTracks","subtitle_tracks"}){
            JSONArray array=root.optJSONArray(key);if(array==null)continue;
            for(int i=0;i<Math.min(array.length(),100);i++){
                JSONObject track=array.optJSONObject(i);if(track==null)continue;String url="";
                for(String field:new String[]{"url","file","src","link"}){url=track.optString(field,"");if(!url.isEmpty())break;}
                if(!CatalogItem.validStream(url))continue;String label="";
                for(String field:new String[]{"lang","language","label","languageCode","srclang"}){label=track.optString(field,"");if(!label.isEmpty())break;}
                String language=label.toLowerCase(Locale.ROOT);language=language.startsWith("en")?"en":language.startsWith("no")||language.startsWith("nb")?"no":language.startsWith("es")||language.startsWith("spa")?"es":language.length()==2?language:"und";
                String lower=url.toLowerCase(Locale.ROOT),mime=lower.contains(".srt")?"application/x-subrip":lower.contains(".ass")||lower.contains(".ssa")?"text/x-ssa":"text/vtt";
                boolean duplicate=false;for(Track old:stream.subtitles)if(old.url.equals(url))duplicate=true;
                if(!duplicate)stream.subtitles.add(new Track(url,language,label.isEmpty()?language:label,mime));
            }
        }
    }
}
