#!/usr/bin/env python3
"""Use upstream's portable source list for both Android and Steam Link."""
import subprocess, sys
from pathlib import Path

root=Path(sys.argv[1]).resolve()
output=Path(sys.argv[2])
recipe=f'CORE_DIR := {root}/src\nHAVE_NTSC := 1\ninclude {root}/Makefile.common\nall:\n\t@echo $(SOURCES_C)\n'
sources=subprocess.check_output(['make','--no-print-directory','-f','-'],input=recipe,text=True).split()
if not sources or any(not Path(source).is_file() for source in sources):raise RuntimeError('Invalid FCEUmm source list')
output.write_text('add_library(flxtr_fceumm SHARED\n'+ '\n'.join('"'+source+'"' for source in sources)+')\n'
    +'target_include_directories(flxtr_fceumm PRIVATE '+ ' '.join('"'+str(root/'src'/p)+'"' for p in ['.','input','boards','ntsc','drivers/libretro','drivers/libretro/libretro-common/include']) +')\n'
    +'target_compile_definitions(flxtr_fceumm PRIVATE __LIBRETRO__ PATH_MAX=1024 FCEU_VERSION_NUMERIC=9900 FRONTEND_SUPPORTS_RGB565 HAVE_NTSC_FILTER PSS_STYLE=1)\n'
    +'target_compile_options(flxtr_fceumm PRIVATE -O2 -fno-strict-aliasing)\n'
    +'target_link_libraries(flxtr_fceumm PRIVATE m)\n')
