/* vitapoke: the one SceShaccCg declaration VitaSDK does not ship.
 *
 * vitaShaRK includes <shacccg_ext.h> for two functions, which turn the shader compiler's
 * non-standard extensions on and off. It comes from the official SDK's shacccg package; VitaSDK has
 * psp2/shacccg.h but not this, so vitaShaRK cannot be built without it. This is that header, and
 * shacccg_ext_stub.c is a definition for it -- because VitaSDK has no import stub for it either.
 *
 * Nothing in this port calls it: the renderer brings the compiler up with shark_init_simple, which
 * is vitaShaRK's own path for exactly this situation and does not ask for the extensions. It has to
 * exist because the code that does call it is in the same object file as the code that does not.
 */
#ifndef VITAPOKE_SHACCCG_EXT_H
#define VITAPOKE_SHACCCG_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

void sceShaccCgExtEnableExtensions(void);
void sceShaccCgExtDisableExtensions(void);

#ifdef __cplusplus
}
#endif
#endif /* VITAPOKE_SHACCCG_EXT_H */
