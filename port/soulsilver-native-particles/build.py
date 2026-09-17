from pathlib import Path
import subprocess,json,shutil,concurrent.futures
b=Path(__file__).resolve().parent;src=b.parent/'native-probe/pokeplatinum/lib/spl';core=b.parent/'soulsilver-native-core'
for d in ['include','src']:shutil.copytree(src/d,b/d,dirs_exist_ok=True)
# SS sets bits 2-4 of header byte 0x4c (reserved on Platinum). The decompiled
# engine never reads them, so they are logged once, not fatal (battle SPAs use 2).
p=b/'src/spl_manager.c'
t=p.read_text().replace('#include "spl_manager.h"', '#include "spl_manager.h"\n#include <stdio.h>\n#include <stdlib.h>')
t=t.replace('        offset += sizeof(SPLResourceHeader);', '        /* SS-only bits 2-4 of header byte 0x4c: padding in the decompiled SPL header, read by nothing in spl_draw/spl_emitter; intro SPA has 0, battle SPAs carry 2. Noted, never fatal. */\n        { unsigned ssPositionMode = (((const u8 *)res->header)[0x4c] >> 2) & 7; static unsigned noted; if (ssPositionMode && noted++ < 4) printf("[SS-PARTICLES] resource %d: header 0x4c bits 2-4 = %u (ignored)\\n", i, ssPositionMode); }\n        offset += sizeof(SPLResourceHeader);')
p.write_text(t)
# PSP divide-by-zero: ARM software division returns 0 for a zero divisor, the PSP's GCC build traps (break 7) and
# the game exits. Thunderbolt / Shadow Ball child particles hit 0xFFFF / (parent lifeTime / 2) with lifeTime 1.
def guard(path, pairs):
    q=b/'src'/path; u=q.read_text()
    for old,new,n in pairs:
        assert u.count(old)==n, (path, old, u.count(old))
        u=u.replace(old,new)
    q.write_text(u)
guard('spl_emit.c', [
 ('ptcl->loopTimeFactor = 0xFFFF / res->header->misc.loopFrames;', 'ptcl->loopTimeFactor = res->header->misc.loopFrames ? 0xFFFF / res->header->misc.loopFrames : 0;', 1),
 ('child->loopTimeFactor = 0xFFFF / (ptcl->lifeTime / 2);', 'child->loopTimeFactor = (ptcl->lifeTime / 2) ? 0xFFFF / (ptcl->lifeTime / 2) : 0;', 1),
 ('child->lifeTimeFactor = 0xFFFF / ptcl->lifeTime;', 'child->lifeTimeFactor = ptcl->lifeTime ? 0xFFFF / ptcl->lifeTime : 0;', 1),
])
guard('spl_emitter.c', [
 ('lifeRates[0] = (ptcl->age << 8) / ptcl->lifeTime;', 'lifeRates[0] = ptcl->lifeTime ? (ptcl->age << 8) / ptcl->lifeTime : 0;', 1),
])
# PSP modulo-by-zero: same trap as division; a zero interval keeps the left value, a zero frameCount picks texture 0.
guard('spl_behavior.c', [
 ('if ((ptcl->age % rng->applyInterval) == 0) {', 'if ((rng->applyInterval ? ptcl->age % rng->applyInterval : ptcl->age) == 0) {', 1),
])
guard('spl_emit.c', [
 ('res->texAnim->textures[SPLRandom_U32(12) % res->texAnim->param.frameCount];', 'res->texAnim->textures[res->texAnim->param.frameCount ? SPLRandom_U32(12) % res->texAnim->param.frameCount : 0];', 1),
])
guard('spl_emitter.c', [
 ('if (emtr->age % emtr->misc.emissionInterval == 0) {', 'if ((emtr->misc.emissionInterval ? emtr->age % emtr->misc.emissionInterval : emtr->age) == 0) {', 1),
 ('((diff >> FX32_SHIFT) % child->misc.emissionInterval == 0)', '((child->misc.emissionInterval ? (diff >> FX32_SHIFT) % child->misc.emissionInterval : (diff >> FX32_SHIFT)) == 0)', 1),
])
# PSP anim 255-out: Scale/Alpha fade-out divide by (255 - out); out == 255 with lifeRate 255 would trap. 0 -> value = end.
guard('spl_anim.c', [
 ('ptcl->animScale = end + (((lifeRate - 255) * (end - mid)) / (255 - out));', 'ptcl->animScale = end + ((255 - out) ? (((lifeRate - 255) * (end - mid)) / (255 - out)) : 0);', 1),
 ('value = ((lifeRate - 255) * (alphaAnim->alpha.end - alphaAnim->alpha.mid)) / (255 - out);', 'value = (255 - out) ? ((lifeRate - 255) * (alphaAnim->alpha.end - alphaAnim->alpha.mid)) / (255 - out) : 0;', 1),
])
# PSP NULL emitter: battle overlay 7 (ov07_022260F8, Shadow Ball) deletes an empty emitter slot; the DS tolerates the NULL
# read, native code faults at address 8.
guard('spl_manager.c', [
 ('void SPLManager_DeleteEmitter(SPLManager *mgr, SPLEmitter *emtr)\n{\n', 'void SPLManager_DeleteEmitter(SPLManager *mgr, SPLEmitter *emtr)\n{\n    if (emtr == NULL) {\n        return;\n    }\n', 1),
])
# Real implementation identities are preserved for SS behavior-pointer comparisons.
alias={'SPLManager_New':'SPL_Init','SPLManager_LoadResources':'SPL_Load','SPLManager_UploadTexturesEx':'SPL_LoadTexByCallbackFunction','SPLManager_UploadPalettesEx':'SPL_LoadTexPlttByCallbackFunction','SPLManager_UploadTextures':'SPL_LoadTexByVRAMManager','SPLManager_UploadPalettes':'SPL_LoadTexPlttByVRAMManager','SPLManager_Update':'SPL_Calc','SPLManager_Draw':'SPL_Draw','SPLManager_CreateEmitter':'SPL_Create','SPLManager_CreateEmitterWithCallback':'SPL_CreateWithInitialize','SPLManager_DeleteEmitter':'SPL_Delete','SPLManager_DeleteAllEmitters':'SPL_DeleteAll','SPLBehavior_ApplyGravity':'spl_calc_gravity','SPLBehavior_ApplyRandom':'spl_calc_random','SPLBehavior_ApplyMagnet':'spl_calc_magnet','SPLBehavior_ApplySpin':'spl_calc_spin','SPLBehavior_ApplyCollisionPlane':'spl_calc_scfield','SPLBehavior_ApplyConvergence':'spl_calc_convergence'}
(b/'aliases.json').write_text(json.dumps(alias,indent=2)+'\n')
cmd=json.loads((core/'compile-command.json').read_text())+['-I'+str(b/'include')]+['-D'+k+'='+v for k,v in alias.items()]
cmd.insert(1,'-I'+str(b/'include'))
(b/'compile-command.json').write_text(json.dumps(cmd)+'\n')
(b/'objects').mkdir(exist_ok=True)
def compile(p):
 out=b/'objects'/p.with_suffix('.o').name
 with out.with_suffix('.log').open('w') as f:r=subprocess.run(cmd+['-c',str(p),'-o',str(out)],stdout=f,stderr=f)
 return {'file':p.name,'ok':r.returncode==0}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(compile,(b/'src').glob('*.c')))
print(results);(b/'compile-results.json').write_text(json.dumps(results,indent=2)+'\n')
if not all(x['ok'] for x in results):raise SystemExit(1)
subprocess.run(['@TOOLBIN@ar','rcs',str(b/'libss-particles.a'),*[str(x) for x in (b/'objects').glob('*.o')]],check=True)
