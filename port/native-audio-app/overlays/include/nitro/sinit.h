static void NitroStaticInit(void);
#define PSP_CAT_(a,b) a##b
#define PSP_CAT(a,b) PSP_CAT_(a,b)
__attribute__((used,section(".psp_sinit"))) void (*PSP_CAT(VitaNativeCtor_,VITAPOKE_OV_ID))(void)=NitroStaticInit;
