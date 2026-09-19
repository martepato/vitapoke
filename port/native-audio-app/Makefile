# The Vita application: the game, the DS SDK replacement, the platform layer and the renderer, linked
# into one executable and packed into a VPK.
#
# Most of what this lists is built elsewhere and copied in as objects, because several of the pieces
# are shared with components that have their own build (the DS memory backing, the sound engine, the
# generated register storage). What is compiled here is the application's own glue.
#
# The platform layer is port/vita: the DS OS interfaces on psp2. Where the PSP build had its own
# implementation of one of those -- threads, alarms, the arena, the pad, the frame, the keyboard --
# this links the Vita one instead and the PSP file is not built at all.
TARGET = vitapoke-platinum
TITLE_ID = VPOK00001
TITLE = vitapoke (Pokemon Platinum)

# The application's own glue.
OBJS = app_main.o config.o frame.o overlay.o romfs.o scene_log.o shacccg_ext_stub.o
# The DS platform layer, on psp2 (port/vita).
OBJS += os_core.o os_thread.o os_alarm.o os_sync.o input.o sdl_sync.o cadence.o owner_info.o \
        backup.o memlog.o audio_out.o watchdog.o
# DS services that are not console specific.
OBJS += ctrdg_absent.o search_disabled.o power.o gx_dma.o mi_dma.o mi_memory.o offline.o
# The DS's memory and registers, and the parts of the game and SDK that other components build.
OBJS += backing.o graphics_registers.o trainer_ai.o
# The DS sound engine.
OBJS += audio_bank.o audio_loader.o audio_backend.o audio_seq.o audio_exchannel.o audio_channel.o \
        audio_engine.o mic_pm.o

INCDIR = ../native-audio-probe/include overlays/include ../native-graphics/libntr/libraries/mi/src ../native-backup-probe/include ../native-fonts/include ../native-input/include ../native-startup/include ../native-probe/backing/include ../native-graphics/alias-proof/include ../native-probe/generated ../native-probe/pokeplatinum/include ../native-probe/libntrsystem/include ../native-graphics/libntr/include
CFLAGS = -DPM_KEEP_ASSERTS -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -D@SDKBUILD@ -DSDK_VERSION_MAJOR=4 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DPOKEPLATINUM_GENERATED_ENUM -ffunction-sections -fdata-sections -include ../native-probe/pokeplatinum/include/pch/global_pch.h
# DEV=1: timing and profiling lines in the log. Errors are logged either way.
DEV ?= 0
ifeq ($(DEV),1)
CFLAGS += -DVITAPOKE_DEV
GAME_PROF ?= 1
endif
CFLAGS += $(EXTRA_CFLAGS)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

RENDER_LIB ?= ../native-vita-render/libnative-render-vita.a
# The psp2 modules the port calls, and vitaGL's. SceShaccCg is linked weakly on purpose: it lives in
# libshacccg.suprx, which the renderer loads at startup, so a console without it still loads the
# application -- and gets the message the renderer writes instead of a failure to start.
STUBS = -lvitaGL -lvitashark -lmathneon -lSceGxm_stub -lSceDisplay_stub -lSceCtrl_stub -lSceTouch_stub \
        -lSceRtc_stub -lScePower_stub -lSceAudio_stub -lSceCommonDialog_stub \
        -lSceKernelDmacMgr_stub -lSceSysmodule_stub -lSceAppMgr_stub -lSceShaccCg_stub_weak \
        -lSceLibKernel_stub -lstdc++ -lm
# One group, because the dependencies run both ways: the game calls the renderer's geometry
# simulator, and the renderer calls back into the SDK. A single pass over these in order would miss
# an archive member that only a later archive turns out to need.
LIBS = -Wl,--start-group $(RENDER_LIB) overlays/libplatinum-overlays.a overlays/libinternal-overlays.a ../native-probe/libnetwork-probe.a libsdk-filtered.a -Wl,--end-group $(STUBS)
# overlays.ld adds the per-overlay data and BSS sections to the toolchain's own linker script; see
# overlays/gen-link.py. --gc-sections is safe with it because those sections are KEEP'd.
LDFLAGS = -Wl,-T,overlays/overlays.ld -Wl,--gc-sections,-Map,$(TARGET).map
# Each overlay module's static constructors are reached only through the overlay loader's table, so
# nothing refers to them by name and --gc-sections would drop every one. overlays/internal.py lists
# them; -u keeps them.
LDFLAGS += $(shell cat overlays/constructors.flags)
# ApplicationManager_New is wrapped for the scene markers in the log (port/vita/scene_log.c); the
# other three are the SDK's dual-slot and wireless entry points, answered by the services below.
LDFLAGS += -Wl,--wrap=ApplicationManager_New -Wl,--wrap=CTRDG_Init,--wrap=CommManager_InitializeSearchParty,--wrap=CommManager_GetAvailableConnections

# TEST ONLY: WARP_TO=<mapHeaderId>,<x>,<z>[,<dir>] links diag_warp.o (FieldTask_Run wrap).
ifneq ($(WARP_TO),)
OBJS += diag_warp.o
CFLAGS += -DWARP_TO=\"$(WARP_TO)\"
LDFLAGS += -Wl,--wrap=FieldTask_Run
endif
# VITAPOKE_GAME_PROF: coarse game-thread CPU buckets and allocator counters in the log every 600
# frames. On with DEV=1, or `make GAME_PROF=1`.
ifneq ($(GAME_PROF),)
OBJS += gameprof.o
CFLAGS += -DVITAPOKE_GAME_PROF
LDFLAGS += -Wl,--wrap=ApplicationManager_Exec,--wrap=CommSys_Update,--wrap=SysTaskManager_ExecuteTasks,--wrap=LandDataManager_RenderLoadedMaps,--wrap=MapPropManager_Render2,--wrap=FieldEffectManager_Render,--wrap=OverworldAnimManagerList_Render,--wrap=BillboardLists_Draw,--wrap=SoundSystem_Tick,--wrap=sub_020241CC,--wrap=ReadKeypadAndTouchpad
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=memalign,--wrap=realloc,--wrap=calloc
endif

# TEST ONLY: MALLOC_GUARD=1 routes every allocation through port/vita/heap_guard.c, which puts a
# magic word either side of each block and checks both on free and on a periodic sweep. It is how a
# heap corruption is traced back to the code that caused it. It costs 32 bytes and a list walk per
# allocation, so it is never on in a build for playing, and it cannot be combined with GAME_PROF,
# which wraps the same four functions for its own accounting.
ifneq ($(MALLOC_GUARD),)
ifneq ($(GAME_PROF),)
$(error MALLOC_GUARD and GAME_PROF both wrap malloc: pick one)
endif
OBJS += heap_guard.o
CFLAGS += -DVITAPOKE_HEAP_GUARD
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=memalign
endif

include @BUILDMAK@

# ---------------------------------------------------------------- the platform layer
VITA = ../vita
app_main.o: $(VITA)/app_main.c
	$(CC) $(CFLAGS) -c $< -o $@
frame.o: $(VITA)/frame.c
	$(CC) $(CFLAGS) -c $< -o $@
scene_log.o: $(VITA)/scene_log.c
	$(CC) $(CFLAGS) -c $< -o $@
os_core.o: $(VITA)/os_core.c
	$(CC) $(CFLAGS) -c $< -o $@
os_thread.o: $(VITA)/os_thread.c
	$(CC) $(CFLAGS) -c $< -o $@
os_alarm.o: $(VITA)/os_alarm.c
	$(CC) $(CFLAGS) -c $< -o $@
os_sync.o: $(VITA)/os_sync.c
	$(CC) $(CFLAGS) -c $< -o $@
input.o: $(VITA)/input.c
	$(CC) $(CFLAGS) -c $< -o $@
sdl_sync.o: $(VITA)/sdl_sync.c
	$(CC) $(CFLAGS) -c $< -o $@
cadence.o: $(VITA)/cadence.c
	$(CC) $(CFLAGS) -c $< -o $@
owner_info.o: $(VITA)/owner_info.c
	$(CC) $(CFLAGS) -c $< -o $@
backup.o: $(VITA)/backup.c
	$(CC) $(CFLAGS) -c $< -o $@
memlog.o: $(VITA)/memlog.c
	$(CC) $(CFLAGS) -c $< -o $@
audio_out.o: $(VITA)/audio_out.c
	$(CC) $(CFLAGS) -c $< -o $@
watchdog.o: $(VITA)/watchdog.c
	$(CC) $(CFLAGS) -c $< -o $@
heap_guard.o: $(VITA)/heap_guard.c
	$(CC) $(CFLAGS) -c $< -o $@
# The one SceShaccCg import VitaSDK has no stub for; see port/vita/shacccg_ext.h.
shacccg_ext_stub.o: $(VITA)/shacccg_ext_stub.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------- the rest
romfs.o: services/romfs.c
	$(CC) $(CFLAGS) -c $< -o $@
overlay.o: overlays/overlay.c overlays/ranges.h
	$(CC) $(CFLAGS) -c $< -o $@
ctrdg_absent.o: services/ctrdg_absent.c
	$(CC) $(CFLAGS) -c $< -o $@
search_disabled.o: services/search_disabled.c
	$(CC) $(CFLAGS) -c $< -o $@
power.o: services/power.c
	$(CC) $(CFLAGS) -c $< -o $@
gx_dma.o: services/gx_dma.c
	$(CC) $(CFLAGS) -c $< -o $@
mi_dma.o: services/mi_dma.c
	$(CC) $(CFLAGS) -c $< -o $@
mi_memory.o: ../native-memory-probe/mi_memory.c
	$(CC) $(CFLAGS) -c $< -o $@

backing.o: ../native-probe/backing/native-backing.o
	/bin/cp -f $< $@
graphics_registers.o: ../native-graphics/alias-proof/graphics_registers.o
	/bin/cp -f $< $@
trainer_ai.o: ../native-core-proof/trainer_ai.o
	/bin/cp -f $< $@
offline.o: ../native-offline/offline.o
	/bin/cp -f $< $@

audio_bank.o: ../native-audio-sound/audio_bank.o
	/bin/cp -f $< $@
audio_loader.o: ../native-audio-sound/audio_loader.o
	/bin/cp -f $< $@
audio_backend.o: ../native-audio-sound/audio_backend.o
	/bin/cp -f $< $@
audio_seq.o: ../native-audio-sound/audio_seq.o
	/bin/cp -f $< $@
audio_exchannel.o: ../native-audio-sound/audio_exchannel.o
	/bin/cp -f $< $@
audio_channel.o: ../native-audio-sound/audio_channel.o
	/bin/cp -f $< $@
audio_engine.o: ../native-audio-sound/audio_engine.o
	/bin/cp -f $< $@
mic_pm.o: ../native-audio-sound/mic_pm.o
	/bin/cp -f $< $@

# frame.c holds the quality-of-life switches' header through overlays/include; rebuild it whenever
# the build runs so a changed switch takes effect without a clean tree.
.PHONY: FORCE_CONFIG
FORCE_CONFIG:
frame.o: FORCE_CONFIG
$(TARGET).elf: | Makefile overlays/overlays.ld overlays/libplatinum-overlays.a overlays/libinternal-overlays.a $(RENDER_LIB)
