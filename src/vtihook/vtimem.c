// vtimem.c - hexdump memory of another process (read-only): vtimem.exe <pid> <address-hex> [length]
// Used to follow pointers inside the running Virus plugin (AudioSM input source, E054).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtimem <pid> <address-hex> [length]\n");
        return 2;
    }
    HANDLE p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, strtoul(argv[1], NULL, 10));
    if (!p) {
        fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }
    uintptr_t a = (uintptr_t)strtoull(argv[2], NULL, 16);
    size_t n = argc > 3 ? strtoul(argv[3], NULL, 0) : 64;
    uint8_t *buf = calloc(1, n);
    SIZE_T got = 0;
    if (!ReadProcessMemory(p, (void *)a, buf, n, &got))
        printf("ReadProcessMemory failed: %lu (got %llu)\n", GetLastError(), (unsigned long long)got);
    for (size_t i = 0; i < got; i += 16) {
        printf("%016llx:", (unsigned long long)(a + i));
        for (size_t j = i; j < i + 16 && j < got; j++)
            printf(" %02x", buf[j]);
        printf("   qwords:");
        for (size_t j = i; j + 8 <= i + 16 && j + 8 <= got; j += 8)
            printf(" %016llx", *(unsigned long long *)(buf + j));
        printf("\n");
    }
    return 0;
}
