/* Capture window state in actual DS HBlank order for the native renderer. */
#include <nitro.h>
#include "system.h"
#include "native_window_rows.h"
extern unsigned char VitaNative_GfxRegisters[];
static void Snapshot(SSNativeWindowRow rows[2][192],unsigned line)
{
    for(unsigned e=0;e<2;e++){
        const volatile u16 *regs=(const volatile u16*)(VitaNative_GfxRegisters+e*0x1000);
        rows[e][line].enable=regs[0]&0xe000;
        for(unsigned i=0;i<6;i++)rows[e][line].reg[i]=regs[0x20+i];
    }
}
int SSNativeCaptureWindowRows(SSNativeWindowRow rows[2][192])
{
    if(!gSystem.hBlankIntr)return 0;
    if(!rows)return -1;
    u16 oldVCount=reg_GX_VCOUNT,oldHBlank=reg_GX_DISPSTAT&2;
    Snapshot(rows,0);
    /* HBlank changes affect the following visible row. IRQs also run during
     * VBlank; circle wipes intentionally reset their first-row shape there. */
    for(unsigned line=0;line<263;line++){
        reg_GX_VCOUNT=line;reg_GX_DISPSTAT|=2;
        if(gSystem.hBlankIntr)gSystem.hBlankIntr(gSystem.hBlankIntrArg);
        if(line<191)Snapshot(rows,line+1);
    }
    reg_GX_VCOUNT=oldVCount;
    reg_GX_DISPSTAT=(reg_GX_DISPSTAT&~2u)|oldHBlank;
    return 1;
}
