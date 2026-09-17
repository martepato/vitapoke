TARGET = native-memory-probe
OBJS = main.o mi_memory.o mi_dma.o
INCDIR = ../native-graphics/libntr/libraries/mi/src ../native-startup/include ../native-graphics/alias-proof/include ../native-graphics/libntr/include ../native-probe/generated
CFLAGS = -DSDL_MAIN_HANDLED -O2 @TARGETCC@ -std=gnu99 -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DSDK_VERSION_MAJOR=4 -ffunction-sections -fdata-sections -include ../native-graphics/libntr/include/pch/nitro_pch.h
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
