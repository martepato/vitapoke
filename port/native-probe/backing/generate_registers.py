from pathlib import Path
import re,json
p=Path(__file__).resolve().parent;inc=p.parent.parent/'native-graphics/libntr/include/nitro/hw';rows=[];asm=['.section .bss'];allnames=set()
for family in ['CP','MI','OS','PAD','PXI','SND','SPI']:
 path=inc/f'X86/ioreg_{family}.h'
 if not path.exists():continue
 s=path.read_text();armfile=inc/f'ARM9/ioreg_{family}.h'
 if not armfile.exists():armfile=inc/f'ARM7/ioreg_{family}.h'
 arm=armfile.read_text();offsets=dict(re.findall(r'#define\s+(REG_\w+_OFFSET)\s+(0x[0-9a-fA-F]+)',s));actual=dict(re.findall(r'#define\s+(REG_\w+_OFFSET)\s+(0x[0-9a-fA-F]+)',arm));decl={name:int(width)//8 for width,name in re.findall(r'extern REGType(\d+)v\s+(s_reg_\w+)\s*;',s)};found=set();subset=[]
 for key,name in re.findall(r'#define\s+(REG_\w+)_ADDR\s+\(&?(s_reg_\w+)\)',s):
  assert int(offsets[key+'_OFFSET'],16)==int(actual[(key.replace('REG_SND_POWCNT','REG_POWCNT'))+'_OFFSET'],16),(family,key)
  off=int(offsets[key+'_OFFSET'],16);subset.append((name,off,decl[name],family));found.add(name)
 assert found==set(decl),(family,set(decl)-found)
 clusters=[]
 for row in sorted(subset,key=lambda x:x[1]):
  if not clusters or row[1]-clusters[-1][-1][1]>4096:clusters.append([])
  clusters[-1].append(row)
 size=0
 for j,cluster in enumerate(clusters):
  base=min(x[1] for x in cluster)&~63;extent=(max(o+w for _,o,w,_ in cluster)-base+63)&~63;size+=extent;symbol='VitaNative_'+family+'Registers'+str(j)
  asm+=['.balign 64','.global '+symbol,'.type '+symbol+',%object',symbol+':',f'.space {extent}',f'.size {symbol},{extent}']
  for name,off,width,_ in cluster:asm += [f'.global {name}',f'.type {name},%object',f'.set {name},{symbol}+{off-base}',f'.size {name},{width}'];allnames.add(name)
 rows+=subset
 out=p/'include/nitro/hw/X86'/path.name;out.parent.mkdir(parents=True,exist_ok=True);s,n=re.subn(r'(#define\s+REG_\w+_ADDR\s+)\(&?(s_reg_\w+)\)',r'\1((u64)(uintptr_t)&\2)',s);out.write_text('#include <stdint.h>\n'+s)
 print(family,len(subset),size)
(p/'device_registers.S').write_text('\n'.join(asm)+'\n');(p/'registers.json').write_text(json.dumps(rows,indent=2))
s=(p/'storage.c').read_text()
for name in allnames:s=re.sub(r'^REGType\w+\s+'+name+r';\s*$', '',s,flags=re.M)
(p/'storage.c').write_text(s)
