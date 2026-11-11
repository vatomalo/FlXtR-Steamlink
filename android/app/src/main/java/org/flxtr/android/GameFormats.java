package org.flxtr.android;

import java.io.IOException;
import java.util.*;
import java.util.regex.*;

/** Format detection and disc references, independent of Android storage APIs. */
public final class GameFormats {
    private GameFormats() {}
    public static String core(String name) {
        String n=name.toLowerCase(Locale.ROOT);
        if(n.endsWith(".cso")||n.endsWith(".elf")||n.endsWith(".prx"))return "ppsspp";
if(n.endsWith(".dol")||n.endsWith(".gcm")||n.endsWith(".rvz")||n.endsWith(".wbfs"))return "dolphin";
        if(n.endsWith(".scummvm"))return "scummvm";
        if(n.endsWith(".md")||n.endsWith(".gen")||n.endsWith(".smd")||n.endsWith(".sms")||n.endsWith(".gg"))return "genesis_plus_gx";
        if(n.endsWith(".pce"))return "mednafen_pce_fast";
        if(n.endsWith(".gb")||n.endsWith(".gbc"))return "gambatte";
        if(n.endsWith(".nes")||n.endsWith(".unf")||n.endsWith(".unif"))return "fceumm";
        if(n.endsWith(".sfc")||n.endsWith(".smc"))return "snes9x";
        if(n.endsWith(".gba"))return "mgba";
        if(n.endsWith(".cue")||n.endsWith(".chd")||n.endsWith(".pbp")||n.endsWith(".iso")||n.endsWith(".m3u"))return "pcsx_rearmed";
        return "";
    }
    public static String label(String core) {
        switch(core){case "genesis_plus_gx":return "Sega";case "mednafen_pce_fast":return "PC Engine";case "ppsspp":return "PSP";case "dolphin":return "GameCube / Wii";case "scummvm":return "ScummVM";case "gambatte":return "GB / GBC";case "fceumm":return "NES";case "snes9x":return "SNES";case "mgba":return "GBA";case "pcsx_rearmed":return "PlayStation";default:return "Game";}
    }
    public static String[] cores(String name){String lower=name.toLowerCase(Locale.ROOT);if(lower.endsWith(".iso"))return new String[]{"pcsx_rearmed","ppsspp","dolphin"};if(lower.endsWith(".pbp"))return new String[]{"pcsx_rearmed","ppsspp"};if(lower.endsWith(".elf"))return new String[]{"ppsspp","dolphin"};String detected=core(name);return detected.isEmpty()?new String[0]:new String[]{detected};}
    public static boolean compatible(String name,String core){return Arrays.asList(cores(name)).contains(core);}
    public static boolean bios(String name) {return name.toLowerCase(Locale.ROOT).matches("(scph(1001|5500|5501|5502|7001)|psxonpsp660)\\.bin");}
    public static String relative(String path)throws IOException {
        String p=path.replace('\\','/');if(p.startsWith("/")||p.contains(":"))throw new IOException("Disc paths must stay inside the selected game folder.");
        ArrayDeque<String> parts=new ArrayDeque<>();
        for(String part:p.split("/")){if(part.isEmpty()||part.equals("."))continue;if(part.equals("..")){if(parts.isEmpty())throw new IOException("Disc path leaves the selected game folder.");parts.removeLast();}else{if(part.indexOf('\0')>=0)throw new IOException("Invalid filename");parts.addLast(part);}}
        if(parts.isEmpty())throw new IOException("Empty filename");return String.join("/",parts);
    }
    public static List<String> references(String path,String text)throws IOException {
        ArrayList<String> result=new ArrayList<>();String lower=path.toLowerCase(Locale.ROOT);int slash=path.lastIndexOf('/');String parent=slash<0?"":path.substring(0,slash+1);
        if(lower.endsWith(".cue")){
            Matcher m=Pattern.compile("(?im)^\\s*FILE\\s+(?:\"([^\"]+)\"|(\\S+))\\s+").matcher(text);
            while(m.find()){String value=m.group(1)!=null?m.group(1):m.group(2);validateReference(value);result.add(relative(parent+value));}
            if(result.isEmpty())throw new IOException("The CUE sheet contains no tracks.");
        }else if(lower.endsWith(".m3u")){
            for(String line:text.replace("\ufeff","").split("\\r?\\n")){String value=line.trim();if(value.isEmpty()||value.startsWith("#"))continue;validateReference(value);String ref=relative(parent+value);if(!ref.toLowerCase(Locale.ROOT).matches(".*\\.(cue|chd|pbp|iso)$"))throw new IOException("PlayStation playlists must reference CUE, CHD, PBP or ISO discs.");result.add(ref);}
            if(result.isEmpty())throw new IOException("The disc playlist is empty.");
        }
        if(result.size()>128)throw new IOException("Too many disc tracks.");return result;
    }
    private static void validateReference(String value)throws IOException {if(value.replace('\\','/').startsWith("/")||value.contains(":"))throw new IOException("Disc references must be relative filenames.");}
}
