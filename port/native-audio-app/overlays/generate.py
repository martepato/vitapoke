# Read the decompilation's own link script and write out what the port needs to know about the
# game's overlay modules: which source files belong to each, and the FS_OVERLAY_ID names the game
# uses to refer to them.
#
# The ROM is not read here. The one thing in it that the port needs -- each module's DS load address
# and size -- is read at startup instead, by overlays/overlay.c, so that the build needs no ROM and
# one build works with any dump of the same game.
from pathlib import Path
import re,json
p=Path(__file__).resolve().parent;b=p.parent.parent/'native-probe';src=b/'pokeplatinum'
lsf=(src/'platinum.us/main.lsf').read_text()
modules=[]
for name,body in re.findall(r'Overlay\s+(\w+)\s*\{(.*?)\}',lsf,re.S):
 files=re.findall(r'Object main.nef.p/src_(\S+)\.c\.o',body)
 modules.append({'id':len(modules),'name':name,'objects':files,'other':[x.strip() for x in body.splitlines() if x.strip().startswith(('Object ','Library ')) and 'Object main.nef.p/src_' not in x]})
(p/'modules.json').write_text(json.dumps(modules,indent=2))
h=p/'include/nitro/fs';h.mkdir(parents=True,exist_ok=True)
s=(p.parent.parent/'native-graphics/libntr/include/nitro/fs/overlay.h').read_text().replace('#define\tFS_OVERLAY_ID(name)\t(0)','#include "native_overlay_ids.h"\n#define FS_OVERLAY_ID(name) (VITAPOKE_OVERLAY_ID_ ## name)')
(h/'overlay.h').write_text(s)
(h/'native_overlay_ids.h').write_text('\n'.join(f'#define VITAPOKE_OVERLAY_ID_{m["name"]} {m["id"]}u' for m in modules)+'\n')
print('overlay modules',len(modules),'other records',[(m['name'],m['other']) for m in modules if m['other']])
