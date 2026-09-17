from pathlib import Path
import re,json,subprocess,concurrent.futures
p=Path(__file__).resolve().parent;b=p.parent.parent/'native-probe';r=b/'pokeplatinum'
ns={'__file__':str(b/'crossprobe-batch.py')};exec((b/'crossprobe-batch.py').read_text().split('previous=')[0],ns)
flags=ns['flags'];flags+=['-DPM_KEEP_ASSERTS','-DVITAPOKE_OFFLINE','-DVITAPOKE_MUTED'];flags[1:1]=['-I'+str(p/'include')]
files=re.findall(r"'([^']+\.c)'",(r/'src/meson.build').read_text())
# The PC port's debug GUI (Dear ImGui over SDL, needing a GL context): a cheat menu, map jump and
# item/monster editors. Not game code and never linked into a console build, so it is not compiled.
files=[f for f in files if not f.startswith(('port/gui_','port/sim_gui_'))]
lookup={f[:-2].replace('/','_'):f for f in files}
mods=json.loads((p/'modules.json').read_text());mapping={lookup[k]:m['id'] for m in mods for k in m['objects'] if k in lookup}
(p/'source-membership.json').write_text(json.dumps(mapping,indent=2))
out=p/'objects';out.mkdir(exist_ok=True);copies=p/'source';copies.mkdir(exist_ok=True)
(p/'include/nitro/sinit.h').write_text('''static void NitroStaticInit(void);
#define PSP_CAT_(a,b) a##b
#define PSP_CAT(a,b) PSP_CAT_(a,b)
__attribute__((used,section(".psp_sinit"))) void (*PSP_CAT(VitaNativeCtor_,VITAPOKE_OV_ID))(void)=NitroStaticInit;
''')
def build(f):
 src=r/'src'/f;s=src.read_text();i=mapping.get(f)
 if f=='game_overlay.c':
  s=s.replace('HW_ITCM_END','0x02000000u').replace('HW_ITCM_IMAGE','0x01ff8000u').replace('HW_DTCM_END','0x027e4000u').replace('HW_DTCM','0x027e0000u')
  s=s.replace('FS_LoadOverlayImageAsync(&info, &file);\n    FS_WaitAsync(&file);\n    FS_CloseFile(&file);','if (!FS_LoadOverlayImage(&info)) { return FALSE; }')
 if f=='main.c':
  s='extern void VitaNativeFrameComplete(void);\n'+s
  s=s.replace('SysTaskManager_ExecuteTasks(gSystem.postVBlankTaskMgr);','SysTaskManager_ExecuteTasks(gSystem.postVBlankTaskMgr);\n        VitaNativeFrameComplete();')
  network='''    if (sub_02038FFC(HEAP_ID_APPLICATION) == DWC_INIT_RESULT_DESTROY_OTHER_SETTING) {
        sub_02039A64(HEAP_ID_APPLICATION, 0);
    }'''
  assert network in s
  s=s.replace(network,'#ifndef VITAPOKE_OFFLINE\n'+network+'\n#endif')
 if f=='sound.c':
  for signature in ['BOOL Sound_StartReverb(int volume)','BOOL Sound_StartFilter(void)']:
   start=s.index('{',s.index(signature));depth=1;end=start+1
   while depth:
    if s[end]=='{':depth+=1
    if s[end]=='}':depth-=1
    end+=1
   s=s[:start+1]+'\n#ifdef VITAPOKE_MUTED\n    return FALSE; /* Optional capture effect is unavailable in muted PSP mode. */\n#else\n'+s[start+1:end-1]+'\n#endif\n'+s[end-1:]
 if f=='system.c':
  s='extern void VitaNativeInputPoll(void);\n'+s
  s=s.replace('void ReadKeypadAndTouchpad(void)\n{','void ReadKeypadAndTouchpad(void)\n{\n    VitaNativeInputPoll();')
 if f=='applications/poketch/poketch_system.c':
  s=s.replace('PoketchOverlayLoadFunctions[appID]();','/* The native overlay loader invokes the module constructor once. */')
 local=copies/(f.replace('/','__'));local.write_text(s)
 obj=out/(f[:-2].replace('/','__')+'.o')
 cmd=flags+['-I'+str(src.parent)]+(['-DVITAPOKE_OV_ID='+str(i)] if i is not None else [])+['-c',str(local),'-o',str(obj)]
 q=subprocess.run(cmd,cwd=r,capture_output=True,text=True);(out/(obj.stem+'.log')).write_text(q.stderr)
 if q.returncode:return f,q.stderr[-1600:]
 if i is not None:
  sec=subprocess.check_output(['@TOOLBIN@objdump','-h',str(obj)],text=True)
  sections=re.findall(r'^\s*\d+\s+(\S+)\s+[0-9a-f]+',sec,re.M);ren=[]
  for name in sections:
   kind='bss' if name.startswith(('.bss','.sbss')) else 'data' if name.startswith(('.data','.sdata')) else 'sinit' if name=='.psp_sinit' else None
   if kind:ren+=['--rename-section',name+f'=.nativeov.{i}.{kind}'+name]
  if ren:subprocess.run(['@TOOLBIN@objcopy',*ren,str(obj)],check=True)
 return f,None
if __import__('os').environ.get('RETRY_FAILED'):
 files=[x[0] for x in json.loads((p/'build-results.json').read_text())]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:res=list(pool.map(build,files))
errors=[x for x in res if x[1]];(p/'build-results.json').write_text(json.dumps(errors,indent=2));print('compiled',len(res)-len(errors),'/',len(res),flush=True)
if errors:print(errors[:3]);raise SystemExit(1)
a=p/'libplatinum-overlays.a';a.unlink(missing_ok=True);subprocess.run(['@TOOLBIN@ar','rcs',str(a),*[str(x) for x in out.glob('*.o')]],check=True)
