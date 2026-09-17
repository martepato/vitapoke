TARGET = native-sdl-thread
OBJS = main.o sdl_threads.o
INCDIR = ../native-probe/generated ../native-graphics/libntr/include
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_VERSION_MAJOR=4 -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -ffunction-sections -fdata-sections
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
