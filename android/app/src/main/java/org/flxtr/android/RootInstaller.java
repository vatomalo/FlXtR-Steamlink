package org.flxtr.android;
import java.io.File;
import java.util.concurrent.TimeUnit;
/** Installs an already-verified FlXtR APK, then returns to its home screen. */
final class RootInstaller {
    private RootInstaller(){}
    private static String quote(String value){return "'"+value.replace("'","'\\''")+"'";}
    static boolean install(File apk)throws Exception {
        String script="set -e\n[ \"$(id -u)\" = 0 ] || exit 1\n"+
            "stage=$(mktemp -d /data/local/tmp/flxtr-update.XXXXXX) || exit 1\n"+
            "cp "+quote(apk.getAbsolutePath())+" \"$stage/update.apk\"\n"+
            "chmod 755 \"$stage\"\nchmod 644 \"$stage/update.apk\"\n"+
            "cat > \"$stage/install.sh\" <<'FLXTR_INSTALL'\n"+
            "#!/system/bin/sh\nset -e\nstage=$(dirname \"$0\")\n"+
            "pm install -r --user 0 \"$stage/update.apk\"\n"+
            "rm -f \"$stage/update.apk\" \"$stage/install.sh\" \"$stage/install.log\" || true\nrmdir \"$stage\" 2>/dev/null || true\n"+
            "am start --user 0 -n org.flxtr.android/.MainActivity\nFLXTR_INSTALL\n"+
            "chmod 700 \"$stage/install.sh\"\n"+
            "setsid /system/bin/sh \"$stage/install.sh\" >\"$stage/install.log\" 2>&1 </dev/null &\n";
        Process process=new ProcessBuilder("su","-c",script).redirectErrorStream(true).start();
        if(!process.waitFor(120,TimeUnit.SECONDS)){process.destroy();return false;}
        return process.exitValue()==0;
    }
}
