TARGET = native_soft2d_probe
OBJS = main.o graphics_registers.o g2.o native_gpu.o GPU2D_Soft.o
INCDIR = include ../libntr/include ../../native-probe/generated ../../../melon
CFLAGS = -O3 @TARGETCC@ -fno-tree-vectorize -fno-strict-aliasing -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DMELONDS_PSP $(EXTRA_CFLAGS)
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti
LIBS = -lstdc++ -lm
include @BUILDMAK@

g2.o: ../libntr/libraries/gx/src/g2.c
	$(CC) $(CFLAGS) $(addprefix -I,$(INCDIR)) -c $< -o $@
