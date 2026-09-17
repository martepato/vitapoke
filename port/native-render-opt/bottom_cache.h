// Engine B never consumes3D. Snapshot all of its render inputs so identical
// bottom output can be reused even while input grace requests fresh feedback.
static struct {
 alignas(64) u8 bg[0x20000],obj[0x20000],palette[1024],oam[1024],ext[40960],regs[0x70];
 unsigned enabled;bool valid;
} bottomCache;
static bool SameWords(const void*aa,const void*bb,unsigned bytes){
 const u32*a=(const u32*)aa,*b=(const u32*)bb;
 // All backing/snapshot arrays and lengths here are aligned for32-bit accesses, with lengths divisible by16bytes.
 while(bytes){u32 d=(a[0]^b[0])|(a[1]^b[1])|(a[2]^b[2])|(a[3]^b[3]);if(d)return false;a+=4;b+=4;bytes-=16;}return true;
}
static bool BottomSame(unsigned power){
 if(!bottomCache.valid||bottomCache.enabled!=(power&512))return false;
 if(!(power&512))return true; // Disabled engine is always blank.
 if(!SameWords(bottomCache.regs,VitaNative_GfxRegisters+0x1000,0x70)||!SameWords(bottomCache.palette,GPU::Palette+1024,512))return false;
 unsigned disp=reg32(0x1000);bool objects=(disp&((1<<12)|(1<<15)))!=0;
 if(!SameWords(bottomCache.bg,s_HW_DB_BG_VRAM,0x20000))return false;
 if(objects&&(!SameWords(bottomCache.obj,s_HW_DB_OBJ_VRAM,0x20000)||!SameWords(bottomCache.palette+512,GPU::Palette+1536,512)||!SameWords(bottomCache.oam,GPU::OAM+1024,1024)))return false;
 if((disp&(1u<<30))&&!SameWords(bottomCache.ext,GPU::VRAMFlat_BBGExtPal,32768))return false;
 if(objects&&(disp&(1u<<31))&&!SameWords(bottomCache.ext+32768,GPU::VRAMFlat_BOBJExtPal,8192))return false;
 return true;
}
static void SaveBottom(unsigned power){
 memcpy(bottomCache.regs,VitaNative_GfxRegisters+0x1000,0x70);
 unsigned disp=reg32(0x1000);bool objects=(disp&((1<<12)|(1<<15)))!=0;
 memcpy(bottomCache.palette,GPU::Palette+1024,512);memcpy(bottomCache.bg,s_HW_DB_BG_VRAM,0x20000);
 if(objects){memcpy(bottomCache.palette+512,GPU::Palette+1536,512);memcpy(bottomCache.oam,GPU::OAM+1024,1024);memcpy(bottomCache.obj,s_HW_DB_OBJ_VRAM,0x20000);}
 if(disp&(1u<<30))memcpy(bottomCache.ext,GPU::VRAMFlat_BBGExtPal,32768);
 if(objects&&(disp&(1u<<31)))memcpy(bottomCache.ext+32768,GPU::VRAMFlat_BOBJExtPal,8192);
 bottomCache.enabled=power&512;bottomCache.valid=true;
}
static void FrameStart(GPU2D::Unit&u){
 u.VBlankEnd(); // Native2D uses one register snapshot, with no HBlank mutations.
 // Initialize wrapping windows as if the preceding blanking interval/scanline
 // had run. This makes same-register snapshots deterministic on first use too.
 u.Win0Active=(u.Win0Coords[2]>u.Win0Coords[3]?1:0)|(u.Win0Coords[0]>u.Win0Coords[1]?2:0);
 u.Win1Active=(u.Win1Coords[2]>u.Win1Coords[3]?1:0)|(u.Win1Coords[0]>u.Win1Coords[1]?2:0);
}
