.section .bss
.balign 64
.global VitaNative_PaletteRAM
.type VitaNative_PaletteRAM,@object
VitaNative_PaletteRAM:
.global s_HW_BG_PLTT
.type s_HW_BG_PLTT,@object
s_HW_BG_PLTT:
.space 512
.size s_HW_BG_PLTT,512
.global s_HW_OBJ_PLTT
.type s_HW_OBJ_PLTT,@object
s_HW_OBJ_PLTT:
.space 512
.size s_HW_OBJ_PLTT,512
.global s_HW_DB_BG_PLTT
.type s_HW_DB_BG_PLTT,@object
s_HW_DB_BG_PLTT:
.space 512
.size s_HW_DB_BG_PLTT,512
.global s_HW_DB_OBJ_PLTT
.type s_HW_DB_OBJ_PLTT,@object
s_HW_DB_OBJ_PLTT:
.space 512
.size s_HW_DB_OBJ_PLTT,512
.size VitaNative_PaletteRAM,2048
.balign 64
.global SDK_AUTOLOAD_DTCM_START
.type SDK_AUTOLOAD_DTCM_START,@object
SDK_AUTOLOAD_DTCM_START:
.space 16384
.size SDK_AUTOLOAD_DTCM_START,16384
