# Broker core + hardware probe. make -C src/broker      -> Linux (libusb) and Windows (WinUSB, mingw-w64)
CFLAGS = -O2 -Wall -Wextra
OUT = ../../work/build
SRC_MIDI = vticore_midi.c

all: $(OUT)/linux/vtiprobe $(OUT)/vtiprobe.exe

$(OUT)/linux/vtiprobe: vtiprobe.c vticore.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)/linux
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libusb-1.0) -o $@ vtiprobe.c vticore.c $(SRC_MIDI) \
		$(shell pkg-config --libs libusb-1.0) -lpthread

$(OUT)/vtiprobe.exe: vtiprobe.c vticore_winusb.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtiprobe.c vticore_winusb.c $(SRC_MIDI) -lwinusb -lsetupapi -static -lpthread

clean:
	rm -f $(OUT)/linux/vtiprobe $(OUT)/vtiprobe.exe
