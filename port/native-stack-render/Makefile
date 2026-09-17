# GE 2D compositor library for the native app. Reuses the native-render-opt objects for
# everything except the frame/present code.
INCDIR = $(PSPDEV)/psp/include $(PSPDEV)/psp/sdk/include ../native-probe/backing/include ../native-graphics/alias-proof/include ../native-probe/generated ../native-graphics/libntr/include ../../melon ../native-render-opt
CFLAGS = -O2 @TARGETCC@ -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DMELONDS_PSP -DPSP_NATIVE_RGBA_OUTPUT=1 -ffunction-sections -fdata-sections -fno-strict-aliasing $(addprefix -I,$(INCDIR))
# Release optimisations (Platinum test builds 2-5, verified exact): content cache, prescan/fast decode,
# fixed-point fast paths, pipelined 2D/GE present, sampled [G3HW] card-log profile (DEV=1 only). Override OPT_REL to build without.
OPT_REL ?= -DOPT_PRESCAN -DOPT_FASTDECODE -DOPT_MATH_CLIP -DOPT_MATH_BOX -DOPT_MATH_SCALE -DOPT_MATH_SCALE_NOP -DOPT_MATH_MUL43 -DOPT_MATH_LAZYFIN -DOPT_GE_ASYNC
# DEV=1 adds the sampled [G3HW] 3D profile and DEV-only log lines.
DEV ?= 0
ifeq ($(DEV),1)
OPT_REL += -DPSP_NATIVE_G3_HWPROF -DPSP_NATIVE_DEV
endif
# Texture change detection from VRAM write generations (needs native-audio-app/vram_dirty.c and the
# write redirects applied by native-audio-app/opttex_redirect.py).
OPT_TEX ?= -DOPT_TEX_DIRTY
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -fpermissive $(OPT_REL) $(OPT_TEX) $(RENDER_EXTRA)
CXX = psp-g++
AR = psp-ar
all: libnative-render-gu2d.a
render-gu2d.o: render-gu2d.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
g3_backend.o: g3_backend.cpp texture_cache.h g3_vtx.h g3_listcache.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
frontend.o: frontend.cpp g3_vtx.h g3_listcache.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
libnative-render-gu2d.a: render-gu2d.o g3_backend.o frontend.o native_gpu.o GPU2D_Soft.o g3_handler.o
	$(AR) rcs $@ $^
clean:
	rm -f render-gu2d.o g3_backend.o frontend.o g3_handler.o libnative-render-gu2d.a

g3_handler.o: g3_handler.cpp g3_vtx.h g3_listcache.h
	$(CXX) $(CXXFLAGS) -O3 -c $< -o $@
