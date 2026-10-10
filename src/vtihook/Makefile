# Cross-build on Linux with mingw-w64: make -C src/vtihook
CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra -municode
OUT = ../../work/build

all: $(OUT)/vtihook.dll $(OUT)/vtiinject.exe

$(OUT)/vtihook.dll: vtihook.c
	mkdir -p $(OUT)
	$(CC) -O2 -Wall -Wextra -shared -o $@ $< -lsetupapi -lshlwapi -static-libgcc

$(OUT)/vtiinject.exe: vtiinject.c
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -static-libgcc

clean:
	rm -f $(OUT)/vtihook.dll $(OUT)/vtiinject.exe
