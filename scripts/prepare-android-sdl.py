#!/usr/bin/env python3
"""Apply the Android 14+ receiver flag to the pinned SDL2 Java glue."""
import sys
from pathlib import Path
source = Path(sys.argv[1]) / "android-project/app/src/main/java/org/libsdl/app/HIDDeviceManager.java"
text = source.read_text()
old = "        mContext.registerReceiver(mUsbBroadcast, filter);"
new = """        // Backport receiver visibility consistently across all supported APIs.
        androidx.core.content.ContextCompat.registerReceiver(mContext, mUsbBroadcast,
            filter, androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED);"""
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("Pinned SDL USB receiver changed; review Android compatibility patch")
    source.write_text(text.replace(old, new))
