# Host (Linux) build of the broker core and its hardware probe: make -C src/broker
CFLAGS = -O2 -Wall -Wextra $(shell pkg-config --cflags libusb-1.0)
LIBS = $(shell pkg-config --libs libusb-1.0) -lpthread
OUT = ../../work/build/linux

all: $(OUT)/vtiprobe

$(OUT)/vtiprobe: vtiprobe.c vticore.c vticore.h
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ vtiprobe.c vticore.c $(LIBS)

clean:
	rm -f $(OUT)/vtiprobe
