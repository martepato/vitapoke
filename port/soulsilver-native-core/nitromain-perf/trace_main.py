from pathlib import Path
import re,json,subprocess
b=Path(__file__).resolve().parent;src=(b.parent/'src/main.c').read_text();start=src.index('void NitroMain(void) {');end=src.index('    while (1)',start);part=src[start:end]
part=re.sub(r'^(    )(\w+\([^;\n]*\);)$',lambda m:m[1]+'printf("[SS-INIT] '+m[2].split('(')[0]+'\\n");\n'+m[0],part,flags=re.M)
src='#include <stdio.h>\n#include "save_arrays.h"\n'+src[:start]+part+src[end:];src=src.replace('    _02111868.unk_10.saveData = SaveData_New();', '    for(int i=0;i<gNumSaveChunkHeaders;i++)printf("[SS-SAVE-SIZE] id=%d block=%lu size=%lu\\n",i,(unsigned long)gSaveChunkHeaders[i].block,(unsigned long)gSaveChunkHeaders[i].sizeFunc());\n    _02111868.unk_10.saveData = SaveData_New();\n    printf("[SS-SAVE] constructed exists=%d new=%d status=%lu\\n",_02111868.unk_10.saveData->saveFileExists,_02111868.unk_10.saveData->isNewGame,(unsigned long)_02111868.unk_10.saveData->statusFlags);')
src = src.replace('#include <stdio.h>', '#include <stdio.h>\nextern void VitaNativeFrameComplete(void);')
# Port adaptation, not a game change. On the DS the VBlank IRQ preempts the
# running code, so a scene that queues many VBlank tasks inside one frame keeps
# draining them and the original 32 slots suffice. This port's VBlank is
# cooperative (it only runs at OS_WaitIrq), so field-effect renderer
# initialization queues 20+ tasks in a single frame and overruns the queue
# (GF_ASSERT in sub_02069714). The queue is made deeper; every task still runs
# on a real VBlank, in order, unchanged. Draining early instead would do VRAM
# work outside VBlank.
src = src.replace('    InitSystemForTheGame();',
 '    InitSystemForTheGame();\n'
 '    gSystem.vblankTaskQueue = SysTaskQueue_PlacementNew(256, OS_AllocFromArenaLo(OS_ARENA_MAIN, SysTaskQueue_GetArenaSize(256), 4));\n'
 '    printf("[SS-INIT] vblank task queue deepened to %u (cooperative VBlank port)\\n", gSystem.vblankTaskQueue->limit);', 1)

src = src.replace('        SysTaskQueue_RunTasks(gSystem.vwaitTaskQueue);', '        SysTaskQueue_RunTasks(gSystem.vwaitTaskQueue);\n        VitaNativeFrameComplete();')
(b/'ssmain_trace.c').write_text(src)
cmd=json.loads((b.parent/'compile-command.json').read_text());r=subprocess.run(cmd+['-c',str(b/'ssmain_trace.c'),'-o',str(b/'ssmain_trace.o')],capture_output=True,text=True);(b/'trace-build.log').write_text(r.stderr);print('trace compile',r.returncode)
