from pathlib import Path
import subprocess
base=Path(__file__).resolve().parent
root=base/'pokeplatinum'
cmd=['@TOOLBIN@gcc','-O2','-DPOKEPLATINUM_GENERATED_ENUM','-std=gnu99','-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-I'+str(base/'generated'),'-I'+str(base/'libntrsystem/include'),'-I'+str(base.parent/'native-graphics/libntr/include'),'-Iinclude','-I.','-include','include/pch/global_pch.h']
import re
files=[p[:-2] for p in re.findall(r"'([^']+\.c)'", (root/'src/meson.build').read_text())]
(base/'allobjects').mkdir(exist_ok=True)
for name in files:
 p=root/'src'/f'{name}.c'
 name=name.replace('/', '__')
 if not p.exists(): continue
 r=subprocess.run(cmd+['-c',str(p),'-o',str(base/'allobjects'/f'{name}.o')],cwd=root,capture_output=True,text=True)
 (base/'allobjects'/f'{name}.log').write_text(r.stderr)
 errors=[s for s in r.stderr.splitlines() if 'error:' in s]
 print(name,'PASS' if r.returncode==0 else 'FAIL', '\n'.join(errors[:2]))
