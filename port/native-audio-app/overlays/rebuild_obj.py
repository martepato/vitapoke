# Recompile edited overlays/source/<name>.c copies with build.py's exact flags and
# section renames, then replace the members in libplatinum-overlays.a. Never regenerates copies.
import sys,re,json,subprocess
from pathlib import Path
p=Path(__file__).resolve().parent;b=p.parent.parent/'native-probe';r=b/'pokeplatinum'
ns={'__file__':str(b/'crossprobe-batch.py')};exec((b/'crossprobe-batch.py').read_text().split('previous=')[0],ns)
flags=ns['flags'];flags+=['-DPM_KEEP_ASSERTS','-DVITAPOKE_OFFLINE','-DVITAPOKE_MUTED'];flags[1:1]=['-I'+str(p/'include')]
mapping=json.loads((p/'source-membership.json').read_text())
objs=[]
for rel in sys.argv[1:]:
 local=p/'source'/rel.replace('/','__');src=r/'src'/rel;i=mapping.get(rel)
 obj=p/'objects'/(rel[:-2].replace('/','__')+'.o')
 cmd=flags+['-I'+str(src.parent)]+(['-DVITAPOKE_OV_ID='+str(i)] if i is not None else [])+['-Wreturn-type','-c',str(local),'-o',str(obj)]
 q=subprocess.run(cmd,cwd=r,capture_output=True,text=True);print(rel,'rc',q.returncode);print('\n'.join(l for l in q.stderr.splitlines() if 'error' in l or 'warning: implicit' in l or 'Wreturn' in l))
 if q.returncode:sys.exit(1)
 if i is not None:
  sec=subprocess.check_output(['@TOOLBIN@objdump','-h',str(obj)],text=True)
  ren=[]
  for name in re.findall(r'^\s*\d+\s+(\S+)\s+[0-9a-f]+',sec,re.M):
   kind='bss' if name.startswith(('.bss','.sbss')) else 'data' if name.startswith(('.data','.sdata')) else 'sinit' if name=='.psp_sinit' else None
   if kind:ren+=['--rename-section',name+f'=.nativeov.{i}.{kind}'+name]
  if ren:subprocess.run(['@TOOLBIN@objcopy',*ren,str(obj)],check=True)
 objs.append(str(obj))
subprocess.run(['@TOOLBIN@ar','r',str(p/'libplatinum-overlays.a'),*objs],check=True)
# The archive's digest, so a rerun that should have changed nothing can be seen not to have. Computed
# here rather than shelling out to md5, which is spelled differently on every system.
import hashlib
print('archive',hashlib.md5((p/'libplatinum-overlays.a').read_bytes()).hexdigest())
