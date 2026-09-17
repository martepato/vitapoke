TARGET = native-romfs
OBJS = main.o romfs.o boot.o stubs.o narc.o
INCDIR = ../native-probe/libntrsystem/include ../native-probe/generated ../native-probe/pokeplatinum/include ../native-graphics/libntr/include
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -include ../native-probe/pokeplatinum/include/pch/global_pch.h -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
boot.o: ../native-probe/pokeplatinum/src/boot.c
	$(CC) $(CFLAGS) -c $< -o $@

narc.o: ../native-probe/pokeplatinum/src/narc.c
	$(CC) $(CFLAGS) -c $< -o $@
