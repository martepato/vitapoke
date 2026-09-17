from pathlib import Path
import concurrent.futures,json,subprocess
exec(Path(__file__).with_name('crossprobe-batch.py').read_text().split('previous=')[0])
out=b/'internalobjects';out.mkdir(exist_ok=True)
files=sorted(list((r/'lib').rglob('*.c'))+list((r/'lib').rglob('*.cpp')))
def comp(p):
 f=flags.copy();name=str(p.relative_to(r)).replace('/','__');obj=out/(name+'.o')
 if p.suffix=='.cpp':f[0]=f[0].replace('@TOOLBIN@gcc','@TOOLBIN@g++');f=[x for x in f if x!='-std=gnu99'];f+=['-std=gnu++03','-fpermissive'];idx=f.index('-include');del f[idx:idx+2]
 c=subprocess.run(f+['-c',str(p),'-o',str(obj)],cwd=r,capture_output=True,text=True);(out/(name+'.log')).write_text(c.stderr)
 return {'file':str(p.relative_to(r)),'ok':c.returncode==0,'errors':[x for x in c.stderr.splitlines() if 'error:' in x]}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:res=list(pool.map(comp,files))
(b/'internal-results.json').write_text(json.dumps(res,indent=2));print('INTERNAL',sum(x['ok'] for x in res),'of',len(res))
for x in res:
 if not x['ok']:print(x['file'],'\n'.join(x['errors'][:3]))
a=b/'libplatinum-internal.a'
if a.exists():a.unlink()
subprocess.run(['@TOOLBIN@ar','rcs',str(a)]+[str(p) for p in out.glob('*.o')],check=True)
