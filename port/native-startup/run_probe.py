from pathlib import Path
import subprocess,os
base=Path(__file__).resolve().parent
ar='@TOOLBIN@ar'
archive=base/'libnative-filtered.a';archive.write_bytes((base.parent/'native-sdk-probe/libnative-sdk-probe.a').read_bytes())
names=subprocess.check_output([ar,'t',str(archive)],text=True).splitlines()
remove=[n for n in names if '__os__' in n or n=='libntr__libraries__card__src__card_common.c.o']
subprocess.run([ar,'d',str(archive)]+remove,check=True)
env=dict(os.environ,PSPDEV='@PSPDEV@');env['PATH']=env['PSPDEV']+'/bin:'+env['PATH']
with (base/'build.log').open('w') as log:
 subprocess.run(['make','clean'],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(['make','-j4'],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
stage=base/'memstick/PSP/GAME/NativeStartup';stage.mkdir(parents=True,exist_ok=True)
(stage/'EBOOT.PBP').write_bytes((base/'EBOOT.PBP').read_bytes())
rom=Path('@PLATINUM_ROM@')
link=stage/'Platinum.nds'
if not link.is_symlink():
 if link.exists():raise RuntimeError('Refusing to replace ROM')
 link.symlink_to(rom)
assert link.resolve()==rom
ppsspp='@PPSSPP@'
with (base/'run.log').open('w') as log:
 subprocess.run([ppsspp,'--memstick='+str(base/'memstick'),'--timeout=5','--log','--loglevel=4','--graphics=software',str(stage/'EBOOT.PBP')],cwd=base,stdout=log,stderr=subprocess.STDOUT,check=True)
text=(base/'run.log').read_text();assert '[STARTUP] validation failures=0 VBlankTasks=1 frame=1 FS=1' in text
print('\n'.join(line for line in text.splitlines() if '[STARTUP]' in line))
