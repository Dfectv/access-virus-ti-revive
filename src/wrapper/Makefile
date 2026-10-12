# IOCTL emulation DLL (injected for testing; later linked into the wrapper VST3): make -C src/wrapper
CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra
OUT = ../../work/build

all: $(OUT)/vtiemu.dll $(OUT)/VirusTI_WinUSB.vst3 $(OUT)/vtiemu-pipe.dll $(OUT)/vtilaunch.exe $(OUT)/vtiisotest.exe $(OUT)/TIRevive_ASIO.dll $(OUT)/vtiasiohost.exe $(OUT)/vtiloadtest.exe

$(OUT)/vtiemu.dll: vtiemu.c vtiaudio.c vtiusbaudio.c vtiusbaudio.h replay_table.h ../broker/vticore_winusb.c ../broker/vticore_midi.c ../broker/vticore.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ vtiemu.c vtiaudio.c vtiusbaudio.c ../broker/vticore_winusb.c ../broker/vticore_midi.c \
		-lwinusb -lsetupapi -lshlwapi -lole32 -luuid -static -lpthread

# Wrapper VST3 (single-file VST3 module, like the original): emulation linked in (no DllMain from vtiemu.c),
# device access through vtibroker.exe (vticore_pipe.c, M3)
$(OUT)/VirusTI_WinUSB.vst3: vtiwrap.c vtiemu.c vtiaudio.c vtiusbaudio.c vtiusbaudio.h replay_table.h ../broker/vticore_pipe.c ../broker/vticore_midi.c ../broker/vticore.h ../broker/vtiproto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DVTIEMU_NO_DLLMAIN -shared -o $@ vtiwrap.c vtiemu.c vtiaudio.c vtiusbaudio.c ../broker/vticore_pipe.c \
		../broker/vticore_midi.c -lwinusb -lsetupapi -lshlwapi -lshell32 -lole32 -luuid -static -lpthread

# Emulation DLL on the broker (pipe backend) for programs started via vtilaunch (Virus Control Center)
$(OUT)/vtiemu-pipe.dll: vtiemu.c vtiaudio.c vtiusbaudio.c vtiusbaudio.h replay_table.h ../broker/vticore_pipe.c ../broker/vticore_midi.c ../broker/vticore.h ../broker/vtiproto.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ vtiemu.c vtiaudio.c vtiusbaudio.c ../broker/vticore_pipe.c ../broker/vticore_midi.c \
		-lwinusb -lsetupapi -lshlwapi -lole32 -luuid -static -lpthread

# ASIO driver "Virus TI (TI Revive)": wraps the original VirusUSB_X64.dll with its own emulation copy (vtiasio.c)
$(OUT)/TIRevive_ASIO.dll: vtiasio.c vtiemu.c vtiaudio.c vtiusbaudio.c vtiusbaudio.h replay_table.h ../broker/vticore_pipe.c ../broker/vticore_midi.c ../broker/vticore.h ../broker/vtiproto.h ../broker/vtiaudioshm.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -DVTIEMU_NO_DLLMAIN -DVTIEMU_LOG_PREFIX='"vtiasio"' -DVTIAUDIO_FORWARD_OUTPUT -shared -o $@ vtiasio.c vtiemu.c vtiaudio.c \
		vtiusbaudio.c ../broker/vticore_pipe.c ../broker/vticore_midi.c -lwinusb -lsetupapi -lshlwapi -lole32 -luuid \
		-ladvapi32 -static -lpthread

# Minimal ASIO host for testing an ASIO driver without a DAW: vtiasiohost [seconds] [{CLSID}]
$(OUT)/vtiasiohost.exe: vtiasiohost.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ vtiasiohost.c -lole32 -luuid -static

# Load a VST3 like a host's plug-in scanner (reproduce load-time hangs): vtiloadtest <plugin.vst3>
$(OUT)/vtiloadtest.exe: vtiloadtest.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -municode -o $@ vtiloadtest.c -static

$(OUT)/vtilaunch.exe: vtilaunch.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -municode -o $@ vtilaunch.c -static

# Stand-alone test of the WinUSB audio streaming (vtiusbaudio.c): vtiisotest [seconds]
$(OUT)/vtiisotest.exe: vtiisotest.c vtiusbaudio.c vtiusbaudio.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ vtiisotest.c vtiusbaudio.c -lwinusb -lsetupapi -static

clean:
	rm -f $(OUT)/vtiemu.dll $(OUT)/VirusTI_WinUSB.vst3 $(OUT)/vtiemu-pipe.dll $(OUT)/vtilaunch.exe $(OUT)/vtiisotest.exe $(OUT)/TIRevive_ASIO.dll
