# vitapoke: the Vita build's make fragment.
#
# This provides the conventions the component Makefiles in port/ are written against: the tool
# variables, INCDIR folded into CFLAGS, and pattern rules for C, C++ and assembly.
#
# Components set TARGET/OBJS/CFLAGS/INCDIR and include this last. Most of them exist only to produce
# object files for the app link, and `make <name>.o` is all the build asks of them.

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

# INCDIR is a list of directories; the component's own directory is always on the path.
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
