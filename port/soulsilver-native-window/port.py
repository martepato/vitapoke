from pathlib import Path
import re,json,subprocess,shutil
b=Path(__file__).resolve().parent;root=b.parents[1];core=b.parent/'soulsilver-native-core'
s=(b.parent/'native-probe/pokeplatinum/src/render_window.c').read_text()  # the two frame-drawing functions are identical upstream
parts=[]
for n in ['DrawStandardWindowFrame','DrawMessageBoxFrame']:
 m=re.search(r'static void '+n+r'\([^;]+?\n\{.*?\n\}',s,re.S);assert m
 parts.append(m[0].replace('Bg_FillTilemapRect','FillBgTilemapRect'))
s='''#include "render_window.h"
#include "gf_gfx_loader.h"
#include "text.h"
#include "render_text.h"
/* Frame tile geometry from public Platinum C, checked against SS ARM9 assembly.
 * Resource indices and SS scroll-arrow behavior below are SS-specific. */
'''+ '\n'.join(parts)+'''
void LoadUserFrameGfx1(BgConfig *b,GFBgLayer layer,u16 tile,u8 pal,u8 frame,enum HeapID heap) {
 GfGfxLoader_LoadCharData(38,frame?1:0,b,layer,tile,0,FALSE,heap);
 GfGfxLoader_GXLoadPal(38,frame==2?46:25,layer<4?0:4,pal*32,32,heap);
}
void LoadUserFrameGfx2(BgConfig *b,GFBgLayer layer,u16 tile,u8 pal,u8 frame,enum HeapID heap) {
 GfGfxLoader_LoadCharData(38,frame+2,b,layer,tile,0,FALSE,heap);
 GfGfxLoader_GXLoadPal(38,frame+26,layer<4?0:4,pal*32,32,heap);
}
void DrawFrameAndWindow1(Window *w,BOOL skip,u16 tile,u8 pal) {
 DrawStandardWindowFrame(w->bgConfig,GetWindowBgId(w),GetWindowX(w),GetWindowY(w),GetWindowWidth(w),GetWindowHeight(w),pal,tile);
 if(!skip)CopyWindowToVram(w);
}
void DrawFrameAndWindow2(Window *w,BOOL skip,u16 tile,u8 pal) {
 DrawMessageBoxFrame(w->bgConfig,GetWindowBgId(w),GetWindowX(w),GetWindowY(w),GetWindowWidth(w),GetWindowHeight(w),pal,tile);
 if(!skip)CopyWindowToVram(w);
 TextPrinter_SetDownArrowBaseTile(tile);
}
void sub_0200E5D4(Window *w,BOOL skip) {
 FillBgTilemapRect(w->bgConfig,GetWindowBgId(w),0,GetWindowX(w)-1,GetWindowY(w)-1,GetWindowWidth(w)+2,GetWindowHeight(w)+2,0);
 if(!skip)ClearWindowTilemapAndCopyToVram(w);
}
void ClearFrameAndWindow2(Window *w,BOOL skip) {
 FillBgTilemapRect(w->bgConfig,GetWindowBgId(w),0,GetWindowX(w)-2,GetWindowY(w)-1,GetWindowWidth(w)+5,GetWindowHeight(w)+2,0);
 if(!skip)ClearWindowTilemapAndCopyToVram(w);
}
'''
(b/'window.c').write_text(s)
cmd=json.loads((core/'compile-command.json').read_text())
with (b/'compile.log').open('w') as f:subprocess.run(cmd+['-Werror=implicit-function-declaration','-c',str(b/'window.c'),'-o',str(b/'window.o')],stdout=f,stderr=f,check=True)
subprocess.run(['@TOOLBIN@ar','rcs',str(b/'libss-window.a'),str(b/'window.o')],check=True)
for f in ['linkfile.prx','run_probe.py']:
 (b.parent/'soulsilver-native-sound-helpers'/f).exists() and shutil.copyfile(b.parent/'soulsilver-native-sound-helpers'/f,b/f)  # proof-only files, absent in pspoke
(b/'Makefile').write_text((b.parent/'soulsilver-native-sound-helpers/Makefile').read_text().replace('ss-sound-helpers','ss-window-proof').replace('main.o helpers.o table.o','main.o window.o').replace('SoulSilver sound helper proof','SoulSilver window frame proof'))
