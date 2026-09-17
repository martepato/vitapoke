from pathlib import Path
import json
p=Path(__file__).resolve().parent;mods=json.loads((p/'modules.json').read_text());ld=['SECTIONS {'];decl=[];rows=[]
for m in mods:
 i=m['id']
 for k in ['data','sinit','bss']:
  n=f'psp_ov_{i}_{k}';ld.append(f'.nativeov.{i}.{k} '+('(NOLOAD) ' if k=='bss' else '')+f': ALIGN(16) {{ {n}_start = .; KEEP(*(.nativeov.{i}.{k}*)) {n}_end = .; }}')
  decl.append(f'extern unsigned char {n}_start[], {n}_end[];')
 rows.append('{'+','.join(f'psp_ov_{i}_{k}_{s}' for k in ['data','bss','sinit'] for s in ['start','end'])+'}')
ld+=['} INSERT AFTER .data;'];(p/'overlays.ld').write_text('\n'.join(ld)+'\n')
(p/'ranges.h').write_text('\n'.join(decl)+'\nstatic struct Range ranges[]={\n'+',\n'.join(rows)+'\n};\n')

# The overlay layout is built by editing a base linker script. The Vita's has still to be written
# (see docs/VITA.md); port/build/overlays.ld is where it goes.
base=Path('@OVERLAYLD@').read_text()
# Linker GC must retain the PSP syslib exports, including module_start.
base=base.replace('.lib.ent        : { *(.lib.ent) }', '.lib.ent        : { KEEP(*(.lib.ent)) }')
# Retail CheckElfSectionPRX rejects sh_offset+sh_size >=32MiB even for NOBITS.
# Split canonical backing from other BSS; runtime addresses still use MEMSIZE=1.
(p/'linkfile.prx').write_text(base.replace('  .bss            :', '\n'.join(ld[1:-1])+'\n  .native.backing (NOLOAD) : { *backing.o(.bss) }\n  .bss            :'))
