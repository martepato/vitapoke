from pathlib import Path
import subprocess,shutil,re
p=Path(__file__).resolve().parent;b=p.parent.parent/'native-probe';out=p/'internal';out.mkdir(exist_ok=True)
for src in (b/'internalobjects').glob('*.o'):
 dst=out/src.name;shutil.copyfile(src,dst)
 i=61 if src.name.startswith('lib__gds__') else 97 if src.name in ['lib__crypto__src__'+x+'.c.o' for x in ['sign','bignum','util']] else None
 if i is None:continue
 sec=subprocess.check_output(['@TOOLBIN@objdump','-h',str(dst)],text=True);ren=[]
 for name in re.findall(r'^\s*\d+\s+(\S+)\s+[0-9a-f]+',sec,re.M):
  k='bss' if name.startswith(('.bss','.sbss')) else 'data' if name.startswith(('.data','.sdata')) else None
  if k:ren+=['--rename-section',name+f'=.nativeov.{i}.{k}'+name]
 if ren:subprocess.run(['@TOOLBIN@objcopy',*ren,str(dst)],check=True)
a=p/'libinternal-overlays.a';a.unlink(missing_ok=True);subprocess.run(['@TOOLBIN@ar','rcs',str(a),*[str(x) for x in out.glob('*.o')]],check=True)
nm=subprocess.check_output(['@TOOLBIN@nm','--defined-only',str(p/'libplatinum-overlays.a')],text=True)
ctors=sorted(set(re.findall(r'PSPNativeCtor_\d+',nm)));(p/'constructors.flags').write_text(' '.join('-Wl,-u,'+x for x in ctors)+'\n');print('constructor modules',len(ctors),ctors)
