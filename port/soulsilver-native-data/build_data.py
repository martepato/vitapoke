"""Extract real ARM source data directives into native PSP relocatable objects.
Never emits code or placeholder data. Unknown directives/preprocessing fail.
"""
from pathlib import Path
import re,subprocess,json,concurrent.futures
BASE=Path(__file__).resolve().parent
SRC=BASE.parent/'soulsilver-research/pokeheartgold-slop'
OUT=BASE/'generated';OUT.mkdir(exist_ok=True)
OBJ=BASE/'objects';OBJ.mkdir(exist_ok=True)
CPPINC=[SRC/'include',SRC/'files',BASE.parent/'soulsilver-native-core/include']
OVERLAYS={name:i for i,name in enumerate(re.findall(r'^Overlay\s+(\w+)',(SRC/'main.lsf').read_text(),re.M))}
NM=subprocess.check_output(['@TOOLBIN@nm','-u',str(BASE.parent/'soulsilver-native-core/libsoulsilver-c.a')],text=True)
EXPORTED={m[1] for m in re.finditer(r'\bU\s+(\S+)',NM)}
SECTIONS={'.rodata':('a','progbits'),'.data':('aw','progbits'),'.bss':('aw','nobits')}
allowed={'.byte','.short','.hword','.word','.space','.skip','.zero','.ascii','.asciz','.string','.balign','.align','.size','.extern','.macro','.endm'}
def convert(p):
 stem=p.stem;lines=p.read_text().splitlines();public=set(EXPORTED)
 public.update(re.findall(r'\.(?:public|global|globl)\s+(\w+)',p.read_text()))
 for inc in re.findall(r'\.include\s+"([^"\n]+)"','\n'.join(lines)):
  ip=SRC/'asm/include'/Path(inc).name
  if ip.exists():public.update(re.findall(r'^\.public\s+(\w+)',ip.read_text(),re.M))
 active=None;blocks=[];labels=[];includes=[];errors=[];macros=set()
 for raw in lines:
  if raw.lstrip().startswith('#include'):includes.append(raw);continue
  line=raw.split(';',1)[0].strip()
  if not line or line.startswith('.include'):continue
  if line.startswith('#'):
   (blocks if active else includes).append(('cpp',raw) if active else raw);continue
  if line in SECTIONS:active=line;blocks.append((line,None));continue
  if line=='.text' or line.startswith('.section'):
   active=None;continue
  if not active:continue
  m=re.match(r'([A-Za-z_]\w*):\s*(.*)',line)
  if m:labels.append(m[1]);line=m[2];blocks.append(('label',m[1]))
  if not line:continue
  if line.startswith(('.global ','.globl ','.public ')):continue
  token=line.split()[0]
  if token=='.macro':macros.add(line.split()[1])
  if token not in allowed and token not in macros:errors.append(line);continue
  # GNU MIPS treats an empty final operand as zero; original ARM data omits it.
  if token in {'.byte','.short','.hword','.word'}:line=line.rstrip(',').rstrip()
  blocks.append(('line',line))
 if not labels:return {'file':str(p.relative_to(SRC)),'status':'no-data'}
 mapping={name:(name if name in public else f'ssdata_{stem}_{name}') for name in labels}
 result={'file':str(p.relative_to(SRC)),'symbols':mapping,'public':sorted(public.intersection(labels)),'unknown':errors}
 if errors:result['status']='unsupported';return result
 text=includes+['/* Generated data only; no ARM instructions. */']
 for kind,line in blocks:
  if kind in SECTIONS:
   flags,typ=SECTIONS[kind];text.append(f'.section {kind}.ss_{stem},"{flags}",@{typ}')
   # The ARM linker word-aligns every DS data section; the original source relies on
   # that instead of writing .balign. Without it gas gives the section alignment 1 and
   # the PSP linker placed blocks at odd addresses: 20 word-sized objects in 11 files
   # were unaligned, and the first 32-bit read of one (fade manager state,
   # unk_0200FA24 _021D0F08) faulted. Allegrex traps unaligned access; PPSSPP's JIT
   # segfaulted on it instead of reporting it (the interpreter, -i, reports it).
   text.append('.balign 4')
  elif kind=='cpp':text.append(line)
  elif kind=='label':text += [f'.global {mapping[line]}',mapping[line]+':']
  else:
   line=re.sub(r'FS_OVERLAY_ID\((\w+)\)',lambda m:str(OVERLAYS[m[1]]),line)
   text.append(re.sub(r'\b[A-Za-z_]\w*\b',lambda m:mapping.get(m[0],m[0]),line))
 target=OUT/(stem+'.S');target.write_text('\n'.join(text)+'\n')
 command=['@TOOLBIN@gcc','-c',*'@TARGETCC@'.split(),'-DSOULSILVER','-DENGLISH','-DPM_ASM']+['-I'+str(x) for x in CPPINC]+[str(target),'-o',str(OBJ/(stem+'.o'))]
 r=subprocess.run(command,cwd=SRC,text=True,capture_output=True);(OBJ/(stem+'.log')).write_text(r.stderr)
 result['status']='pass' if r.returncode==0 else 'compile-fail';result['errors']=r.stderr.splitlines()[-8:]
 return result
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(convert,sorted((SRC/'asm').glob('*.s'))))
(BASE/'inventory.json').write_text(json.dumps(results,indent=2)+'\n')
passed=[str(OBJ/(Path(x['file']).stem+'.o')) for x in results if x['status']=='pass']
if passed:subprocess.run(['@TOOLBIN@ar','rcs',str(BASE/'libsoulsilver-data.a'),*passed],check=True)
from collections import Counter
print(Counter(x['status'] for x in results));print('data symbols',sum(len(x.get('symbols',{})) for x in results if x['status']=='pass'))
