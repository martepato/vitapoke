from pathlib import Path
import re,subprocess,os
h=Path(__file__).resolve().parent;p=h/'dispatch-proof';p.mkdir(exist_ok=True);r=h/'title-render-profile'
env=os.environ.copy();env['PSPDEV']=str(Path.home()/'pspdev');env['PATH']=env['PSPDEV']+'/bin:'+env['PATH']
# Derive exactly the renderer include/compile options, with no source changes.
line=subprocess.check_output(['make','-n','-B','frontend.o'],cwd=r,env=env,text=True).strip().splitlines()[-1]
base=line.split();base=base[:base.index('-c')]
for kind,file in [('original','frontend.original.cpp'),('candidate','frontend.cpp')]:
 defs=['-D'+n+'='+kind+'_'+n for n in ['PSPNativeG3CommandsBorrowed','PSPNativeG3GetProfile','SIM_HandleG3Command','ModelFrontendPolygons','s_numG3DrawsThisFrame','g3ProfUs','g3ProfCalls']]
 subprocess.run(base+defs+['-c',str(r/file),'-o',str(p/(kind+'.o'))],cwd=r,env=env,check=True)
header=(h.parent/'native-graphics/libntr/include/simulator/g3_handler.h').read_text()
needed=subprocess.check_output([env['PSPDEV']+'/bin/psp-nm','-u',str(p/'candidate.o')],text=True)
names=set(re.findall(r'U (G3SIM_\w+)',needed));names.discard('G3SIM_VtxAssemble');names.discard('G3SIM_FlushArray')
funcs=[]
for i,name in enumerate(sorted(names)):
 match=re.search(r'void '+name+r'\((.*?)\);',header);args=match.group(1).strip();decl='void '+name+'('+args+')';vals=[]
 if args:
  for arg in args.split(','):
   arg=arg.strip();an=re.search(r'(\w+)$',arg).group(1)
   if '*' in arg:
    count={'G3SIM_MtxLoad43':12,'G3SIM_MtxMult43':12,'G3SIM_MtxLoad44':16,'G3SIM_MtxMult44':16,'G3SIM_MtxMult33':9,'G3SIM_MtxScale':3,'G3SIM_MtxTranslate':3}[name]
    vals += [f'record(((u32*){an})[{j}]);' for j in range(count)]
   else:vals.append(f'record((u32){an});')
 funcs.append(decl+'{record('+str(i+1)+');'+''.join(vals)+'}')
(p/'capture.inc').write_text('\n'.join(funcs)+'\nextern "C" void G3SIM_FlushArray(void){record(100);}\nextern "C" void G3SIM_VtxAssemble(s16 x,s16 y,s16 z){record(101);record((u32)(s32)x);record((u32)(s32)y);record((u32)(s32)z);}\n')
(p/'Makefile').write_text('''TARGET = ss-dispatch-proof
OBJS = main.o original.o candidate.o
CXXFLAGS = '''+' '.join(base[1:])+'''
LIBS = -lstdc++
LDFLAGS = -Wl,--gc-sections
BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SoulSilver packed G3 dispatch proof
include @BUILDMAK@
''')
