from pathlib import Path
import subprocess,os,json
base=Path(__file__).resolve().parent
ar='@TOOLBIN@ar'
archive=base/'libnative-filtered.a';archive.write_bytes((base.parent/'native-sdk-probe/libnative-sdk-probe.a').read_bytes())
names=subprocess.check_output([ar,'t',str(archive)],text=True).splitlines()
remove=[n for n in names if '__os__' in n or '__pxi__' in n or n=='libntr__libraries__card__src__card_common.c.o' or n in ['libntr__libraries__snd__src__snd_bank.c.o','libntrsystem__libraries__snd__src__sndarc_loader.c.o','libntr__libraries__mi__src__mi_memory.c.o','libntr__libraries__mi__src__mi_dma.c.o']]
subprocess.run([ar,'d',str(archive)]+remove,check=True)
overrides=json.loads((base/'override-symbols.json').read_text())
objcopy='@TOOLBIN@objcopy'
for name in ['libplatinum-probe.a','libplatinum-internal.a','libnetwork-probe.a']:
 subprocess.run([objcopy]+['--weaken-symbol='+s for s in overrides]+[str(base.parent/'native-app/overlays/libplatinum-overlays.a') if name=='libplatinum-probe.a' else str(base.parent/'native-probe'/name),str(base/name)],check=True)
subprocess.run([objcopy]+['--weaken-symbol='+s for s in overrides]+[str(archive)],check=True)
env=dict(os.environ,PSPDEV='@PSPDEV@');env['PATH']=env['PSPDEV']+'/bin:'+env['PATH']
with (base/'build.log').open('w') as log:
 subprocess.run(['make','clean'],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(['make','-j4'],cwd=base,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
stage=base/'memstick/PSP/GAME/NativeSound';stage.mkdir(parents=True,exist_ok=True)
fixture=stage/'synthetic-blank512.bin'
if fixture.exists() and fixture.is_symlink():raise RuntimeError('Refusing symlink fixture')
fixture.write_bytes(bytes([255])*(512*1024))
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
assert fixture.read_bytes()==bytes([255])*(512*1024),'Initialization unexpectedly wrote fixture'
text=(base/'run.log').read_text();assert '[INPUT-RTC] failures=0' in text;assert '[SAVE] initialized failures=0' in text;assert '[SOUND] 200 ticks returned' in text;assert '[STARTUP] validation failures=0 VBlankTasks=1 frame=1 FS=1' in text
print('\n'.join(line for line in text.splitlines() if '[STARTUP]' in line or '[INPUT-RTC]' in line or '[SAVE]' in line or '[SOUND]' in line or '[AUDIO' in line))
