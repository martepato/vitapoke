"""Rebuild the objects that inline g3imm.h (stack draw_msg_t) into opt-game-app's archive copies only."""
from pathlib import Path
import subprocess,re,sys,os
T=Path('@WORK@'); src=T/'native-audio-app'/'sdk-g3stack'; app=T/'native-audio-app'; ov=app/'overlays'
AR='@TOOLBIN@ar'; GCC='@TOOLBIN@gcc'
override='-I'+str(src/'include')
obj=src/'obj'; obj.mkdir(exist_ok=True)
# --- SDK (flags from native-sdk-probe/compile.py)
ns={'__file__':str(T/'native-sdk-probe/compile.py')};exec((T/'native-sdk-probe/compile.py').read_text().split('entries=[]')[0],ns)
sflags=[override]+ns['flags']+['-DOPT_CMDLIST_BORROW']  # gecom.c/sbc.c: per-op lists on the stack, borrowed by the renderer
patched={'libntr__libraries__gx__src__g3imm.c.o':src/'libntr-gx/g3imm.c','libntr__libraries__gx__src__g3_util.c.o':src/'libntr-gx/g3_util.c',
 'libntrsystem__libraries__g3d__src__gecom.c.o':src/'g3d/gecom.c','libntrsystem__libraries__g3d__src__sbc.c.o':src/'g3d/sbc.c'}
members=subprocess.check_output(['@TOOLBIN@nm','-A',str(app/'libsdk-filtered.a.base')],text=True,stderr=subprocess.DEVNULL)
sdkm=sorted(set(re.findall(r'^[^:]*:([^:]*):\s+U SIM_HandleG3Command$',members,re.M)))
built=[]
for m in sdkm:
    repo,rest=m.split('__',1); root=T/('native-graphics/libntr' if repo=='libntr' else 'native-probe/libntrsystem')
    s=patched.get(m) or root/rest[:-2].replace('__','/')
    assert Path(s).is_file(),s
    o=obj/m; q=subprocess.run([GCC]+sflags+['-c',str(s),'-o',str(o)],capture_output=True,text=True,cwd=Path(s).parent)
    if q.returncode: print(q.stderr[-2000:]); sys.exit(1)
    built.append(str(o))
subprocess.run(['/bin/cp','-f',str(app/'libsdk-filtered.a.base'),str(app/'libsdk-filtered.a')],check=True)
subprocess.run([AR,'r',str(app/'libsdk-filtered.a')]+built,check=True)
print('sdk rebuilt',len(built))
# --- game objects: reuse overlays/build.py build() with override include
code=(ov/'build.py').read_text().split('if __import__')[0]
code=code.replace("flags[1:1]=['-I'+str(p/'include')]","flags[1:1]=['-I'+str(p/'include'),%r]"%override)
code=code.replace("out=p/'objects'","out=p/'objects-g3stack'")
ns2={'__file__':str(ov/'build.py')};exec(code,ns2)
gm=subprocess.check_output(['@TOOLBIN@nm','-A',str(ov/'libplatinum-overlays.a.base')],text=True,stderr=subprocess.DEVNULL)
gm=sorted(set(re.findall(r'^[^:]*:([^:]*):\s+U SIM_HandleG3Command$',gm,re.M)))
inv={v[:-2].replace('/','__')+'.o':v for v in ns2['files']}
outs=[]
for m in gm:
    f,err=ns2['build'](inv[m])
    if err: print(f,err); sys.exit(1)
    outs.append(str(ov/'objects-g3stack'/m))
subprocess.run(['/bin/cp','-f',str(ov/'libplatinum-overlays.a.base'),str(ov/'libplatinum-overlays.a')],check=True)
subprocess.run([AR,'r',str(ov/'libplatinum-overlays.a')]+outs,check=True)
print('game rebuilt',len(outs))
