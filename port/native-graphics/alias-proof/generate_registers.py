from pathlib import Path
import re,json
p=Path(__file__).resolve().parent
inc=p.parent/'libntr/include/nitro/hw'
rows=[]
for family in ['G2','G2S','G3','G3X','GX','GXS']:
 s=(inc/f'X86/ioreg_{family}.h').read_text()
 arm=(inc/f'ARM9/ioreg_{family}.h').read_text()
 offsets=dict(re.findall(r'#define\s+(REG_\w+_OFFSET)\s+(0x[0-9a-fA-F]+)',s))
 actual=dict(re.findall(r'#define\s+(REG_\w+_OFFSET)\s+(0x[0-9a-fA-F]+)',arm))
 decl=dict((name,int(width)//8) for width,name in re.findall(r'extern REGType(\d+)v\s+(s_reg_\w+)\s*;',s))
 found=set()
 for key,name in re.findall(r'#define\s+(REG_\w+)_ADDR\s+\(&?(s_reg_\w+)\)',s):
  assert int(offsets[key+'_OFFSET'],16)==int(actual[key+'_OFFSET'],16),(family,key)
  off=int(offsets[key+'_OFFSET'],16)
  rows.append((name,off,decl[name],family));found.add(name)
 assert found==set(decl), (family, set(decl)-found)
rows=sorted(set(rows),key=lambda r:(r[1],r[0]))
size=(max(o+w for _,o,w,_ in rows)+63)&~63
s=['/* Generated from libntr X86 ioreg headers; offsets verified against ARM9. */','.section .bss','.balign 64','.global VitaNative_GfxRegisters','.type VitaNative_GfxRegisters, %object','VitaNative_GfxRegisters:',f'.space {size}',f'.size VitaNative_GfxRegisters, {size}']
for name,off,width,_ in rows:s += [f'.global {name}',f'.type {name}, %object',f'.set {name}, VitaNative_GfxRegisters + {off}',f'.size {name}, {width}']
(p/'graphics_registers.S').write_text('\n'.join(s)+'\n')
(p/'registers.json').write_text(json.dumps(rows,indent=2)+'\n')
print(f'{len(rows)} graphics symbols share {size} bytes')
# Native headers accidentally give ADDR macros typed pointer arithmetic. Hardware
# addresses are byte quantities. Preserve SDK_PORT's 64-bit address API ABI.
for family in ['G2','G2S','G3','G3X','GX','GXS']:
 src=inc/f'X86/ioreg_{family}.h'
 out=p/'include/nitro/hw/X86'/src.name
 out.parent.mkdir(parents=True,exist_ok=True)
 s=src.read_text()
 s,n=re.subn(r'(#define\s+REG_\w+_ADDR\s+)\(&?(s_reg_\w+)\)',r'\1((u64)(uintptr_t) &\2)',s)
 out.write_text("#include <stdint.h>\n"+s)
 print(f'{family}: {n} byte-address macros')
