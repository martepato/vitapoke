"""Compile NitroMain-closure ISA candidates, without exporting unaudited ABIs."""
from pathlib import Path
import json,re,subprocess,os,concurrent.futures,hashlib
import translate as t
h=t.HERE;out=h/'candidate-objects';out.mkdir(exist_ok=True)
rows=json.loads((h/'support-inventory.json').read_text())
data_maps={Path(r['file']).name:r.get('symbols',{}) for r in json.loads((h.parent/'soulsilver-native-data/inventory.json').read_text())}
cc=Path('@TOOLBIN@gcc')
def compile_one(r):
 name=r['name'];file=r['file'];text=(t.SRC/file).read_text();body=re.search(r'thumb_func_start '+name+r'\s*\n(.*?)thumb_func_end '+name,text,re.S).group(1)
 code,ops=t.emit(name,body,data_maps.get(file,{}));src=out/(name+'.c');obj=out/(name+'.o');src.write_text(t.prefix+'\n'+code+'\n')
 p=subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(src),'-o',str(obj)],capture_output=True,text=True)
 return dict(name=name,file=file,compile_pass=p.returncode==0,errors=p.stderr,object_bytes=obj.stat().st_size if p.returncode==0 else 0,abi_audited=False,exported=False)
selected=[r for r in rows if r['accepted'] and r['NitroMain_dependency']]
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:results=list(pool.map(compile_one,selected))
(h/'candidate-compile.json').write_text(json.dumps(results,indent=2)+'\n')
print('Candidates',len(results),'native compile passes',sum(r['compile_pass'] for r in results),'not exported or added to integration archive')
for r in results:
 if not r['compile_pass']:print(r['name'],r['errors'][:200])
