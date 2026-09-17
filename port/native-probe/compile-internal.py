from pathlib import Path
import concurrent.futures,json,subprocess
exec(Path(__file__).with_name('crossprobe-batch.py').read_text().split('previous=')[0])
out=b/'internalobjects';out.mkdir(exist_ok=True)
files=sorted(list((r/'lib').rglob('*.c'))+list((r/'lib').rglob('*.cpp')))
def comp(p):
 f=flags.copy();name=str(p.relative_to(r)).replace('/','__');obj=out/(name+'.o')
 # The C++ sources here are the game's Wi-Fi lobby library, and the game's precompiled header is C,
 # so it is dropped. It has to be dropped by name: there are other -include flags on the command
 # line (the DS SDK wants stdlib.h and ctype.h declared for it), and dropping the first one found
 # would leave the precompiled header in place -- along with the min() and max() function-like
 # macros it defines, which libstdc++'s headers do not survive.
 if p.suffix=='.cpp':
  f[0]=f[0].replace('@TOOLBIN@gcc','@TOOLBIN@g++');f=[x for x in f if x!='-std=gnu99']
  f+=['-std=gnu++03','-fpermissive']
  pch=next(i for i,x in enumerate(f) if x.endswith('global_pch.h'));del f[pch-1:pch+1]
 c=subprocess.run(f+['-c',str(p),'-o',str(obj)],cwd=r,capture_output=True,text=True);(out/(name+'.log')).write_text(c.stderr)
 return {'file':str(p.relative_to(r)),'ok':c.returncode==0,'errors':[x for x in c.stderr.splitlines() if 'error:' in x]}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:res=list(pool.map(comp,files))
(b/'internal-results.json').write_text(json.dumps(res,indent=2));print('INTERNAL',sum(x['ok'] for x in res),'of',len(res))
for x in res:
 if not x['ok']:print(x['file'],'\n'.join(x['errors'][:3]))
a=b/'libplatinum-internal.a'
if a.exists():a.unlink()
subprocess.run(['@TOOLBIN@ar','rcs',str(a)]+[str(p) for p in out.glob('*.o')],check=True)
