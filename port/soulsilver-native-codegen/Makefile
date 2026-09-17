TARGET = ss-native-codegen
OBJS = main.o
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -ffunction-sections -fdata-sections -fno-inline-functions
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
main.o: generated.c
