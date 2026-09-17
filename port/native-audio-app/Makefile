TARGET = native-app
OBJS = sas_out.o vram_dirty.o stackprobe.o ctrdg_absent.o search_disabled.o cadence.o sdl_threads.o power.o audio_bank.o audio_loader.o audio_out.o audio_backend.o audio_seq.o audio_exchannel.o audio_channel.o audio_engine.o mic_pm.o os_sync.o offline.o gx_dma.o frame.o owner_lock.o threads.o alarms.o overlay.o main.o config.o platform.o input.o romfs.o backup.o backing.o graphics_registers.o trainer_ai.o mi_memory.o mi_dma.o memprobe.o osk.o naming_osk.o
INCDIR = ../native-audio-probe/include overlays/include ../native-graphics/libntr/libraries/mi/src ../native-backup-probe/include ../native-fonts/include ../native-input/include ../native-startup/include ../native-probe/backing/include ../native-graphics/alias-proof/include ../native-probe/generated ../native-probe/pokeplatinum/include ../native-probe/libntrsystem/include ../native-graphics/libntr/include
CFLAGS = -DPM_KEEP_ASSERTS -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -D@SDKBUILD@ -DSDK_VERSION_MAJOR=4 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -DPOKEPLATINUM_GENERATED_ENUM -ffunction-sections -fdata-sections -include ../native-probe/pokeplatinum/include/pch/global_pch.h
# printf is invisible on real hardware, so every build that goes on a card
# mirrors its log to native-memlog.txt next to the EBOOT.
CFLAGS += -DPSP_NATIVE_MEMLOG=1
# DEV=1: on-screen fps counter plus timing/profiling lines in native-memlog.txt (for development).
# Normal builds (DEV=0) have neither; errors are still logged.
DEV ?= 0
ifeq ($(DEV),1)
CFLAGS += -DPSP_NATIVE_DEV
GAME_PROF ?= 1
endif
CFLAGS += $(EXTRA_CFLAGS)
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
RENDER_LIB ?= ../native-stack-render/libnative-render-gu2d.a
LIBS = $(RENDER_LIB) -Wl,--start-group overlays/libplatinum-overlays.a overlays/libinternal-overlays.a ../native-probe/libnetwork-probe.a libsdk-filtered.a -Wl,--end-group -lstdc++ -lpthread -lm
LDFLAGS = -Wl,--gc-sections,-Map,native-app.map
# TEST ONLY: QOL_TEST=<mode> links qol_test.o (Repel_UpdateSteps wrap, see qol_test.c).
ifneq ($(QOL_TEST),)
OBJS += qol_test.o
CFLAGS += -DPSP_NATIVE_QOL_TEST_MODE=$(QOL_TEST)
endif
# TEST ONLY: WARP_TO=<mapHeaderId>,<x>,<z>[,<dir>] links diag_warp.o (FieldTask_Run wrap, see diag_warp.c).
ifneq ($(WARP_TO),)
OBJS += diag_warp.o
CFLAGS += -DWARP_TO=\"$(WARP_TO)\"
endif
# PSP_NATIVE_GAME_PROF: coarse game-thread CPU buckets + allocator counters ([GPROF] card-log lines
# every 600 frames). On with DEV=1, or `make GAME_PROF=1` (adds the define, gameprof.o and the wraps).
ifneq ($(GAME_PROF),)
OBJS += gameprof.o
CFLAGS += -DPSP_NATIVE_GAME_PROF
LDFLAGS += -Wl,--wrap=ApplicationManager_Exec,--wrap=CommSys_Update,--wrap=SysTaskManager_ExecuteTasks,--wrap=LandDataManager_RenderLoadedMaps,--wrap=MapPropManager_Render2,--wrap=FieldEffectManager_Render,--wrap=OverworldAnimManagerList_Render,--wrap=BillboardLists_Draw,--wrap=SoundSystem_Tick,--wrap=sub_020241CC,--wrap=ReadKeypadAndTouchpad
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=memalign,--wrap=realloc,--wrap=calloc
endif
# Own game ID in PARAM.SFO (PSPSDK defaults to UCJS10041, shared with lots of homebrew; custom firmware
# applies per-game settings by this ID).
include @BUILDMAK@
romfs.o: services/romfs.c
	$(CC) $(CFLAGS) -c $< -o $@
backup.o: ../native-backup-probe/backup.c
	$(CC) $(CFLAGS) -c $< -o $@
backing.o: ../native-probe/backing/native-backing.o
	/bin/cp -f $< $@
graphics_registers.o: ../native-graphics/alias-proof/graphics_registers.o
	/bin/cp -f $< $@
trainer_ai.o: ../native-core-proof/trainer_ai.o
	/bin/cp -f $< $@
mi_memory.o: ../native-memory-probe/mi_memory.c
	$(CC) $(CFLAGS) -c $< -o $@
mi_dma.o: services/mi_dma.c
	$(CC) $(CFLAGS) -c $< -o $@

overlay.o: overlays/overlay.c overlays/ranges.h
	$(CC) $(CFLAGS) -c $< -o $@

threads.o: ../native-threads/threads.o
	/bin/cp -f $< $@
alarms.o: ../native-alarms/alarms.o
	/bin/cp -f $< $@

owner_lock.o: services/owner_lock.c
	$(CC) $(CFLAGS) -c $< -o $@

offline.o: ../native-offline/offline.o
	/bin/cp -f $< $@
gx_dma.o: services/gx_dma.c
	$(CC) $(CFLAGS) -c $< -o $@

audio_bank.o: ../native-audio-sound/audio_bank.o
	/bin/cp -f $< $@

audio_loader.o: ../native-audio-sound/audio_loader.o
	/bin/cp -f $< $@

audio_out.o: ../native-audio-sound/audio_out.o
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
sas_out.o: ../native-audio-sound/sas_out.o
	/bin/cp -f $< $@

mic_pm.o: ../native-audio-sound/mic_pm.o
	/bin/cp -f $< $@

os_sync.o: ../native-audio-sound/os_sync.o
	/bin/cp -f $< $@

power.o: services/power.c
	$(CC) $(CFLAGS) -c $< -o $@

sdl_threads.o: ../native-sdl-thread/sdl_threads.o
	/bin/cp -f $< $@

cadence.o: ../native-cadence/cadence.o
	/bin/cp -f $< $@

.PHONY: FORCE_NATIVE_CONFIG
FORCE_NATIVE_CONFIG:
frame.o: FORCE_NATIVE_CONFIG
$(TARGET).elf: | Makefile overlays/libplatinum-overlays.a overlays/libinternal-overlays.a $(RENDER_LIB)

# Static firmware-format audit catches the retail 80020148 section-bound failure.
.PHONY: check-native-format PARAM.SFO
all: check-native-format
check-native-format: EBOOT.PBP
	python3 ../../tools/check_native_pbp.py EBOOT.PBP

ctrdg_absent.o: services/ctrdg_absent.c
	$(CC) $(CFLAGS) -c $< -o $@
search_disabled.o: services/search_disabled.c
	$(CC) $(CFLAGS) -c $< -o $@
LDFLAGS += -Wl,--wrap=G3X_GetClipMtx -Wl,--wrap=ApplicationManager_New -Wl,--wrap=CTRDG_Init,--wrap=CommManager_InitializeSearchParty,--wrap=CommManager_GetAvailableConnections
ifneq ($(QOL_TEST),)
LDFLAGS += -Wl,--wrap=Repel_UpdateSteps
endif
ifneq ($(WARP_TO),)
LDFLAGS += -Wl,--wrap=FieldTask_Run
endif
