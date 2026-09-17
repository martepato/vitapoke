TARGET = native_render_probe
OBJS = probe.o render.o native_gpu.o GPU2D_Soft.o g3_backend.o g3_handler.o frontend.o graphics_registers.o native-backing.o
INCDIR = ../native-probe/backing/include ../native-graphics/alias-proof/include ../native-probe/generated ../native-graphics/libntr/include ../../melon
CFLAGS = -O3 @TARGETCC@ -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DMELONDS_PSP -DPSP_NATIVE_RGBA_OUTPUT=1 -ffunction-sections -fdata-sections -fno-strict-aliasing
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -fpermissive
LIBS = -lstdc++ -lm

LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
libnative-render.a: render.o native_gpu.o GPU2D_Soft.o g3_backend.o g3_handler.o frontend.o
	$(AR) rcs $@ $^
