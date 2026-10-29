package org.flxtr.android;
/** Keep signed query values unchanged while isolating media ranges in edge caches. */
final class MediaRange {
 static String url(String address,long position,long length){if(position<0)throw new IllegalArgumentException("Negative media offset");int fragment=address.indexOf('#');if(fragment>=0)address=address.substring(0,fragment);int mark=address.indexOf('?');String base=mark<0?address:address.substring(0,mark);StringBuilder query=new StringBuilder();if(mark>=0)for(String part:address.substring(mark+1).split("&"))if(!part.isEmpty()&&!part.startsWith("range=")&&!part.startsWith("rn=")){if(query.length()>0)query.append('&');query.append(part);}if(query.length()>0)query.append('&');return base+"?"+query+"range="+position+"-"+(position+length-1);}
}
