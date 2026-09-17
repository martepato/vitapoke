TARGET = native-core-proof
OBJS = main.o math_util.o trainer_ai.o sys_task_manager.o
INCDIR = ../native-probe/libntrsystem/include ../native-graphics/libntr/include ../native-probe/pokeplatinum/include
CFLAGS = -DPM_KEEP_ASSERTS -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -O2 @TARGETCC@ -std=gnu99 -ffunction-sections -fdata-sections
LIBS = ../native-sdk-probe/libnative-sdk-probe.a
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
