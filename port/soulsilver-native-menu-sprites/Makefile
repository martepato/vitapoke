TARGET = ss-menu-sprite-proof
OBJS = main.o progress.o
CFLAGS = -DSDL_MAIN_HANDLED -O2 -include @WORK@/soulsilver-native-core/include/game-pack4.h -Wno-incompatible-pointer-types -DNONMATCHING -Wno-implicit-function-declaration -Wno-int-conversion @TARGETCC@ -std=gnu99 -include @WORK@/soulsilver-native-core/include/compat.h -DPM_KEEP_ASSERTS -DSOULSILVER -DENGLISH -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -D@SDKBUILD@ -ffunction-sections -fdata-sections -I@WORK@/soulsilver-native-core/include -I@WORK@/soulsilver-native-core/game-include -I@WORK@/soulsilver-native-core/game-include/library -I@WORK@/soulsilver-native-core/src -I@WORK@/soulsilver-research/pokeheartgold-slop/files -I@WORK@/native-probe/generated -I@WORK@/native-probe/libntrsystem/include -I@WORK@/native-graphics/libntr/include
LDFLAGS = -Wl,--gc-sections
include @BUILDMAK@
