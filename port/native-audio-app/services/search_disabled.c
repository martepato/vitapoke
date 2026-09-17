#include <nitro.h>
#include <stdio.h>
#include "savedata.h"
/* Passive main-menu discovery is unavailable: DS wireless is not ported. Keep
 * CommSys uninitialized, report no peers, and let normal null-safe cleanup run. */
void __wrap_CommManager_InitializeSearchParty(SaveData*save){(void)save;puts("[WIRELESS] passive discovery disabled: DS wireless is not ported");}
u8 __wrap_CommManager_GetAvailableConnections(void){return 0;}
