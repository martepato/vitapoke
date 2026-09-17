# vitapoke: the Vita build's make fragment.
#
# VitaSDK has no equivalent of PSPSDK's lib/build.mak, so this provides the same conventions the
# component Makefiles in port/ are written against: the tool variables, INCDIR folded into CFLAGS, and
# pattern rules for C, C++ and assembly. Component Makefiles set TARGET/OBJS/CFLAGS/INCDIR and include
# this last, exactly as they do for the PSP.
#
# What it deliberately does not reproduce:
#   - EBOOT.PBP and BUILD_PRX. A Vita executable is built by a different chain (see the vpk rule
#     below), and the components that name EBOOT.PBP are standalone PSP probes; the real build only
#     ever asks them for object files.
#   - PSP_LARGE_MEMORY, which has no meaning on a console with 512 MB.
#
# The PSP toolchain is never referenced. PSPSDK is defined only so that the `$(subst
# $(PSPSDK)/lib/linkfile.prx,...)` lines a few Makefiles run after this include stay harmless: the
# pattern cannot occur, so the substitution does nothing.
PSPSDK = /nonexistent-on-vita

PREFIX  = @TOOLBIN@
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
AS      = $(PREFIX)gcc
LD      = $(PREFIX)gcc
AR      = $(PREFIX)ar
RANLIB  = $(PREFIX)ranlib
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
STRIP   = $(PREFIX)strip

# VitaSDK's own tools, which have no prefix.
VITA_ELF_CREATE = $(dir $(PREFIX))vita-elf-create
VITA_MAKE_FSELF = $(dir $(PREFIX))vita-make-fself
VITA_MKSFOEX    = $(dir $(PREFIX))vita-mksfoex
VITA_PACK_VPK   = $(dir $(PREFIX))vita-pack-vpk

# The same convention as PSPSDK: INCDIR is a list of directories, and the component's own directory is
# always on the path.
INCDIR := $(INCDIR) .
CFLAGS := $(addprefix -I,$(INCDIR)) $(CFLAGS)
CXXFLAGS := $(addprefix -I,$(INCDIR)) $(CXXFLAGS)
ASFLAGS := $(CFLAGS) $(ASFLAGS)

# -Wl,-q keeps the relocations vita-elf-create needs to turn an ELF into a Vita module.
LDFLAGS := -Wl,-q $(LDFLAGS)

all: $(TARGET).elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

# eboot.bin is what a Vita application boots from; the VPK is that plus the metadata the console's
# installer wants. TITLE_ID and TITLE are set by the component, or defaulted here.
TITLE_ID ?= VPOK00001
TITLE ?= $(TARGET)

eboot.bin: $(TARGET).elf
	$(VITA_ELF_CREATE) $< $(TARGET).velf
	$(VITA_MAKE_FSELF) -q $(TARGET).velf $@

param.sfo:
	$(VITA_MKSFOEX) -s TITLE_ID=$(TITLE_ID) "$(TITLE)" $@

$(TARGET).vpk: eboot.bin param.sfo
	$(VITA_PACK_VPK) -s param.sfo -b eboot.bin $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).velf $(TARGET).vpk eboot.bin param.sfo

.PHONY: all clean
