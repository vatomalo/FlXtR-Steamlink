package org.flxtr.android;
import org.junit.Test;
import static org.junit.Assert.*;
public class GameKeyboardTest {
    @Test public void mapsPunctuationAndModifiers(){assertEquals(47,GameKeyboard.key(76));assertEquals(306,GameKeyboard.key(113));assertEquals(0xc3,GameKeyboard.modifiers(0x1001));assertEquals(0,GameKeyboard.modifiers(0));}
    @Test public void mapsLettersNumbersAndNavigation(){assertEquals(97,GameKeyboard.key(29));assertEquals(122,GameKeyboard.key(54));assertEquals(48,GameKeyboard.key(7));assertEquals(273,GameKeyboard.key(19));assertEquals(276,GameKeyboard.key(21));assertEquals(13,GameKeyboard.key(66));assertEquals(282,GameKeyboard.key(131));assertEquals(293,GameKeyboard.key(142));assertEquals(0,GameKeyboard.key(96));}
}
