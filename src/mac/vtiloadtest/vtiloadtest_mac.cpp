// vtiloadtest_mac.cpp - load a macOS VST3 bundle like a host (bundleEntry, GetPluginFactory, createInstance,
// IComponent::initialize with a minimal IHostApplication) and print a symbolised backtrace on a crash, to debug the
// Access plug-in with TI Revive's libVirusUSB replacement without a DAW (macOS counterpart of src/wrapper/vtiloadtest.c).
// Build x86_64 (the Access plug-in is Intel-only, runs under Rosetta):
//   clang++ -arch x86_64 -std=c++17 -O1 -g -o vtiloadtest vtiloadtest_mac.cpp -framework CoreFoundation
// usage: vtiloadtest "/Library/Audio/Plug-Ins/VST3/Virus TI.vst3" [seconds to keep the instance alive]
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <initializer_list>
#include <signal.h>
#include <unistd.h>

static void on_crash(int sig, siginfo_t *si, void *)
{
    fprintf(stderr, "\n*** signal %d (%s) at address %p\n", sig, strsignal(sig), si ? si->si_addr : nullptr);
    void *frames[64];
    int n = backtrace(frames, 64);
    for (int i = 0; i < n; i++) {
        Dl_info d{};
        if (dladdr(frames[i], &d) && d.dli_fname) {
            const char *mod = strrchr(d.dli_fname, '/');
            fprintf(stderr, "  #%-2d %-28s %s + 0x%lx\n", i, mod ? mod + 1 : d.dli_fname,
                    d.dli_sname ? d.dli_sname : "?",
                    (unsigned long)((char *)frames[i] - (char *)(d.dli_saddr ? d.dli_saddr : d.dli_fbase)));
        } else {
            fprintf(stderr, "  #%-2d %p\n", i, frames[i]);
        }
    }
    _exit(128 + sig);
}

// --- minimal VST3 ABI (Steinberg FUnknown / IPluginFactory / IComponent; non-COM byte order on macOS)
struct FUID { unsigned char b[16]; };
static FUID uid(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    FUID u;
    uint32_t v[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++)
            u.b[i * 4 + k] = (unsigned char)(v[i] >> (24 - 8 * k));
    return u;
}
static const FUID IID_FUnknown = uid(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
static const FUID IID_IComponent = uid(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
static const FUID IID_IHostApplication = uid(0x58E595CC, 0xDB2D4969, 0x8B6AAF8C, 0x36A664E5);

struct PClassInfo { char cid[16]; int32_t cardinality; char category[32]; char name[64]; };
struct IPluginFactory {
    virtual int32_t queryInterface(const char *, void **) = 0;
    virtual uint32_t addRef() = 0;
    virtual uint32_t release() = 0;
    virtual int32_t getFactoryInfo(void *) = 0;
    virtual int32_t countClasses() = 0;
    virtual int32_t getClassInfo(int32_t, PClassInfo *) = 0;
    virtual int32_t createInstance(const char *, const char *, void **) = 0;
};
struct IComponentLike { // FUnknown + IPluginBase
    virtual int32_t queryInterface(const char *, void **) = 0;
    virtual uint32_t addRef() = 0;
    virtual uint32_t release() = 0;
    virtual int32_t initialize(void *context) = 0;
    virtual int32_t terminate() = 0;
};
struct Host { // IHostApplication
    virtual int32_t queryInterface(const char *iid, void **obj)
    {
        if (!memcmp(iid, IID_IHostApplication.b, 16) || !memcmp(iid, IID_FUnknown.b, 16)) {
            *obj = this;
            return 0;
        }
        *obj = nullptr;
        return -1; // kNoInterface
    }
    virtual uint32_t addRef() { return 1; }
    virtual uint32_t release() { return 1; }
    virtual int32_t getName(char16_t *s)
    {
        const char *n = "vtiloadtest";
        for (int i = 0;; i++)
            if (!(s[i] = (char16_t)n[i]))
                break;
        return 0;
    }
    virtual int32_t createInstance(const char *, const char *, void **o) { *o = nullptr; return 3; }
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vtiloadtest <plugin.vst3> [seconds]\n");
        return 1;
    }
    struct sigaction sa{};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE})
        sigaction(s, &sa, nullptr);
    int keep = argc > 2 ? atoi(argv[2]) : 3;

    CFStringRef path = CFStringCreateWithCString(nullptr, argv[1], kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
        fprintf(stderr, "could not load the bundle executable\n");
        return 1;
    }
    printf("bundle loaded\n");
    auto entry = (bool (*)(CFBundleRef))CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry"));
    if (entry)
        printf("bundleEntry -> %d\n", entry(bundle));
    auto get = (IPluginFactory * (*)()) CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory"));
    IPluginFactory *f = get ? get() : nullptr;
    printf("GetPluginFactory -> %p\n", (void *)f);
    if (!f)
        return 1;
    int n = f->countClasses();
    printf("%d classes\n", n);
    static Host host;
    for (int i = 0; i < n; i++) {
        PClassInfo ci{};
        f->getClassInfo(i, &ci);
        printf("class %d \"%s\" (%s)\n", i, ci.name, ci.category);
        if (strcmp(ci.category, "Audio Module Class"))
            continue;
        void *c = nullptr;
        int32_t r = f->createInstance(ci.cid, (const char *)IID_IComponent.b, &c);
        printf("  createInstance(IComponent) -> %d %p\n", r, c);
        fflush(stdout);
        if (!c)
            continue;
        auto *comp = (IComponentLike *)c;
        printf("  initialize -> %d\n", comp->initialize(&host));
        fflush(stdout);
        sleep(keep);
        printf("  terminate -> %d\n", comp->terminate());
        comp->release();
        break; // the first audio module is enough
    }
    printf("OK\n");
    return 0;
}
