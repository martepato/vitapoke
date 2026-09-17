// Exact engine A reuse only for normal 2D display without an enabled 3D BG0.
static struct {
 alignas(64) u8 bg[0x80000],obj[0x40000],palette[1024],oam[1024],ext[40960],regs[0x70];
 unsigned power;bool valid;
} topCache;
static bool TopEligible(unsigned disp){return ((disp>>16)&3)==1&&!(disp&(1<<3));}
static bool TopSame(unsigned power,unsigned disp){
 if(!topCache.valid||topCache.power!=power||!TopEligible(disp))return false;
 if(!SameWords(topCache.regs,VitaNative_GfxRegisters,0x70)||!SameWords(topCache.palette,GPU::Palette,512))return false;
 if(!SameWords(topCache.bg,s_HW_BG_VRAM,0x80000))return false;
 bool objects=(disp&((1<<12)|(1<<15)))!=0;
 if(objects&&(!SameWords(topCache.obj,s_HW_OBJ_VRAM,0x40000)||!SameWords(topCache.palette+512,GPU::Palette+512,512)||!SameWords(topCache.oam,GPU::OAM,1024)))return false;
 if((disp&(1u<<30))&&!SameWords(topCache.ext,GPU::VRAMFlat_ABGExtPal,32768))return false;
 if(objects&&(disp&(1u<<31))&&!SameWords(topCache.ext+32768,GPU::VRAMFlat_AOBJExtPal,8192))return false;
 return true;
}
static void SaveTop(unsigned power,unsigned disp){
 topCache.valid=false;if(!TopEligible(disp))return;
 memcpy(topCache.regs,VitaNative_GfxRegisters,0x70);memcpy(topCache.palette,GPU::Palette,512);memcpy(topCache.bg,s_HW_BG_VRAM,0x80000);
 bool objects=(disp&((1<<12)|(1<<15)))!=0;
 if(objects){memcpy(topCache.obj,s_HW_OBJ_VRAM,0x40000);memcpy(topCache.palette+512,GPU::Palette+512,512);memcpy(topCache.oam,GPU::OAM,1024);}
 if(disp&(1u<<30))memcpy(topCache.ext,GPU::VRAMFlat_ABGExtPal,32768);
 if(objects&&(disp&(1u<<31)))memcpy(topCache.ext+32768,GPU::VRAMFlat_AOBJExtPal,8192);
 topCache.power=power;topCache.valid=true;
}
