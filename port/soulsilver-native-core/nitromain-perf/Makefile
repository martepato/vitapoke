TARGET = ss-native-main
OBJS = g2_asm_abi.o mi_hwaddr.o naming_osk_ss.o osk.o fx_mtx_native.o ss_memlog.o dsprot_native.o area_data_native.o billboard_bootstrap.o fieldmap_native.o map_movement.o field_init.o main_menu_native.o menu_native.o window_adapter.o fade_trace.o g2d_culling.o g2d_culling_proof.o fade_wrapper_test.o g2d_native.o g2d_proof.o overlay.o brightness_native.o brightness_proof.o timer3_psp.o frame.o cadence.o game_aliases.o ssmain_trace.o missing.o sdk_alias.o owner_lock.o backup.o audio_bank.o audio_loader.o audio_backend.o audio_seq.o audio_exchannel.o audio_channel.o audio_engine.o offline.o gx_dma.o main.o platform.o config.o romfs.o romfs_lock.o ss_rom_header.o threads.o alarms.o mi_memory.o mi_dma.o backing.o graphics_registers.o power.o mic_pm.o os_sync.o sdl_threads.o ctrdg_absent.o input.o wifi_check_native.o wifi_task_native.o mwcc_float_ptrs.o sas_out.o
INCDIR = include @WORK@/soulsilver-native-core/include @WORK@/soulsilver-native-core/game-include @WORK@/native-probe/generated @WORK@/native-probe/libntrsystem/include @WORK@/native-graphics/libntr/include
PSP_NATIVE_PROBE_FRAMES ?= 3600
PSP_NATIVE_PLAY ?= 0
CFLAGS = -DPSP_NATIVE_PROBE_FRAMES=$(PSP_NATIVE_PROBE_FRAMES) -DPSP_NATIVE_MEMLOG=1 -DPSP_NATIVE_PLAY=$(PSP_NATIVE_PLAY) -DSDL_MAIN_HANDLED -O2 @TARGETCC@ -std=gnu99 -DSDK_PORT -DSDK_X86 -D@SDKBUILD@ -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -ffunction-sections -fdata-sections
# DEV=1: on-screen fps counter plus timing lines in native-memlog.txt (for development).
DEV ?= 0
ifeq ($(DEV),1)
CFLAGS += -DPSP_NATIVE_DEV
endif
# Test-only frame dumps (alpha-3D check); empty for every normal/play build.
CFLAGS += $(FRAME_DUMP_CFLAGS)
# Diagnostic replays only: the firmware keyboard cannot be typed into by headless PPSSPP,
# so `make OSK_AUTONAME=Gold` makes osk.c return that name without opening the dialog.
# Never set for a release build (the play build recipe does not).
ifdef OSK_AUTONAME
CFLAGS += -DPSP_NATIVE_OSK_AUTONAME=\"$(OSK_AUTONAME)\"
endif
LIBS = -Wl,--start-group ../../soulsilver-native-player-movement/libss-player-movement.a ../../soulsilver-native-menu-sprites/libss-menu-sprites.a ../../soulsilver-native-particles/libss-particles.a ../../soulsilver-native-window/libss-window.a ../../soulsilver-native-sound-helpers/libss-sound-helpers.a ../../soulsilver-native-assets/fade-port/wipe-candidate/libss-native-fade.a ../../soulsilver-native-overlays/libss-game-overlays.a ../../soulsilver-native-play/maploader/libss-maploader.a ../../soulsilver-native-codegen/libss-native-translated.a ../../soulsilver-native-overlays/libss-data-overlays.a ../../soulsilver-native-islands/libss-wfc-startup.a libsdk-filtered-ss.a libss-render.a ../../native-probe/libnetwork-probe.a -Wl,--end-group -lstdc++ -lpsppower -lpspgu -lpspaudio -lpspsascore -lpsputility -lpspwlan -lpsprtc -lm
LDFLAGS = -Wl,--wrap=sub_02078B78 -Wl,--wrap=sub_02078B58 -Wl,--wrap=BeginNormalPaletteFade -Wl,--wrap=OverlayManager_New -Wl,--wrap=MIi_CpuClear32 -Wl,--wrap=MIi_CpuClear16 -Wl,--wrap=MIi_CpuClearFast -Wl,--wrap=MIi_CpuCopy16 -Wl,--wrap=MIi_CpuCopy32 -Wl,--wrap=MIi_CpuCopyFast -Wl,--wrap=MI_CpuCopy8 -Wl,--wrap=MI_CpuFill8 -Wl,--wrap=MI_DmaCopy16 -Wl,--wrap=MI_DmaCopy32 -Wl,--wrap=MI_DmaFill32 -Wl,--wrap=DC_FlushRange -Wl,--wrap=CTRDG_Init -Wl,--wrap=G3X_GetClipMtx -Wl,--gc-sections,-Map,ss-native-main.map
# Diagnostic replays only: `make NO_WILD=1` links diag_nowild.o, which wraps the grass-tile
# encounter check so exploration scripts walk without wild battles. Never for a release.
# (Must follow the LDFLAGS definition; relink.py rewrites this file from the copy it read at
# start, so never edit the Makefile while a loop is running.)
# Diagnostic replays only: `make WARP_TO=67,574,398` warps the player there once the field runs.
ifdef WARP_TO
OBJS += diag_warp.o
LDFLAGS += -Wl,--wrap=PlayerAvatar_UpdateMovement
CFLAGS += -DWARP_TO=\"$(WARP_TO)\"
endif
ifdef NO_WILD
OBJS += diag_nowild.o
LDFLAGS += -Wl,--wrap=MetatileBehavior_CanGenerateWalkingEncounters
endif
# Diagnostic replays only: GIVE_SPECIES / GIVE_ITEMS / GIVE_BADGES / TEACH_MOVES share diag_give.c.
DIAG_GIVE_ANY := $(GIVE_SPECIES)$(GIVE_ITEMS)$(GIVE_BADGES)$(TEACH_MOVES)$(GIVE_ITEM_IDS)$(SET_VARS)$(SET_FLAGS)$(CLEAR_FLAGS)$(GIVE_FRIENDSHIP)$(REPEL_STEPS)
ifneq ($(DIAG_GIVE_ANY),)
ifdef HATCH_NOW
$(error GIVE_SPECIES/GIVE_ITEMS/GIVE_BADGES/TEACH_MOVES and HATCH_NOW all wrap HandleDaycareStep; use one at a time)
endif
OBJS += diag_give.o
LDFLAGS += -Wl,--wrap=HandleDaycareStep
CFLAGS += $(if $(GIVE_SPECIES),-DGIVE_SPECIES=$(GIVE_SPECIES)) $(if $(GIVE_LEVEL),-DGIVE_LEVEL=$(GIVE_LEVEL))
CFLAGS += $(if $(GIVE_ITEMS),-DGIVE_ITEMS=1) $(if $(GIVE_BADGES),-DGIVE_BADGES=$(GIVE_BADGES))
CFLAGS += $(if $(TEACH_MOVES),-DTEACH_MOVES=\"$(TEACH_MOVES)\")
CFLAGS += $(if $(GIVE_ITEM_IDS),-DGIVE_ITEM_IDS=\"$(GIVE_ITEM_IDS)\")
CFLAGS += $(if $(SET_FLAGS),-DSET_FLAGS=\"$(SET_FLAGS)\") $(if $(CLEAR_FLAGS),-DCLEAR_FLAGS=\"$(CLEAR_FLAGS)\")
CFLAGS += $(if $(GIVE_FRIENDSHIP),-DGIVE_FRIENDSHIP=$(GIVE_FRIENDSHIP)) $(if $(REPEL_STEPS),-DREPEL_STEPS=$(REPEL_STEPS) -DREPEL_FULL=$(or $(REPEL_FULL),100))
CFLAGS += $(if $(SET_VARS),-DSET_VARS=\"$(SET_VARS)\")
endif
# Diagnostic replays only: `make DIAG_TILES=1` logs unique (x,z,behaviour,solid) from GetMetatileBehavior.
ifdef DIAG_TILES
OBJS += diag_tiles.o
LDFLAGS += -Wl,--wrap=GetMetatileBehavior
endif
ifdef HATCH_NOW
OBJS += diag_hatch.o
LDFLAGS += -Wl,--wrap=HandleDaycareStep
endif
ifdef PARTY_LEVEL
ifdef WARP_TO
$(error PARTY_LEVEL and WARP_TO both wrap PlayerAvatar_UpdateMovement; use one at a time)
endif
OBJS += diag_level.o
LDFLAGS += -Wl,--wrap=PlayerAvatar_UpdateMovement
CFLAGS += -DPARTY_LEVEL=$(PARTY_LEVEL)
ifdef PARTY_LEVEL_EDGE
CFLAGS += -DPARTY_LEVEL_EDGE
endif
endif
BUILD_PRX = 1
PSP_LARGE_MEMORY = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SoulSilver (pspoke)
# Own game ID in PARAM.SFO (PSPSDK defaults to UCJS10041, shared with lots of homebrew; custom firmware
# applies per-game settings by this ID).
SFOFLAGS += -s DISC_ID=NATS00001
include @BUILDMAK@

LDFLAGS := $(subst $(PSPSDK)/lib/linkfile.prx,linkfile.prx,$(LDFLAGS))
.PHONY: PARAM.SFO
ss-native-main.elf: | linkfile.prx Makefile

# overlay.o comes from the shared overlay manager source (built with this program's flags).
overlay.o: ../../soulsilver-native-overlays/overlay.c
	$(CC) $(CFLAGS) -c $< -o $@
menu_native.o: menu_native.c
	python3 build_menu.py

main_menu_native.o: ../src/application/main_menu/main_menu.c build_main_menu.py
	python3 build_main_menu.py

field_init.o: field_init.c build_field_init.py
	python3 build_field_init.py

map_movement.o: map_movement.c build_map_movement.py
	python3 build_map_movement.py

fieldmap_native.o: fieldmap_native.c build_fieldmap.py
	python3 build_fieldmap.py

area_data_native.o: area_data_native.c build_area_data.py
	python3 build_area_data.py

ssmain_trace.o: ../src/main.c trace_main.py
	python3 trace_main.py
