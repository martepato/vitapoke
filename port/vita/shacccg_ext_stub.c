/* vitapoke: a definition for the one SceShaccCg import VitaSDK has no stub for.
 *
 * See shacccg_ext.h. vitaShaRK's shark_init turns the compiler's extensions on and shark_end turns them off; this port
 * brings the compiler up with shark_init_simple instead, which does neither, so neither is reached.
 * They exist so that vitaShaRK links at all -- and the first says so in the log rather than silently
 * doing nothing, in case a future change starts taking the other path.
 */
extern void VitaNativeMemLog(const char *fmt, ...);

void sceShaccCgExtEnableExtensions(void)
{
	VitaNativeMemLog("[GPU] the shader compiler's extensions are not available in this build");
}

void sceShaccCgExtDisableExtensions(void) { }
