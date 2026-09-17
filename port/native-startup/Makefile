TARGET = native-startup
OBJS = os_vram.o storage.o main.o romfs.o boot.o platform.o system.o heap.o math_util.o sys_task_manager.o graphics_registers.o gx.o gx_vramcnt.o gxstate.o
INCDIR = include ../native-graphics/libntr/libraries/gx/include ../native-graphics/alias-proof/include ../native-probe/libntrdwc/include ../native-probe/libntrsystem/include ../native-probe/generated ../native-probe/pokeplatinum/include ../native-graphics/libntr/include
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -include ../native-probe/pokeplatinum/include/pch/global_pch.h -ffunction-sections -fdata-sections
LIBS = libnative-filtered.a
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
boot.o: ../native-probe/pokeplatinum/src/boot.c
	$(CC) $(CFLAGS) -c $< -o $@

narc.o: ../native-probe/pokeplatinum/src/narc.c
	$(CC) $(CFLAGS) -c $< -o $@

system.o: ../native-probe/pokeplatinum/src/system.c
	$(CC) $(CFLAGS) -c $< -o $@

heap.o: ../native-probe/pokeplatinum/src/heap.c
	$(CC) $(CFLAGS) -c $< -o $@

math_util.o: ../native-probe/pokeplatinum/src/math_util.c
	$(CC) $(CFLAGS) -c $< -o $@

sys_task_manager.o: ../native-probe/pokeplatinum/src/sys_task_manager.c
	$(CC) $(CFLAGS) -c $< -o $@

gx.o: ../native-graphics/libntr/libraries/gx/src/gx.c
	$(CC) $(CFLAGS) -c $< -o $@

gx_vramcnt.o: ../native-graphics/libntr/libraries/gx/src/gx_vramcnt.c
	$(CC) $(CFLAGS) -c $< -o $@

gxstate.o: ../native-graphics/libntr/libraries/gx/src/gxstate.c
	$(CC) $(CFLAGS) -c $< -o $@

romfs.o: ../native-romfs/romfs.c
	$(CC) $(CFLAGS) -c $< -o $@

graphics_registers.o: ../native-graphics/alias-proof/graphics_registers.S
	$(CC) -c $< -o $@

storage.o: storage.S
	$(CC) -c $< -o $@

os_vram.o: ../native-graphics/libntr/libraries/os/src/os_vramExclusive.c
	$(CC) $(CFLAGS) -c $< -o $@
