"""Texture dirty tracking: in the objects that write the DS VRAM (s_HW_LCDC_VRAM), redirect memory-writing calls to the
PSPNativeVramW_* shims in vram_dirty.c (psp-objcopy --redefine-sym, no recompile). Run after sdk-g3stack/rebuild.py."""
from pathlib import Path
import subprocess, tempfile, shutil
T = Path('@WORK@'); app = T/'native-audio-app'
AR, OBJCOPY, NM = '@TOOLBIN@ar', '@TOOLBIN@objcopy', '@TOOLBIN@nm'
write_syms = {
    app/'libsdk-filtered.a': (['libntr__libraries__gx__src__gx_load3d.c.o', 'libntr__libraries__gx__src__gx_load2d.c.o'],
                              ['MIi_CpuCopy32', 'MIi_CpuCopy16', 'MI_DmaCopy32', 'MI_DmaCopy16', 'MI_DmaCopy32Async', 'memset', 'memcpy']),
    app/'overlays/libplatinum-overlays.a': (['system.o', 'overlay005__encounter_effect.o', 'overlay005__motion_blur.o', 'overlay100__ov100_021D4E04.o'],
                                            ['memset', 'memcpy', 'MIi_CpuCopy32', 'MIi_CpuCopy16', 'MI_DmaCopy32', 'MI_DmaCopy16', 'MI_DmaCopy32Async']),
}
shim = {'memset', 'MIi_CpuCopy32', 'MIi_CpuCopy16', 'MI_DmaCopy32', 'MI_DmaCopy16', 'MI_DmaCopy32Async'}
work = Path(tempfile.mkdtemp())
try:
    for ar, (members, syms) in write_syms.items():
        listing = subprocess.check_output([AR, 't', str(ar)], text=True).split()
        for m in members:
            assert listing.count(m) == 1, (ar, m, listing.count(m))
            subprocess.run([AR, 'x', str(ar), m], cwd=work, check=True)
            undef = subprocess.check_output([NM, '-u', str(work/m)], text=True).split()
            used = [s for s in syms if s in undef]
            missing = [s for s in used if s not in shim]
            assert not missing, (m, 'needs a shim for', missing)
            if used:
                subprocess.run([OBJCOPY] + sum((['--redefine-sym', f'{s}=PSPNativeVramW_{s}'] for s in used), []) + [str(work/m)], check=True)
            print(f'{m}: redirected {used}')
        subprocess.run([AR, 'r', str(ar)] + [str(work/m) for m in members], check=True)
finally:
    shutil.rmtree(work)
