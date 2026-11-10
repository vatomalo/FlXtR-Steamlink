package org.flxtr.android;

import java.io.IOException;
import java.net.URI;
import org.json.JSONArray;
import org.json.JSONObject;

/** A verified FlXtR announcement, independent of its local or tailnet route. */
final class FlxtrDevice {
    final String id,name,transport,platform;
    final URI endpoint;
    final boolean receiver,romReceiver;
    FlxtrDevice(String id,String name,String transport,String platform,URI endpoint,boolean receiver,boolean romReceiver){this.id=id;this.name=name;this.transport=transport;this.platform=platform;this.endpoint=endpoint;this.receiver=receiver;this.romReceiver=romReceiver;}
    static boolean tailnet(String host){
        try{String[] parts=host.split("\\.");return parts.length==4&&Integer.parseInt(parts[0])==100&&Integer.parseInt(parts[1])>=64&&Integer.parseInt(parts[1])<=127&&octet(parts[2])&&octet(parts[3]);}catch(Exception invalid){return false;}
    }
    private static boolean octet(String value){int n=Integer.parseInt(value);return n>=0&&n<=255;}
    static boolean lan(String host){
        try{String[] p=host.split("\\.");if(p.length!=4||!octet(p[0])||!octet(p[1])||!octet(p[2])||!octet(p[3]))return false;int a=Integer.parseInt(p[0]),b=Integer.parseInt(p[1]);return a==10||a==192&&b==168||a==172&&b>=16&&b<=31||a==169&&b==254;}catch(Exception invalid){return false;}
    }
    static FlxtrDevice announcement(JSONObject reply,String host,boolean remote)throws Exception{
        if(reply.optInt("protocol")!=1||!reply.optString("app","FlXtR").equals("FlXtR")||!(remote?tailnet(host):lan(host)))throw new IOException("Not a FlXtR device");
        JSONArray roles=reply.optJSONArray("roles");boolean receiver=roles==null&&reply.has("endpoint");
        if(roles!=null)for(int i=0;i<roles.length();i++)if(roles.optString(i).equals("receiver"))receiver=true;
        boolean romReceiver=false;if(roles!=null)for(int i=0;i<roles.length();i++)if(roles.optString(i).equals("rom-receiver"))romReceiver=true;
        URI endpoint=null;
        if(receiver||romReceiver){endpoint=TvHandoff.endpoint(reply.getString("endpoint"));if(!host.equals(endpoint.getHost())||remote&&endpoint.getPort()!=43927)throw new IOException("Device redirected to another address");}
        String id=reply.optString("id","");if(!id.matches("[a-zA-Z0-9._:-]{1,128}"))id=host;
        String name=reply.optString("name","FlXtR").replaceAll("[\\p{Cntrl}]","").trim();if(name.isEmpty())name="FlXtR";if(name.length()>80)name=name.substring(0,80);
        return new FlxtrDevice(id,name,remote?"Tailscale":"Local",reply.optString("platform","device"),endpoint,receiver,romReceiver);
    }
    static java.util.List<String> arpNeighbors(String contents){
        java.util.ArrayList<String> found=new java.util.ArrayList<>();
        for(String line:contents.split("\n")){
            String[] p=line.trim().split("\\s+");if(p.length<6||!lan(p[0]))continue;
            try{if((Integer.decode(p[2])&2)!=0&&!p[3].equals("00:00:00:00:00:00")&&!found.contains(p[0]))found.add(p[0]);}catch(NumberFormatException ignored){}
            if(found.size()==64)break;
        }
        return found;
    }
}
