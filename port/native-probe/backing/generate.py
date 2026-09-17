from pathlib import Path
import re
b=Path(__file__).resolve().parent;src=b.parent.parent/'native-graphics/libntr/include/simulator/simvariables.h';s=src.read_text()
# Graphics registers use independently verified canonical alias storage.
s=re.sub(r'^REGType\w+\s+s_reg_(?:G2|G2S|G3|G3X|GX|GXS)_\w+;\s*$', '',s,flags=re.M)
# Keep all SDK capacities, with explicit assembler for palette contiguity and real 16KiB DTCM.
s=re.sub(r'^u32 SDK_AUTOLOAD_DTCM_START\[2\];','',s,flags=re.M)
for name in ['s_HW_BG_PLTT','s_HW_OBJ_PLTT','s_HW_DB_BG_PLTT','s_HW_DB_OBJ_PLTT']:
 s=re.sub(r'^u8 '+name+r'\[[^\]]+\];','',s,flags=re.M)
# s_HW_MAIN_MEM_EX is the DSi (TWL) extended main RAM. These are NTR (SDK_4M) games and nothing references
# the buffer -- only the HW_MAIN_MEM_EX_SIZE macro (a constant) is used, in nvram bounds checks -- so drop its
# 8 MiB reservation. Removed, not shrunk, so any real reference fails at link instead of silently corrupting.
s=re.sub(r'^u8 s_HW_MAIN_MEM_EX\[[^\]]+\];','',s,flags=re.M)
# Ensure base memory buffers and OSArenaInfo have at least cache-line alignment.
s=re.sub(r'^(u8 s_HW_\w+\[[^\]]+\]);',r'\1 __attribute__((aligned(64)));',s,flags=re.M)
(b/'storage.c').write_text('/* Generated from actual libntr simvariables.h; see README. */\n#include <nitro.h>\n'+s+'\nGXVRamTex s_SIM_GXVRamTex;\nGXVRamTexPltt s_SIM_GXVRamTexPltt;\nGXVRamBGExtPltt s_SIM_GXBgExtPltt;\nGXVRamSubBGExtPltt s_SIM_GXSubBgExtPltt;\n')
asm=['.section .bss','.balign 64','.global VitaNative_PaletteRAM','.type VitaNative_PaletteRAM,%object','VitaNative_PaletteRAM:']
for name in ['s_HW_BG_PLTT','s_HW_OBJ_PLTT','s_HW_DB_BG_PLTT','s_HW_DB_OBJ_PLTT']:
 asm += ['.global '+name,'.type '+name+',%object',name+':','.space 512','.size '+name+',512']
asm += ['.size VitaNative_PaletteRAM,2048','.balign 64','.global SDK_AUTOLOAD_DTCM_START','.type SDK_AUTOLOAD_DTCM_START,%object','SDK_AUTOLOAD_DTCM_START:','.space 16384','.size SDK_AUTOLOAD_DTCM_START,16384']
(b/'memory_layout.S').write_text('\n'.join(asm)+'\n')
