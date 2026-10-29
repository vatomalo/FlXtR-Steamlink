package org.flxtr.android;
import org.json.*;
import java.util.*;
import java.io.*;
final class ArchiveMedia {
    static final String[] TITLES={"Classic films","Animation","Documentaries","Sci-fi / horror","Comedy","Music / concerts","Educational","All videos"};
    static final String[] FILTERS={"(subject:classic OR collection:feature_films)","(subject:animation OR collection:classic_cartoons)","(subject:documentary OR subject:documentaries)","(subject:science_fiction OR subject:horror)","(subject:comedy OR subject:humor)","(subject:concert OR subject:music)","(subject:education OR subject:educational)",""};
    static CatalogPage folders(){ArrayList<CatalogItem> rows=new ArrayList<>();for(int i=0;i<TITLES.length;i++)rows.add(new CatalogItem("archive-folder",i+1,0,0,TITLES[i],"Internet Archive","",""));return new CatalogPage(rows,1);}
    static CatalogPage list(int page,int category,String query,StreamResolver.Request request) throws Exception {
        String filter=category>0&&category<=FILTERS.length?FILTERS[category-1]:"";
        String expression="mediatype:movies AND (format:\"h.264\" OR format:\"MPEG4\") AND -access-restricted-item:true"+(filter.isEmpty()?"":" AND "+filter)+(query.isEmpty()?"":" AND ("+query.replaceAll("[^\\p{L}\\p{N} -]","")+")");
        String url="https://archive.org/advancedsearch.php?q="+MediaHttp.encode(expression)+"&fl%5B%5D=identifier&fl%5B%5D=title&rows=20&page="+page+"&sort%5B%5D=downloads%20desc&output=json";
        return cards(new JSONObject(MediaHttp.text(url,request)));
    }
    static boolean identifier(String text){return text.matches("[A-Za-z0-9_.-]{1,128}")&&!text.contains("..");}
    static CatalogPage cards(JSONObject root) throws Exception {
        JSONObject response=root.getJSONObject("response");JSONArray docs=response.getJSONArray("docs");ArrayList<CatalogItem> rows=new ArrayList<>();
        for(int i=0;i<docs.length();i++){JSONObject doc=docs.optJSONObject(i);if(doc==null)continue;String id=doc.optString("identifier");if(!identifier(id))continue;rows.add(new CatalogItem("archive-item",0,0,0,doc.optString("title",id),"Internet Archive · videos","","https://archive.org/services/img/"+id,id));}
        return new CatalogPage(rows,Math.max(1,Math.min(600,(response.optInt("numFound")+19)/20)));
    }
    static CatalogPage files(String id,int page,StreamResolver.Request request) throws Exception {if(!identifier(id))throw new IOException("Invalid archive item");return filesJson(new JSONObject(MediaHttp.text("https://archive.org/metadata/"+id,request)),id,page);}
    static CatalogPage filesJson(JSONObject root,String id,int page) throws Exception {
        JSONArray files=root.getJSONArray("files");ArrayList<CatalogItem> rows=new ArrayList<>();String overview=root.optJSONObject("metadata")==null?"":root.getJSONObject("metadata").optString("description","");
        for(int i=0;i<files.length();i++){JSONObject file=files.optJSONObject(i);if(file==null)continue;String name=file.optString("name"),format=file.optString("format");
            if(file.optBoolean("private",false)||name.length()>1024||!name.toLowerCase(Locale.ROOT).matches(".*\\.(mp4|mov|m4v)$")||!(format.equalsIgnoreCase("h.264")||format.equalsIgnoreCase("MPEG4")))continue;
            rows.add(new CatalogItem("archive-file",0,0,i,name,"Archive · "+format,org.jsoup.Jsoup.parse(overview).text(),"https://archive.org/services/img/"+id,"https://archive.org/download/"+id+"/"+MediaHttp.encode(name)));
        }
        int from=Math.min(rows.size(),Math.max(0,page-1)*20);return new CatalogPage(new ArrayList<>(rows.subList(from,Math.min(rows.size(),from+20))),Math.max(1,(rows.size()+19)/20));
    }
}
