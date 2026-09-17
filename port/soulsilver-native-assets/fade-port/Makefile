CC = @TOOLBIN@gcc
AR = @TOOLBIN@ar
ROOT = @WORK@
INC = -I$(ROOT)/native-probe/generated -I$(ROOT)/native-probe/pokeplatinum/include -I$(ROOT)/native-probe/libntrsystem/include -I$(ROOT)/native-graphics/libntr/include
CFLAGS = -O2 @TARGETCC@ -std=gnu99 -DPM_KEEP_ASSERTS -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -D@SDKBUILD@ -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DPOKEPLATINUM_GENERATED_ENUM -ffunction-sections -fdata-sections $(INC) -include names.h -include $(ROOT)/native-probe/pokeplatinum/include/pch/global_pch.h
all: libss-native-fade.a
libss-native-fade.a: screen_fade.o screen_fade_funcs.o hardware_window.o
	$(AR) rcs $@ $^
%.o: %.c names.h
	$(CC) $(CFLAGS) -c $< -o $@
