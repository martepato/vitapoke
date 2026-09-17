from pathlib import Path
import re,json,subprocess,struct,sys,concurrent.futures,shutil
B=Path(__file__).resolve().parent;C=B.parent/'soulsilver-native-core';S=B.parent/'soulsilver-research/pokeheartgold-slop'
sys.path.insert(0,str(B.parent/'soulsilver-native-data/python'));import ndspy.rom
rom=ndspy.rom.NintendoDSRom.fromFile(Path('@SOULSILVER_ROM@'));overlays=rom.loadArm9Overlays()
memberOwner={};asmOwner={};current=None;count=0;unsupported=set()
for line in (S/'main.lsf').read_text().splitlines():
 m=re.match(r'^(Static|Autoload|Overlay)\s+(\w+)',line)
 if m:
  current=count if m[1]=='Overlay' else None
  if m[1]=='Overlay':count+=1
 m=re.match(r'\s*Object\s+(src|asm)/([^ ]+)\.o',line)
 if m:
  key=m[2].replace('/','__')+'.o';memberOwner[key]=current
  if m[1]=='asm':asmOwner[Path(m[2]).stem+'.o']=current
 if line.strip().startswith('Object lib/') and current is not None:unsupported.add(current)
meta=[]
for i,o in overlays.items():
 values=struct.unpack('<'+'I'*((o.staticInitEnd-o.staticInitStart)//4),o.data[o.staticInitStart-o.ramAddress:o.staticInitEnd-o.ramAddress])
 if any(values):unsupported.add(i)
 meta.append({'id':i,'ram_address':o.ramAddress,'ram_size':o.ramSize,'bss_size':o.bssSize,'file_id':o.fileID,'initializers':list(values),'supported_lifetime':i not in unsupported})
(B/'rom-metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
# Rebuild private archive copies; original game objects and code remain unchanged.
archives=[(C/'libsoulsilver-c.a','game',memberOwner),(B.parent/'soulsilver-native-data/libsoulsilver-data.a','data',asmOwner)]
results=[]
for source,tag,mapping in archives:
 out=B/tag;out.mkdir(exist_ok=True)
 subprocess.run(['@TOOLBIN@ar','x',str(source)],cwd=out,check=True)
 names=subprocess.check_output(['@TOOLBIN@ar','t',str(source)],text=True).splitlines()
 def convert(name):
  p=out/name;owner=mapping.get(name);missing=name not in mapping;reason='linker-object'
  if missing:
   basename=name.split('__')[-1]
   matches={v for k,v in memberOwner.items() if k.split('__')[-1]==basename}
   if len(matches)==1:owner=matches.pop();missing=False;reason='unique-source-basename'
   else:
    m=re.search(r'(?:^|__)overlay_(\d+)(?:_|\.)',name)
    if m:owner=int(m[1]);missing=False;reason='explicit-overlay-number'
    elif re.match(r'unk_(?:data_)?020[0-9a-fA-F]+\.o$',name):owner=None;missing=False;reason='resident-ARM9-address'
  if missing:raise RuntimeError('Unclassified object '+tag+'/'+name)
  sec=subprocess.check_output(['@TOOLBIN@objdump','-h',str(p)],text=True)
  sections=re.findall(r'^\s*\d+\s+(\S+)\s+[0-9a-f]+',sec,re.M);args=[];changes=[]
  if owner is not None:
   for s in sections:
    kind='bss' if s.startswith(('.bss','.sbss')) else 'data' if s.startswith(('.data','.sdata')) else None
    if kind:
     new=f'.ssov.{owner}.{kind}'+s;args+=['--rename-section',s+'='+new];changes.append([s,new])
   if args:subprocess.run(['@TOOLBIN@objcopy',*args,str(p)],check=True)
  return {'archive':tag,'member':name,'overlay':owner,'unmapped':missing,'classification':reason,'sections':changes}
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results.extend(pool.map(convert,names))
 target=B/('libss-'+tag+'-overlays.a');target.unlink(missing_ok=True)
 subprocess.run(['@TOOLBIN@ar','rcs',str(target),*[str(out/name) for name in names]],check=True)
(B/'membership.json').write_text(json.dumps(results,indent=2)+'\n')
# Embed ranges into existing PSP linker script, before ordinary data and BSS.
base=B/'base-linkfile.prx'
if not base.exists():
 base.write_text(re.sub(r'^  \.ssov\.\d+\.(?:data|bss).*\n', '', (C/'nitromain-proof/linkfile.prx').read_text(), flags=re.M))
script=base.read_text();data=[];bss=[];decl=[];ranges=[]
for i in range(count):
 for kind,dest in [('data',data),('bss',bss)]:
  prefix=f'ss_ov_{i}_{kind}';dest.append(f'  .ssov.{i}.{kind} '+('(NOLOAD) ' if kind=='bss' else '')+f': ALIGN(16) {{ {prefix}_start = .; KEEP(*(.ssov.{i}.{kind}*)) {prefix}_end = .; }}')
  decl.append(f'extern unsigned char {prefix}_start[],{prefix}_end[];')
 ranges.append('{'+','.join(f'ss_ov_{i}_{kind}_{edge}' for kind in ['data','bss'] for edge in ['start','end'])+'}')
script=script.replace('  .data           :','\n'.join(data)+'\n  .data           :').replace('  .bss            :','\n'.join(bss)+'\n  .bss            :');(B/'linkfile.prx').write_text(script)
(B/'ranges.h').write_text('\n'.join(decl)+'\nstatic struct NativeRange ranges[]={\n'+',\n'.join(ranges)+'\n};\n')
(B/'rom_bounds.h').write_text('static const struct RomBounds bounds[]={\n'+',\n'.join('{'+','.join(str(x) for x in [m['ram_address'],m['ram_size'],m['bss_size'],m['file_id'],int(m['supported_lifetime'])])+'}' for m in meta)+'\n};\n')
print('archives',len(results),'unmapped',[x['member'] for x in results if x['unmapped']]);print('unsupported overlays',sorted(unsupported))
