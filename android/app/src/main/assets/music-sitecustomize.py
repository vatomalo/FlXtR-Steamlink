# Python's platform.architecture() executes Android's system `file` utility.
# On Samsung Android 8, inherited Python OpenSSL paths break that executable
# (and its crash dumper). The interpreter's pointer size is authoritative here.
import platform
import struct
import sys
import os

if hasattr(sys, 'getandroidapilevel'):
    def _android_architecture(executable=sys.executable, bits='', linkage=''):
        return (str(struct.calcsize('P') * 8) + 'bit', 'ELF')
    platform.architecture = _android_architecture
    # platform.uname().processor otherwise invokes the system `uname -p` too.
    platform._Processor.get = classmethod(lambda cls: os.uname().machine)
