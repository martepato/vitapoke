from pathlib import Path
import re,json
b=Path(__file__).resolve().parent;root=b.parents[1]/'soulsilver-research/pokeheartgold-slop'
s=(b/'build.log').read_text();undefined=set(re.findall("undefined reference to `([^']+)'",s));asm={}
for p in list((root/'asm').rglob('*.s'))+list((root/'lib').rglob('*.s')):
 for f in re.findall(r'(?:thumb|arm)_func_start\s+(\w+)',p.read_text()):asm[f]=str(p.relative_to(root))
# Public SDK wrappers are generated as encrypted ARM entry points by dsprot/Makefile.
for suffix in ['Dummy','NotDummy','Emulator','NotEmulator','Flashcart','NotFlashcart']:
 asm['DSProt_Detect'+suffix]='lib/dsprot/Makefile (generated ARM wrapper)'
# Never turn unresolved data into an executable stand-in.
known=sorted(undefined & asm.keys());others=sorted(undefined-asm.keys())
(b/'missing-assembly-functions.json').write_text(json.dumps({n:asm[n] for n in known},indent=2))
(b/'unresolved-not-functions.json').write_text(json.dumps(others,indent=2))
(b/'missing.c').write_text('#include <stdio.h>\n#include <stdlib.h>\nextern void VitaNativeMemLog(const char *,...);__attribute__((noreturn)) static void Missing(const char *name){VitaNativeMemLog("[SS-MISSING] %s",name);fflush(stdout);abort();}\n'+''.join(f'__attribute__((noreturn)) void {n}(void){{Missing("{n}");}}\n' for n in known))
print('Strict abort traps for',len(known),'known original assembly functions; unknown unresolved',len(others));print('\n'.join(others))
