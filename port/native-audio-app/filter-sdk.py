#!/usr/bin/env python3
"""Preserve pure SDK modules; replace native service implementations explicitly."""
from pathlib import Path
import subprocess,shutil
p=Path(__file__).resolve().parent
ar='@TOOLBIN@ar'
src=p.parent/'native-sdk-probe/libnative-sdk-probe.a'
names=subprocess.check_output([ar,'t',str(src)],text=True).splitlines()
keep_os={'os_alloc.c','os_printf.c','os_vramExclusive.c'}
omit=[n for n in names if ('__os__' in n and not any('__'+x+'.o' in n for x in keep_os)) or '__fs__' in n or '__card__' in n or any(x in n for x in ['__spi__src__pm.c','__spi__src__mic.c','__snd__src__snd_bank.c','__snd__src__sndarc_loader.c','__mi__src__mi_dma_gxcommand.c','__mi__src__mi_memory.c','__mi__src__mi_dma.c','__spi__src__tp.c','__rtc__src__external.c','__rtc__src__internal.c'])]
shutil.copyfile(src,p/'libsdk-filtered.a')
subprocess.run([ar,'d',str(p/'libsdk-filtered.a'),*omit],check=True)
(p/'sdk-omitted.txt').write_text('\n'.join(omit)+'\n')
