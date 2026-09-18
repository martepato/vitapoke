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
# Assembly gets the include paths, the defines and the CPU flags, and nothing else: the C flags also
# carry force-included headers (the DS SDK expects some declarations the C sources do not ask for),
# and the assembler would try to assemble them.
ASFLAGS := $(filter -I% -D% -m%,$(CFLAGS)) $(ASFLAGS)

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

# ASSET_DIR: a directory whose whole contents go into the VPK beside the executable -- the game's
# data, unpacked from a ROM at build time by scripts/extract_assets.py. Empty means a VPK with no
# game data in it, which reads a ROM from the memory card instead. vita-pack-vpk takes one -a
# source=destination per file, so the list is built here; for Platinum that is 341 of them.
ifneq ($(ASSET_DIR),)
VPK_ASSET_LIST := $(shell cd $(ASSET_DIR) && find . -type f ! -name .stamp | sed 's|^\./||')
VPK_ASSETS := $(foreach f,$(VPK_ASSET_LIST),-a $(ASSET_DIR)/$(f)=$(f))
endif

$(TARGET).vpk: eboot.bin param.sfo
	$(VITA_PACK_VPK) -s param.sfo -b eboot.bin $(VPK_ASSETS) $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).velf $(TARGET).vpk eboot.bin param.sfo

.PHONY: all clean
