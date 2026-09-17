TARGET = ss-native-codegen
OBJS = main.o
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -ffunction-sections -fdata-sections -fno-inline-functions
LDFLAGS = -Wl,--gc-sections
LIBS = -lpsppower
BUILD_PRX = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SoulSilver native assembly bridge proof
include @BUILDMAK@
main.o: generated.c
