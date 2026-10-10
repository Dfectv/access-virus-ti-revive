# IOCTL emulation DLL (injected for testing; later linked into the wrapper VST3): make -C src/wrapper
CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra
OUT = ../../work/build

all: $(OUT)/vtiemu.dll $(OUT)/VirusTI_WinUSB.vst3

$(OUT)/vtiemu.dll: vtiemu.c replay_table.h ../broker/vticore_winusb.c ../broker/vticore_midi.c ../broker/vticore.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ vtiemu.c ../broker/vticore_winusb.c ../broker/vticore_midi.c \
		-lwinusb -lsetupapi -lshlwapi -static -lpthread

# Wrapper VST3 (single-file VST3 module, like the original): emulation linked in, no DllMain from vtiemu.c
$(OUT)/VirusTI_WinUSB.vst3: vtiwrap.c vtiemu.c replay_table.h ../broker/vticore_winusb.c ../broker/vticore_midi.c ../broker/vticore.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DVTIEMU_NO_DLLMAIN -shared -o $@ vtiwrap.c vtiemu.c ../broker/vticore_winusb.c \
		../broker/vticore_midi.c -lwinusb -lsetupapi -lshlwapi -lshell32 -lole32 -luuid -static -lpthread

clean:
	rm -f $(OUT)/vtiemu.dll $(OUT)/VirusTI_WinUSB.vst3
