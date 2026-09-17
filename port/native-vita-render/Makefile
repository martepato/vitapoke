# The Vita renderer: the DS's 2D engines composed in software, its 3D rasterised by the GPU, both
# put on the display through vitaGL.
#
# Four of the six objects are built from ../native-render-opt, unchanged: the DS 2D compositor
# (melonDS's), the DS memory and register bindings it reads, and libntr's geometry engine simulator.
# None of them knows which console it is on. What is specific to the Vita is render-vita.cpp (the
# frame and the display) and g3_backend.cpp (the rasteriser and the texture cache).
TARGET = native-render-vita
OBJS = render-vita.o g3_backend.o gpu.o native_gpu.o GPU2D_Soft.o g3_handler.o frontend.o
INCDIR = ../native-render-opt ../vita/include ../vita/sdl2-shim ../native-probe/backing/include \
         ../native-graphics/alias-proof/include ../native-probe/generated \
         ../native-graphics/libntr/include ../../melon
CFLAGS = -O2 @TARGETCC@ -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -D@SDKBUILD@ -DSDK_TS -DSDK_4M \
         -DSDK_FINALROM -DNNS_FINALROM -DMELONDS_PSP -DVITAPOKE_RGBA_OUTPUT=1 \
         -ffunction-sections -fdata-sections -fno-strict-aliasing
# -fpermissive: the melonDS and libntr simulator sources are C++ that a modern GCC is stricter about
# than the compiler they were written for. -fno-rtti and -fno-exceptions because nothing here uses
# either and they cost code size.
CXXFLAGS = $(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -fpermissive
LIBS = -lvitaGL -lmathneon -lstdc++ -lm
# CXXFLAGS is deliberately set before the include: @BUILDMAK@ folds INCDIR into it.
include @BUILDMAK@

# The build asks for the archive by name; vita.mak's default target is an executable, which this
# component is not.
lib$(TARGET).a: $(OBJS)
	$(AR) rcs $@ $^

# The DS 2D compositor and its memory bindings come from melonDS, whose types.h defines u8..s64 as
# the <stdint.h> types. libntr's nitro/types.h defines them too, and spells u32 `unsigned long` where
# melonDS spells it `uint32_t` -- the same size, a different type. That matters here and nowhere else
# in the port, because these are C++: the two spellings mangle differently, so a caller compiled
# against one cannot link against a function compiled against the other. On the PSP they agreed by
# accident, uint32_t being `unsigned long` there.
#
# nitro's win, because the SDK and the whole game are compiled against them. So these two files are
# compiled with melonDS's header closed off (-DTYPES_H is its include guard) and nitro's types put in
# ahead of it. melonDS's header is upstream and stays unmodified.
MELONTYPES = -DTYPES_H -include nitro/types.h

native_gpu.o: ../native-render-opt/native_gpu.cpp
	$(CXX) $(CXXFLAGS) $(MELONTYPES) -c $< -o $@
GPU2D_Soft.o: ../native-render-opt/GPU2D_Soft.cpp
	$(CXX) $(CXXFLAGS) $(MELONTYPES) -c $< -o $@
g3_handler.o: ../native-render-opt/g3_handler.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
frontend.o: ../native-render-opt/frontend.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

render-vita.o: render-vita.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
g3_backend.o: g3_backend.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

gpu.o: gpu.cpp gpu.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
