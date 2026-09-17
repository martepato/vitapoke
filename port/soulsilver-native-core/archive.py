"""Package only successful real game C objects; no trap objects or Platinum game code."""
from pathlib import Path
import subprocess,json
b=Path(__file__).resolve().parent;out=b/'libsoulsilver-c.a';objs=[];rows=[]
for src in sorted((b/'src').rglob('*.c')):
 name=str(src.relative_to(b/'src'))[:-2].replace('/','__');obj=b/'objects'/f'{name}.o';log=b/'objects'/f'{name}.log'
 success=obj.exists() and log.exists() and not any(s in log.read_text() for s in ['error:','Error:'])
 rows.append({'name':name,'success':success})
 if success:objs.append(str(obj))
tmp=b/'libsoulsilver-c.next.a';tmp.unlink(missing_ok=True)
subprocess.run(['@TOOLBIN@ar','rcs',str(tmp),*objs],check=True);tmp.replace(out)
(b/'compile-results-all.json').write_text(json.dumps(rows,indent=2)+'\n')
print(f'Archive has {len(objs)}/{len(rows)} real C units')
