from pathlib import Path
import subprocess,json,concurrent.futures
base=Path(__file__).resolve().parent
root=base.parent/'soulsilver-research/pokeheartgold-slop'; shim=base/'include'
inc=[shim,base/'game-include',base/'game-include/library',base/'src',root/'files',base.parent/'native-probe/generated',base.parent/'native-probe/libntrsystem/include',base.parent/'native-graphics/libntr/include']
(base/'objects').mkdir(exist_ok=True)
cmd=['@TOOLBIN@gcc','-O2','-include',str(shim/'game-pack4.h'),'-Wno-incompatible-pointer-types','-DNONMATCHING','-Wno-implicit-function-declaration','-Wno-int-conversion',*'@TARGETCC@'.split(),'-std=gnu99','-include',str(shim/'compat.h'),'-DPM_KEEP_ASSERTS','-DPSP_NATIVE_OFFLINE','-DSOULSILVER','-DENGLISH','-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-D@SDKBUILD@','-ffunction-sections','-fdata-sections']+['-I'+str(p) for p in inc]
def run(p):
 name=str(p.relative_to(base/'src'))[:-2].replace('/','__')
 (base/'objects'/f'{name}.o').unlink(missing_ok=True)
 r=subprocess.run(cmd+['-c',str(p),'-o',str(base/'objects'/f'{name}.o')],cwd=root,capture_output=True,text=True)
 (base/'objects'/f'{name}.log').write_text(r.stdout+r.stderr)
 errors=[s for s in r.stderr.splitlines() if 'error:' in s or 'fatal error:' in s]
 return dict(name=name,success=r.returncode==0,errors=errors[:5])
import sys
(base/'compile-command.json').write_text(json.dumps(cmd))
files=[base/'src'/x for x in sys.argv[1:]] if len(sys.argv)>1 else list((base/'src').rglob('*.c'))
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 outcomes=list(pool.map(run,files))
(base/'compile-results.json').write_text(json.dumps(outcomes,indent=2))
if not sys.argv[1:]: (base/'compile-results-all.json').write_text(json.dumps(outcomes,indent=2))
print('PASS',sum(x['success'] for x in outcomes),'/',len(outcomes))
for x in outcomes:
 if not x['success']: print(x['name'],x['errors'][:1])
