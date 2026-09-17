from pathlib import Path
import subprocess
p=Path(__file__).resolve().parent;b=p.parent/'native-probe';r=b/'pokeplatinum'
flags=['-DPM_KEEP_ASSERTS','-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-DPOKEPLATINUM_GENERATED_ENUM','-DSDK_VERSION_MAJOR=4','-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-I'+str(b/'generated'),'-I'+str(b/'libntrsystem/include'),'-I'+str(b.parent/'native-graphics/libntr/include'),'-I'+str(r/'include'),'-include',str(r/'include/pch/global_pch.h')]
for module in ['math_util','sys_task_manager']:
 with (p/(module+'-build.log')).open('w') as log:subprocess.run(['@TOOLBIN@gcc']+flags+['-c',str(r/'src'/(module+'.c')),'-o',str(p/(module+'.o'))],stdout=log,stderr=log,check=True)
script=subprocess.check_output(['@TOOLBIN@gcc','-E','-P','-x','assembler-with-cpp','-D__ASSEMBLER__','-I'+str(r/'asm'),'-I'+str(r/'include'),'-I'+str(b/'generated'),str(r/'src/battle/trainer_ai/script.s')],text=True)
(p/'trainer_ai.s').write_text(script+'\n.global gTrainerAITableEnd\ngTrainerAITableEnd:\n')
subprocess.run(['@TOOLBIN@as','-EL',str(p/'trainer_ai.s'),'-o',str(p/'trainer_ai.o')],check=True)
seed=0x12345678;check=0
for _ in range(1000):
 seed=(seed*0x41C64E6D+0x6073)&0xffffffff;check=(check*31+(seed>>16))&0xffffffff
(p/'expected.h').write_text(f'#define EXPECTED_SEED 0x{seed:08X}u\n#define EXPECTED_CHECK 0x{check:08X}u\n')
# pspoke only needs trainer_ai.o; the standalone proof EBOOT is not built.
