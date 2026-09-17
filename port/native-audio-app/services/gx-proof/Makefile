TARGET = gx-dma-proof
OBJS = probe.o gx_dma.o backing.o graphics_registers.o
INCDIR = ../../../native-probe/backing/include ../../../native-graphics/alias-proof/include ../../../native-probe/generated ../../../native-graphics/libntr/include ../../../../../../melon ../../../native-render-opt
CFLAGS = -O3 @TARGETCC@ -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DMELONDS_PSP -DPSP_NATIVE_RGBA_OUTPUT=1 -ffunction-sections -fdata-sections -fno-strict-aliasing
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -fpermissive
LIBS = ../../../native-render-opt/libnative-render-convert-v1.a -lpsppower -lpspgu -lpspge -lstdc++ -lm
BUILD_PRX = 1
PSP_LARGE_MEMORY = 1
LDFLAGS = -Wl,--gc-sections
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = GX DMA proof
include @BUILDMAK@
gx_dma.o: ../gx_dma.c
	$(CC) $(CFLAGS) -c $< -o $@
backing.o: ../../../native-probe/backing/native-backing.o
	cp $< $@
graphics_registers.o: ../../../native-graphics/alias-proof/graphics_registers.o
	cp $< $@
