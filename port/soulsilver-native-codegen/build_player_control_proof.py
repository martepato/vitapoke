from pathlib import Path
import player_control as control
h=Path(__file__).resolve().parent;p=h/'player-control-proof';p.mkdir(exist_ok=True)
p.joinpath('Makefile').write_text('''TARGET = ss-player-control-proof
OBJS = main.o
LIBS = ../libss-native-translated.a
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections
BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SoulSilver player control closure proof
include @BUILDMAK@
''')
ps=['typedef struct FieldSystem FieldSystem;'];defs=[];ids={n:i for i,n in enumerate(control.SCALARS)}
for name,(ret,args) in control.SCALARS.items():
 sig=ret+' '+name+'('+','.join(ty+' a'+str(i) for i,ty in enumerate(args))+')';ps.append(sig+';')
 vals=[]
 for i,ty in enumerate(args):vals.append('(uint32_t)(uintptr_t)a'+str(i) if '*' in ty else '(uint32_t)a'+str(i))
 body='event('+str(ids[name]+1)+','+','.join(vals+['0']*(3-len(vals)))+');'
 for i,ty in enumerate(args):
  if '*' in ty:body+='if(a'+str(i)+'!='+{'PlayerAvatar *':'avatar','LocalMapObject *':'object','FieldSystem *':'field'}[ty]+')errors++;'
 if ret!='void':
  if '*' in ret:body+='return '+{'LocalMapObject *':'object','FieldSystem *':'field'}[ret]+';'
  elif name=='sub_0205D01C':body+='return cfg[phase]['+str(ids[name])+']%3;'
  elif name=='sub_020611F4':body+='return (uint32_t)a0^1u;'
  elif name=='sub_0206234C':body+='return (uint32_t)a0*4u+a1;'
  elif name in ['sub_0205DAA8','sub_0205DBF4','sub_0205DB68','sub_0205DCA0','sub_0205DCFC']:body+='return cfg[phase]['+str(ids[name])+'][(unsigned)a2&3];' if False else 'return cfg[phase]['+str(ids[name])+'] & ((cfg[phase][63]>>((a2&3)*4))&15);'
  else:body+='return cfg[phase]['+str(ids[name])+'];'
 else:
  target={'PlayerAvatar_SetMoveState':'PlayerAvatar_GetMoveState','PlayerAvatar_SetUnk24':'PlayerAvatar_GetUnk24','PlayerAvatar_ClearFlag6':'PlayerAvatar_CheckFlag6','PlayerAvatar_SetForcedMovement':'PlayerAvatar_CheckForcedMovement','PlayerAvatar_SetFlag7':'PlayerAvatar_CheckFlag7','PlayerAvatar_ClearUnk24ClearFlag2':'PlayerAvatar_GetUnk24'}.get(name)
  if target:body+='cfg[phase]['+str(ids[target])+']=' + ('a1;' if len(args)>1 else '0;')
 defs.append(sig+'{'+body+'}')
(p/'leafs.inc').write_text('\n'.join(ps+defs)+'\n')
(p/'abi.c').write_text('#include <player_avatar.h>\n#include <map_object.h>\n#include <unk_02062108.h>\n#include <unk_0205FD20.h>\n#include <unk_0206D494.h>\n#include <stdint.h>\n'+'\n'.join(ps+['extern '+ret+' '+name+'('+','.join(args)+');' for name,(ret,args) in control.EXPORTS.items()])+'\n')
