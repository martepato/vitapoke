from pathlib import Path
import subprocess, concurrent.futures,re,json
b=Path(__file__).resolve().parent;r=b/'pokeplatinum';out=b/'batchobjects';out.mkdir(exist_ok=True)
flags=['@TOOLBIN@gcc','-ffile-prefix-map='+str(b.parent)+'=/pspoke/build/tree/test-out/deterministic-path-token-----','-O2',*'@TARGETCC@'.split(),'-ffunction-sections','-fdata-sections','-D@SDKBUILD@','-DSDK_VERSION_MAJOR=4','-DPOKEPLATINUM_GENERATED_ENUM','-std=gnu99','-DGAME_VERSION=VERSION_PLATINUM','-DGAME_LANGUAGE=LANGUAGE_ENGLISH','-D_NITRO','-DLINK_PPWLOBBY','-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-I'+str(b.parent/'native-graphics/alias-proof/include'),'-I'+str(b/'generated'),'-I'+str(b/'libntrsystem/include'),'-I'+str(b.parent/'native-graphics/libntr/include'),'-I'+str(b/'libvct/include'),'-I'+str(b/'generated/res'),'-Iinclude','-I.','-I'+str(b/'generated/generated'),'-I'+str(b/'libntrdwc/include'),'-I'+str(b/'libntrdwc/include/gs'),'-I'+str(b/'libntrdwc/include/base'),'-I'+str(b/'libntrwifi/include'),'-Ilib/spl/include','-Ilib/gds/include','-Ilib/crypto/include','-Ilib/ppwlobby/include','-include','include/pch/global_pch.h']
flags[1:1]=['-I'+str(p) for p in [b.parent/'native-backup-probe/include',b.parent/'native-fonts/include',b.parent/'native-input/include',b.parent/'native-startup/include',b/'backing/include']]
previous=json.loads((b/'batch-results.json').read_text()) if __import__('os').environ.get('RETRY_FAILED') else None
files=re.findall(r"'([^']+\.c)'",(r/'src/meson.build').read_text())
if previous:files=[x['file'] for x in previous if not x['ok']]
def compile(f):
 name=f[:-2].replace('/','__'); obj=out/(name+'.o')
 if obj.exists():obj.unlink()
 p=subprocess.run(flags+['-c','src/'+f,'-o',str(obj)],cwd=r,capture_output=True,text=True)
 (out/(name+'.log')).write_text(p.stderr)
 errors=[s for s in p.stderr.splitlines() if 'error:' in s]
 return {'file':f,'ok':p.returncode==0,'errors':errors}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 results=list(pool.map(compile,files))
if previous:
 done={x['file']:x for x in results};results=[done.get(x['file'],x) for x in previous]
(b/'batch-results.json').write_text(json.dumps(results,indent=2))
print('COMPILED',sum(x['ok'] for x in results),'OF',len(results),flush=True)
objs=sorted(str(p) for p in out.glob('*.o'))
archive=b/'libplatinum-probe.a'
if archive.exists():archive.unlink()
subprocess.run(['@TOOLBIN@ar','rcs',str(b/'libplatinum-probe.a')]+objs,check=True)
with (b/'object-sizes.txt').open('w') as f:subprocess.run(['@TOOLBIN@size']+objs,stdout=f,check=True)
