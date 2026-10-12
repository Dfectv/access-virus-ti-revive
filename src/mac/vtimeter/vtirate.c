// vtirate.c - diagnostics: set the nominal sample rate of the "Virus TI" Core Audio device (e.g. 48000 then 44100 to
// make AppleUSBAudio restart its stream, E109). Build: clang -O2 -o vtirate vtirate.c -framework CoreAudio
// -framework CoreFoundation; usage: vtirate <rate> [<rate> ...] (0.5 s apart)
#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioObjectID ids[64], dev = 0;
    UInt32 size = sizeof ids;
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids);
    for (UInt32 i = 0; i < size / sizeof *ids && !dev; i++) {
        CFStringRef name = NULL;
        UInt32 n = sizeof name;
        AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
        char buf[128] = "";
        if (!AudioObjectGetPropertyData(ids[i], &na, 0, NULL, &n, &name) && name) {
            CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
            CFRelease(name);
        }
        if (!strcmp(buf, "Virus TI"))
            dev = ids[i];
    }
    if (!dev)
        return fprintf(stderr, "no Virus TI device\n"), 1;
    AudioObjectPropertyAddress ra = {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    for (int k = 1; k < argc; k++) {
        Float64 r = atof(argv[k]), cur = 0;
        OSStatus st = AudioObjectSetPropertyData(dev, &ra, 0, NULL, sizeof r, &r);
        usleep(500000);
        UInt32 n = sizeof cur;
        AudioObjectGetPropertyData(dev, &ra, 0, NULL, &n, &cur);
        printf("set %.0f -> %d, now %.0f\n", r, (int)st, cur);
    }
    return 0;
}
