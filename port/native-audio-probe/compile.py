from pathlib import Path
import subprocess,json
b=Path(__file__).resolve().parent;r=b.parent/'native-graphics/libntr'
flags=['-O2',*'@TARGETCC@'.split(),'-ffunction-sections','-fdata-sections','-DSDL_MAIN_HANDLED','-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-DSDK_VERSION_MAJOR=4','-I'+str(b.parent/'native-probe/backing/include'),'-I'+str(b.parent/'native-graphics/alias-proof/include'),'-I'+str(r/'include'),'-I'+str(b.parent/'native-probe/generated'),'-I'+str(r/'libraries/sim/include'),'-include',str(r/'include/pch/nitro_pch.h')]
files=list((r/'libraries/sim7/src/snd').glob('*.c'))+[r/'libraries/sim/src/sim_audio.cpp',r/'libraries/sim/src/blip-buf/blip_buf.c']
results=[]
for src in files:
 iscpp=src.suffix=='.cpp';tool='@TOOLBIN@g++' if iscpp else '@TOOLBIN@gcc';obj=b/(src.stem+'.o')
 extra=['-std=gnu++17','-fno-exceptions','-fno-rtti'] if iscpp else ['-std=gnu99']
 result=subprocess.run([tool]+flags+extra+['-c',str(src),'-o',str(obj)],capture_output=True,text=True)
 (b/(src.stem+'.log')).write_text(result.stderr);results.append(dict(source=str(src),ok=result.returncode==0,errors=[s for s in result.stderr.splitlines() if 'error:' in s]));print(src.name,result.returncode)
(b/'results.json').write_text(json.dumps(results,indent=2))
