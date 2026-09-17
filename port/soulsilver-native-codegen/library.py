"""Build typed, selected native exports; broader scanner candidates stay separate."""
from pathlib import Path
import re,json,os,subprocess
import translate as t
import communications as comm
import sound_scene as sound
import player_direction as direction
import player_control as control
H=t.HERE
data_maps={Path(r['file']).name:r.get('symbols',{}) for r in json.loads((H.parent/'soulsilver-native-data/inventory.json').read_text())}
model=(t.SRC/'model_attributes.s').read_text()
selected=[(n,b,'model_attributes.s') for n,b in re.findall(r'thumb_func_start (\w+)\s*\n(.*?)thumb_func_end \w+',model,re.S) if n!='ov01_021EAAB8']
extra={'unk_02037C94.s':['sub_0203A01C'],'unk_020210A0.s':['GF_TouchpadInit','GF_TouchpadStartAutoSampling','GF_TouchpadStopAutoSampling','GF_TouchpadPauseOnLidClose','GF_TouchpadResumeOnLidOpen'],'unk_02004A44.s':['sub_020059D8','sub_02005D00','GF_SndSetMonoFlag'],'unk_0200FA24.s':['IsPaletteFadeFinished'],'unk_0205FD20.s':['GetDeltaXByFacingDirection','GetDeltaYByFacingDirection'],'unk_02026DE0.s':['sub_02026DE0','sub_02026E18']}
for group in [comm,sound,direction,control]:
 for file,names in group.SELECTION.items():extra.setdefault(file,[]).extend(names)
for file,names in extra.items():
 if file=='unk_02004A44.s':
  cpp=Path('@TOOLBIN@gcc')
  text=subprocess.check_output([str(cpp),'-E','-P','-x','assembler-with-cpp','-I'+str(t.SRC.parent/'include'),str(t.SRC/file)],text=True)
  (H/'sound-preprocessed.s').write_text(text)
 else:text=(t.SRC/file).read_text()
 for n in names:selected.append((n,re.search(r'thumb_func_start '+n+r'\s*\n(.*?)thumb_func_end '+n,text,re.S).group(1),file))
protos=['typedef struct UnkStruct_021D2230 UnkStruct_021D2230;','typedef struct DWCstAccUserData DWCAccUserData;','extern void *sub_0202C08C(UnkStruct_021D2230*);','extern int DWC_CheckUserData(const DWCAccUserData*);','extern void DWC_CreateUserData(DWCAccUserData*,uint32_t);','extern void DWC_ClearDirtyFlag(DWCAccUserData*);','extern void Heap_FreeExplicit(int,void*);']
t.CALLS.update({
 'sub_0202C08C':('r0=(uint32_t)(uintptr_t)sub_0202C08C((UnkStruct_021D2230*)(uintptr_t)r0);',1),
 'DWC_CheckUserData':('r0=DWC_CheckUserData((const DWCAccUserData*)(uintptr_t)r0);',1),
 'DWC_CreateUserData':('DWC_CreateUserData((DWCAccUserData*)(uintptr_t)r0,r1);',2),
 'DWC_ClearDirtyFlag':('DWC_ClearDirtyFlag((DWCAccUserData*)(uintptr_t)r0);',1),
})
protos.append('extern void NNS_SndSetMonoFlag(int);')
t.CALLS['NNS_SndSetMonoFlag']=('NNS_SndSetMonoFlag((int)r0);',1)
protos.extend(['extern void TP_RequestAutoSamplingStartAsync(uint16_t,uint16_t,void*,uint16_t);','extern void TP_RequestAutoSamplingStopAsync(void);','extern void TP_WaitBusy(int);','extern uint32_t TP_CheckBusy(int);','extern void GF_AssertFail(void);'])
t.CALLS.update({
 'TP_RequestAutoSamplingStartAsync':('TP_RequestAutoSamplingStartAsync((uint16_t)r0,(uint16_t)r1,(void*)(uintptr_t)r2,(uint16_t)r3);',4),
 'TP_RequestAutoSamplingStopAsync':('TP_RequestAutoSamplingStopAsync();',0),
 'TP_WaitBusy':('TP_WaitBusy((int)r0);',1),
 'TP_CheckBusy':('r0=TP_CheckBusy((int)r0);',1),
 'GF_AssertFail':('GF_AssertFail();',0),
 'GF_TouchpadStartAutoSampling':('r0=native_GF_TouchpadStartAutoSampling(r0,0,0,0);',1),
 'GF_TouchpadStopAutoSampling':('r0=native_GF_TouchpadStopAutoSampling(0,0,0,0);',0),
})
t.CALLS['Heap_FreeExplicit']=('Heap_FreeExplicit((int)r0,(void*)(uintptr_t)r1);',2)
sdk={'NNS_G3dGlbLightVector':['int','int16_t','int16_t','int16_t'],'NNS_G3dGlbLightColor':['int','uint16_t'],'NNS_G3dGlbMaterialColorDiffAmb':['uint16_t','uint16_t','int'],'NNS_G3dGlbMaterialColorSpecEmi':['uint16_t','uint16_t','int'],'NNS_G3dGlbPolygonAttr':['int']*6}
for suffix,typ in [('Diff','uint16_t'),('Amb','uint16_t'),('Spec','uint16_t'),('Emi','uint16_t'),('LightEnableFlag','int'),('PolygonMode','int'),('CullMode','int'),('PolygonID','int'),('Alpha','int'),('FogEnableFlag','int'),('DepthTestCond','int'),('1Dot','int'),('FarClip','int'),('XLDepthUpdate','int')]:sdk['NNS_G3dMdlSetMdl'+suffix+'All']=['void*',typ]
for name,types in sdk.items():
 protos.append(f'extern void {name}('+','.join(types)+');')
 args=[f'({ty})'+('(uintptr_t)' if '*' in ty else '')+(f'r{i}' if i<4 else f'ld32(sp+{4*(i-4)})') for i,ty in enumerate(types)]
 t.CALLS[name]=(name+'('+','.join(args)+');',len(types))
protos.extend(comm.configure(t))
protos.extend(sound.configure(t))
protos.extend(direction.configure(t))
protos.extend(control.configure(t))
protos.extend('uint32_t native_'+n+'(uint32_t,uint32_t,uint32_t,uint32_t);' for n,b,f in selected)
t.CALLS['ModelAttributes_ApplyGlobal']=('native_ModelAttributes_ApplyGlobal(r0,r1,0,0);',2)
generated_parts={}
code=[t.prefix,'\n'.join(protos)]
manifest=[]
for n,b,file in selected:
 emitted,ops=t.emit(n,b,data_maps.get(file,{}));code.append(emitted);generated_parts[n]=emitted;manifest.append(dict(name=n,file=file,instructions=len(ops)))
exports={
'ModelAttributes_Init':('ModelAttributes *',[]),
'ModelAttributes_Free':('void',['ModelAttributes **']),
'ModelAttributes_ApplyGlobal':('void',['ModelAttributes *','int']),
'ov01_021EA9B0':('void',['ModelAttributes *','NNSG3dResMdl *','uint32_t']),
'ModelAttributes_SetPolygonMode':('void',['ModelAttributes *','int','int']),
'ModelAttributes_SetCullMode':('void',['ModelAttributes *','int','int']),
'ModelAttributes_SetAlpha':('void',['ModelAttributes *','int','int']),
'ModelAttributes_SetMiscAttrEnabled':('void',['ModelAttributes *','int','int','int']),
'sub_0203A01C':('void',['UnkStruct_021D2230 *']),'GF_TouchpadInit':('void',[]),'GF_TouchpadPauseOnLidClose':('void',[]),'GF_TouchpadResumeOnLidOpen':('void',[]),'GF_SndSetMonoFlag':('void',['int']),'sub_020059D8':('int8_t *',[]),'sub_02005D00':('void',[]),'IsPaletteFadeFinished':('int',[]),
'GetDeltaXByFacingDirection':('int',['int']),'GetDeltaYByFacingDirection':('int',['int'])}
exports.update(comm.EXPORTS)
exports.update(sound.EXPORTS)
exports.update(direction.EXPORTS)
exports.update(control.EXPORTS)
header=['typedef struct LocalMapObject LocalMapObject;','typedef struct PlayerAvatar PlayerAvatar;','typedef struct UnkStruct_021D2230 UnkStruct_021D2230;','#include <stdint.h>','typedef struct ModelAttributes ModelAttributes;','typedef struct NNSG3dResMdl_ NNSG3dResMdl;']
export_wrappers={}
wrappers=['#include "library.h"']
for name,(ret,types) in exports.items():
 sig=ret+' '+name+'('+(','.join(ty+' a'+str(i) for i,ty in enumerate(types)) or 'void')+')'
 header.append(sig+';')
 args=[f'(uint32_t)'+('(uintptr_t)' if '*' in ty else '')+f'a{i}' for i,ty in enumerate(types)]+['0']*(4-len(types))
 expr='native_'+name+'('+','.join(args)+')'
 wrapper=sig+'{'+(expr+';' if ret=='void' else 'return ('+ret+')'+('(uintptr_t)' if '*' in ret else '')+expr+';')+'}'
 wrappers.append(wrapper);export_wrappers[name]=wrapper
(H/'data-map.h').write_text('\n'.join('#define '+old+' '+new for file in extra for old,new in data_maps.get(file,{}).items() if old!=new)+'\n')
(H/'library.h').write_text('\n'.join(header)+'\n')
(H/'library.c').write_text('\n\n'.join(code+wrappers)+'\n')
(H/'library-manifest.json').write_text(json.dumps(dict(implementations=manifest,exports=exports,sdk_calls=sdk,manual_exports={'MIi_CpuClearFast':['void','uint32_t','void *','uint32_t'],'sub_02034044':['int','int'],'GF_SndSetAllocatableChannelForBGMPlayer':['void','uint32_t'],'sub_02005910':['void','int'],'sub_02005908':['int']}),indent=2)+'\n')
cc=Path('@TOOLBIN@gcc');ar=Path('@TOOLBIN@ar')
subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(H/'library.c'),'-o',str(H/'library.o')],check=True)
objdir=H/'library-objects';objdir.mkdir(exist_ok=True);objects=[]
for name,part in generated_parts.items():
 source=objdir/(name+'.c');obj=objdir/(name+'.o')
 source.write_text(t.prefix+'\n'+'\n'.join(protos)+'\n'+part+'\n#include "../library.h"\n'+export_wrappers.get(name,'')+'\n')
 subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(source),'-o',str(obj)],check=True);objects.append(str(obj))
tail_obj=objdir/'sound_tailcalls.o'
subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(H/'sound_tailcalls.c'),'-o',str(tail_obj)],check=True);objects.append(str(tail_obj))
mode_obj=objdir/'sub_02034044.o'
subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(H/'communication_mode.c'),'-o',str(mode_obj)],check=True);objects.append(str(mode_obj))
obj=objdir/'MIi_CpuClearFast.o'
subprocess.run([str(cc),'-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-c',str(H/'mi_clear_fast.c'),'-o',str(obj)],check=True);objects.append(str(obj))
archive=H/'libss-native-translated.new.a'
if archive.exists():archive.unlink()
subprocess.run([str(ar),'rcs',str(archive)]+objects,check=True)
archive.replace(H/'libss-native-translated.a')
print(f'Built {len(selected)} translated implementations/{len(exports)} typed exports plus five manual native exports; other inventory candidates excluded.')
