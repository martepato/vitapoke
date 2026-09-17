# vitapoke: the PSP build's make fragment.
#
# Every component Makefile ends with `include @BUILDMAK@`, which stage.sh points at this file for the
# PSP and at vita.mak for the Vita. For the PSP that is exactly what the Makefiles used to do inline,
# so the build is unchanged: PSPSDK's own build.mak supplies CC, the object rules and the EBOOT targets.
#
# PSPSDK is left defined because a few Makefiles substitute $(PSPSDK)/lib/linkfile.prx out of LDFLAGS
# after this include, to swap in their own linker script.
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
