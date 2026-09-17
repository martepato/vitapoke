TARGET = native-backup-probe
OBJS = main.o backup.o
INCDIR = include ../native-graphics/libntr/include ../native-probe/generated
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DSDK_VERSION_MAJOR=4 -ffunction-sections -fdata-sections -include ../native-graphics/libntr/include/pch/nitro_pch.h
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
