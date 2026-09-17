#include <nitro.h>
#include <stdio.h>
#include <string.h>
/* The Vita has no Slot-2 bus and no ARM7 cartridge service. Publish the same
 * absent-cartridge state consumed by the unmodified SDK query functions. */
void __wrap_CTRDG_Init(void){
 static BOOL initialized;
 if(initialized)return;
 CTRDGModuleInfo *info=CTRDGi_GetModuleInfoAddr();
 memset(info,0,sizeof(*info));info->moduleID.raw=0xffff;
 *(vu8*)HW_IS_CTRDG_EXIST=0;
 *(vu8*)HW_SET_CTRDG_MODULE_INFO_ONCE=1;
 initialized=TRUE;
 printf("[SLOT2] no GBA cartridge slot on this console\n");
}
