package com.limelight.flxtr;

import android.content.Context;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class GamezBridge {
    private static final String PREFS="flxtr-gamez",TOKEN="token";
    private static final int MAX_RESPONSE=512*1024;
    private GamezBridge(){}
    public static final class Game {public final String id,title;Game(String id,String title){this.id=id;this.title=title;}}
    public static boolean validToken(String token){return token!=null&&token.matches("[A-Za-z0-9]{32,256}");}
    public static boolean configured(Context context){return validToken(context.getSharedPreferences(PREFS,0).getString(TOKEN,""));}
    public static void configure(Context context,String token){token=token==null?"":token.trim();if(!validToken(token))throw new IllegalArgumentException("The Gamez token must be 32–256 letters or digits.");context.getSharedPreferences(PREFS,0).edit().putString(TOKEN,token).apply();}
    public static List<Game> parse(String text)throws Exception {JSONObject root=new JSONObject(text);JSONArray games=root.getJSONArray("games");if(games.length()>5000)throw new IOException("PC game catalog is too large");ArrayList<Game> result=new ArrayList<>();HashSet<String> ids=new HashSet<>();for(int i=0;i<games.length();i++){JSONObject item=games.getJSONObject(i);String id=item.getString("id"),title=item.getString("title").trim();if(!id.matches("[A-Za-z0-9]{1,32}")||title.isEmpty()||title.getBytes(StandardCharsets.UTF_8).length>79||!ids.add(id))throw new IOException("Invalid PC game catalog");result.add(new Game(id,title));}return result;}
    public static List<Game> games(Context context,String host)throws Exception {return parse(request(context,host,"GET","/v1/games",null));}
    public static void launch(Context context,String host,String id)throws Exception {if(id==null||!id.matches("[A-Za-z0-9]{1,32}"))throw new IOException("Invalid PC game");request(context,host,"POST","/v1/launch",id);}
    private static String request(Context context,String host,String method,String path,String body)throws Exception {if(host==null||!host.matches("[A-Za-z0-9.:-]+"))throw new IOException("Invalid PC address");String token=context.getSharedPreferences(PREFS,0).getString(TOKEN,"");if(!validToken(token))throw new IOException("Configure the Gamez token first");HttpURLConnection connection=(HttpURLConnection)new URL("http://"+host+":47777"+path).openConnection();connection.setConnectTimeout(3000);connection.setReadTimeout(6000);connection.setInstanceFollowRedirects(false);connection.setRequestMethod(method);connection.setRequestProperty("Authorization","Bearer "+token);if(body!=null){connection.setDoOutput(true);byte[] bytes=body.getBytes(StandardCharsets.US_ASCII);connection.setFixedLengthStreamingMode(bytes.length);try(OutputStream output=connection.getOutputStream()){output.write(bytes);}}try{int status=connection.getResponseCode();if(status!=200)throw new IOException("PC Gamez bridge rejected the request ("+status+")");try(InputStream input=connection.getInputStream();ByteArrayOutputStream output=new ByteArrayOutputStream()){byte[] buffer=new byte[8192];int read,total=0;while((read=input.read(buffer))!=-1){total+=read;if(total>MAX_RESPONSE)throw new IOException("PC game catalog is too large");output.write(buffer,0,read);}return output.toString("UTF-8");}}finally{connection.disconnect();}}
}
