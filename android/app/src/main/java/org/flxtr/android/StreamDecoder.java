package org.flxtr.android;

/** Same pinned decoder and host bindings as the Steam Link resolver. */
final class StreamDecoder {
    static final Object LOCK=new Object();
    static { System.loadLibrary("flxtr_resolver"); }
    static native String nativeBegin() throws java.io.IOException;
    static native String nativeDecode(String payload,String key) throws java.io.IOException;
    static native void nativeEnd();
    private StreamDecoder() {}
}
