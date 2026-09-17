static unsigned errors=0,checks=0;
static void check(bool ok,const char*name){++checks;if(!ok){++errors;if(errors<10)printf("[ALIAS] FAIL %s\n",name);}}
static u32 hashA(){u32 h=2166136261u;for(unsigned i=0;i<256*192;i++)h=(h^out[0][i])*16777619u;return h;}
static void aliasTest(){
 for(unsigned i=0;i<1024;i++){
  unsigned x=(i*37)&511,y=(i*73)&511;
  G2_SetBG0Offset(x,y);G2S_SetBG0Offset(y,x);
  check(s_reg_G2_BG0HOFS==x&&s_reg_G2_BG0VOFS==y,"main packed to halves");
  check(s_reg_G2S_DB_BG0HOFS==y&&s_reg_G2S_DB_BG0VOFS==x,"sub packed to halves");
  s_reg_G2_BG0HOFS=y;s_reg_G2_BG0VOFS=x;
  s_reg_G2S_DB_BG0HOFS=x;s_reg_G2S_DB_BG0VOFS=y;
  check(s_reg_G2_BG0OFS==(y|(x<<16)),"main halves to packed");
  check(s_reg_G2S_DB_BG0OFS==(x|(y<<16)),"sub halves to packed");
 }
 MtxFx22 matrix={4096,512,-256,8192};
 G2_SetBG2Affine(&matrix,2,3,10,12);
 check(s_reg_G2_BG2PA==256&&s_reg_G2_BG2PB==32&&s_reg_G2_BG2PC==(u16)-16&&s_reg_G2_BG2PD==512,"affine matrix contiguous");
 check(s_reg_G2_BG2X==2848&&s_reg_G2_BG2Y==5248,"affine origin contiguous");
 G2S_SetBG2Affine(&matrix,2,3,10,12);
 check(s_reg_G2S_DB_BG2X==2848&&s_reg_G2S_DB_BG2Y==5248,"sub affine origin");
 G2_SetBlendAlpha(1,2,7,9);
 check(s_reg_G2_BLDCNT==(0x40|1|(2<<8))&&s_reg_G2_BLDALPHA==(7|(9<<8)),"blend contiguous write");
 G2_BlendNone();G2_SetBGMosaicSize(2,3);G2_SetOBJMosaicSize(4,5);
 check(s_reg_G2_MOSAIC==0x5432,"mosaic byte address arithmetic");
 s_reg_G2_MOSAIC=0;
 // Real renderer: packed and separate scrolling must produce identical pixels.
 scene();render();u32 original=hashA();
 G2_SetBG0Offset(11,7);render();u32 packed=hashA();
 s_reg_G2_BG0HOFS=11;s_reg_G2_BG0VOFS=7;render();
 check(hashA()==packed&&packed!=original,"rendered packed and halfword scroll");
 // 8bpp affine tile with asymmetric rows; actual SDK affine setter vs halves.
 memset(s_HW_BG_VRAM,0,sizeof(s_HW_BG_VRAM));
 for(int y=0;y<8;y++)for(int x=0;x<8;x++)s_HW_BG_VRAM[64+y*8+x]=(x<3&&y<5)?1:2;
 memset(s_HW_BG_VRAM+0x2000,1,16*16);
 s_reg_GX_DISPCNT=(1<<16)|(1<<10)|2;s_reg_G2_BG2CNT=4<<8;
 MtxFx22 identity={4096,0,0,4096};
 G2_SetBG2Affine(&identity,0,0,0,0);render();original=hashA();
 G2_SetBG2Affine(&identity,0,0,4,2);render();packed=hashA();
 s_reg_G2_BG2PA=256;s_reg_G2_BG2PB=0;s_reg_G2_BG2PC=0;s_reg_G2_BG2PD=256;
 s_reg_G2_BG2X=1024;s_reg_G2_BG2Y=512;render();
 check(hashA()==packed&&packed!=original,"rendered affine SDK and scalar writes");
 printf("[ALIAS] checks=%u failures=%u scroll_crc=%08lx affine_crc=%08lx\n",checks,errors,(unsigned long)original,(unsigned long)packed);
 memset(VitaNative_GfxRegisters,0,4224);memset(s_HW_BG_VRAM,0,sizeof(s_HW_BG_VRAM));
 memset(s_HW_DB_BG_VRAM,0,sizeof(s_HW_DB_BG_VRAM));
}
