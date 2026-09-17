TARGET = native-sound
OBJS = audio_bank.o audio_loader.o audio_backend.o audio_seq.o audio_exchannel.o audio_channel.o audio_engine.o mic_pm.o os_sync.o backup.o savedata.o mi_memory.o mi_dma.o input.o rtc.o os_vram.o backing.o main.o romfs.o boot.o platform.o system.o heap.o math_util.o sys_task_manager.o graphics_registers.o gx.o gx_vramcnt.o gxstate.o
INCDIR = ../native-audio-probe/include ../native-graphics/libntr/libraries/sim/include ../native-graphics/libntr/libraries/mi/src ../native-backup-probe/include include ../native-probe/backing/include ../native-graphics/libntr/libraries/gx/include ../native-graphics/alias-proof/include ../native-probe/libntrdwc/include ../native-probe/libntrsystem/include ../native-probe/generated ../native-probe/pokeplatinum/include ../native-graphics/libntr/include
CFLAGS = -D_NITRO -DPM_KEEP_ASSERTS -DPOKEPLATINUM_GENERATED_ENUM -O2 @TARGETCC@ -std=gnu99 -DSDL_MAIN_HANDLED -DSDK_PORT -DSDK_X86 -DSDK_TS -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM -include ../native-probe/pokeplatinum/include/pch/global_pch.h -ffunction-sections -fdata-sections
# Sound plays through the PSP sceSasCore voice mixer (sas_out.c); the CPU mixer is not used.
CFLAGS += -DPSP_NATIVE_SAS
CFLAGS += $(EXTRA_CFLAGS)
LIBS = -lpsppower -lstdc++ -lm -lpspaudio -lpsprtc -Wl,--start-group libplatinum-probe.a libplatinum-internal.a libnetwork-probe.a libnative-filtered.a -lstdc++ -lpthread -lc -Wl,--end-group
LDFLAGS =  -Wl,-Map=link.map -Wl,--gc-sections
BUILD_PRX = 1
PSP_LARGE_MEMORY = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Native Platinum startup
include @BUILDMAK@
boot.o: ../native-probe/pokeplatinum/src/boot.c
	$(CC) $(CFLAGS) -c $< -o $@

narc.o: ../native-probe/pokeplatinum/src/narc.c
	$(CC) $(CFLAGS) -c $< -o $@

system.o: ../native-probe/pokeplatinum/src/system.c
	$(CC) $(CFLAGS) -c $< -o $@

heap.o: ../native-probe/pokeplatinum/src/heap.c
	$(CC) $(CFLAGS) -c $< -o $@

math_util.o: ../native-probe/pokeplatinum/src/math_util.c
	$(CC) $(CFLAGS) -c $< -o $@

sys_task_manager.o: ../native-probe/pokeplatinum/src/sys_task_manager.c
	$(CC) $(CFLAGS) -c $< -o $@

gx.o: ../native-graphics/libntr/libraries/gx/src/gx.c
	$(CC) $(CFLAGS) -c $< -o $@

gx_vramcnt.o: ../native-graphics/libntr/libraries/gx/src/gx_vramcnt.c
	$(CC) $(CFLAGS) -c $< -o $@

gxstate.o: ../native-graphics/libntr/libraries/gx/src/gxstate.c
	$(CC) $(CFLAGS) -c $< -o $@

romfs.o: ../native-romfs/romfs.c
	$(CC) $(CFLAGS) -c $< -o $@

graphics_registers.o: ../native-graphics/alias-proof/graphics_registers.S
	$(CC) -c $< -o $@

storage.o: storage.S
	$(CC) -c $< -o $@

os_vram.o: ../native-graphics/libntr/libraries/os/src/os_vramExclusive.c
	$(CC) $(CFLAGS) -c $< -o $@

rtc.o: ../native-probe/pokeplatinum/src/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

backing.o: ../native-probe/backing/native-backing.o
	cp $< $@

backup.o: ../native-backup-probe/backup.c
	$(CC) $(CFLAGS) -c $< -o $@

savedata.o: ../native-probe/pokeplatinum/src/savedata.c
	$(CC) $(CFLAGS) -c $< -o $@

mi_memory.o: ../native-memory-probe/mi_memory.c
	$(CC) $(CFLAGS) -c $< -o $@

mi_dma.o: ../native-memory-probe/mi_dma.c
	$(CC) $(CFLAGS) -c $< -o $@

audio_backend.o: backend.c consumer_switch.inc
	$(CC) $(CFLAGS) -c $< -o $@

audio_seq.o: ../native-graphics/libntr/libraries/sim7/src/snd/snd_seq.c
	$(CC) $(CFLAGS) -c $< -o $@

audio_exchannel.o: ../native-graphics/libntr/libraries/sim7/src/snd/snd_exchannel.c
	$(CC) $(CFLAGS) -c $< -o $@

audio_channel.o: ../native-graphics/libntr/libraries/sim7/src/snd/snd_channel.c
	$(CC) $(CFLAGS) -c $< -o $@

audio_engine.o: sim_audio.cpp
	$(CXX) $(filter-out -std=gnu99 -include ../native-probe/pokeplatinum/include/pch/global_pch.h,$(CFLAGS)) -std=gnu++17 -fno-exceptions -fno-rtti -c $< -o $@

audio_bank.o: ../native-audio-probe/snd_bank.c
	$(CC) $(filter-out -include ../native-probe/pokeplatinum/include/pch/global_pch.h,$(CFLAGS)) -include ../native-graphics/libntr/include/pch/nitro_pch.h -c $< -o $@

audio_loader.o: ../native-probe/libntrsystem/libraries/snd/src/sndarc_loader.c
	$(CC) $(CFLAGS) -c $< -o $@

sas_out.o: sas_out.c
	$(CC) $(CFLAGS) -c $< -o $@
