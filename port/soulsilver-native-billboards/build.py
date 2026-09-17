from pathlib import Path
import re,json,subprocess,shutil
b=Path(__file__).resolve().parent;c=b.parent/'soulsilver-native-core';p=b.parent/'native-probe/pokeplatinum';a=b.parent/'soulsilver-native-audit/pokeheartgold/asm'
for name in ['billboard','billboard_vram_transfer','billboard_gfx_sequence','easy3d']:
 shutil.copyfile(p/'include'/(name+'.h'),b/'include'/(name+'.h'))
s=(p/'src/billboard.c').read_text();s=re.sub(r'#ifdef SDK_PORT\n.*?#endif\n','',s,flags=re.S)
mapping=json.loads((b/'verified-aliases.json').read_text());assert len(mapping)==68 and len(set(mapping.values()))==68
mapping['Easy3D_DrawRenderObj']='GF3dRender_DrawModel'
mapping.update({'BillboardListVRAMTransfer_New':'sub_0202068C','BillboardListVRAMTransfer_Free':'sub_020206C8','BillboardListVRAMTransfer_NewBillboardTransfer':'sub_020206E0','BillboardVRAMTransfer_Reset':'sub_02020738','BillboardVRAMTransfer_Request':'sub_02020764'})
# Explicit semantic mapping audited against all original functions/call edges.
(b/'aliases.json').write_text(json.dumps(mapping,indent=2)+'\n')
(b/'include/aliases.h').write_text(''.join('#define '+k+' '+v+'\n' for k,v in mapping.items() if k!=v))
s=s.replace('static BillboardList *sBillboardLists = NULL;\nstatic int sBillboardListCount = 0;', 'extern struct {BillboardList *lists; int count;} ssdata_unk_02023694__021D2208;\n#define sBillboardLists ssdata_unk_02023694__021D2208.lists\n#define sBillboardListCount ssdata_unk_02023694__021D2208.count')
s+='\n'+(b/'ss_extras.inc').read_text()
s='#include "aliases.h"\n#include <string.h>\n'+s
(b/'billboard.c').write_text(s)
cmd=json.loads((c/'compile-command.json').read_text());cmd.insert(1,'-I'+str(b/'include'))
with (b/'compile.log').open('w') as f:subprocess.run(cmd+['-c',str(b/'billboard.c'),'-o',str(b/'billboard.o')],stdout=f,stderr=f,check=True)

with (b/'gfx-compile.log').open('w') as f:subprocess.run(cmd+['-c',str(b/'gfx_sequence.c'),'-o',str(b/'gfx_sequence.o')],stdout=f,stderr=f,check=True)
subprocess.run(['@TOOLBIN@ar','rcs',str(b/'libss-billboards-candidate.a'),str(b/'billboard.o'),str(b/'gfx_sequence.o')],check=True)
