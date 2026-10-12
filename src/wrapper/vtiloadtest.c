// vtiloadtest.c - load a VST3 module like a host's plug-in scanner, to reproduce load-time hangs/crashes without a
// DAW: LoadLibrary, InitDll, GetPluginFactory, list the classes, create (and release) an instance of each.
// A watchdog reports HANG after 20 s. usage: vtiloadtest "C:\...\Plugin.vst3"
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { char cid[16]; int cardinality; char category[32]; char name[64]; } PClassInfo;
typedef struct IPluginFactory IPluginFactory;
typedef struct {
    long(__stdcall *queryInterface)(IPluginFactory *, const char *, void **);
    unsigned long(__stdcall *addRef)(IPluginFactory *);
    unsigned long(__stdcall *release)(IPluginFactory *);
    long(__stdcall *getFactoryInfo)(IPluginFactory *, void *);
    int(__stdcall *countClasses)(IPluginFactory *);
    long(__stdcall *getClassInfo)(IPluginFactory *, int, PClassInfo *);
    long(__stdcall *createInstance)(IPluginFactory *, const char *, const char *, void **);
} IPluginFactoryVtbl;
struct IPluginFactory { const IPluginFactoryVtbl *v; };
typedef struct { const struct { long(__stdcall *qi)(void *, const char *, void **); unsigned long(__stdcall *addRef)(void *);
                                 unsigned long(__stdcall *release)(void *); } *v; } IUnk;

// Minimal Steinberg::Vst::IHostApplication {58E595CC-DB2D-4969-8B6A-AF8C36A664E5} as initialize() context:
// FUnknown (3), getName(String128), createInstance(cid, iid, obj) -> kNotImplemented
typedef struct Host Host;
typedef struct {
    long(__stdcall *qi)(Host *, const char *, void **);
    unsigned long(__stdcall *addRef)(Host *);
    unsigned long(__stdcall *release)(Host *);
    long(__stdcall *getName)(Host *, unsigned short *);
    long(__stdcall *createInstance)(Host *, const char *, const char *, void **);
} HostVtbl;
struct Host { const HostVtbl *v; };
static const char IID_IHostApplication[16] = {(char)0xCC, (char)0x95, (char)0xE5, 0x58, 0x2D, (char)0xDB, 0x69, 0x49,
                                              (char)0x8B, 0x6A, (char)0xAF, (char)0x8C, 0x36, (char)0xA6, 0x64, (char)0xE5};
static const char IID_FUnknown_[16] = {0, 0, 0, 0, 0, 0, 0, 0, (char)0xC0, 0, 0, 0, 0, 0, 0, 0x46};
static long __stdcall host_qi(Host *h, const char *iid, void **obj)
{
    if (!memcmp(iid, IID_IHostApplication, 16) || !memcmp(iid, IID_FUnknown_, 16)) {
        *obj = h;
        return 0;
    }
    *obj = NULL;
    return -2147467262; // kNoInterface
}
static unsigned long __stdcall host_ref(Host *h) { (void)h; return 1; }
static long __stdcall host_name(Host *h, unsigned short *s)
{
    (void)h;
    const char *n = "vtiloadtest";
    for (int i = 0; n[i] || (s[i] = 0, 0); i++)
        s[i] = (unsigned short)n[i];
    return 0;
}
static long __stdcall host_create(Host *h, const char *c, const char *i, void **o)
{
    (void)h, (void)c, (void)i;
    *o = NULL;
    return -2147467263; // kNotImplemented
}
static const HostVtbl g_host_vtbl = {host_qi, host_ref, host_ref, host_name, host_create};
static Host g_host = {&g_host_vtbl};

static volatile LONG g_stage;
static const char *STAGES[] = {"LoadLibrary", "InitDll", "GetPluginFactory", "countClasses", "createInstance",
                               "initialize", "done"};

static DWORD WINAPI watchdog(LPVOID unused)
{
    (void)unused;
    Sleep(20000);
    printf("HANG in stage %s\n", STAGES[g_stage]);
    fflush(stdout);
    ExitProcess(2);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) {
        printf("usage: vtiloadtest <plugin.vst3>\n");
        return 1;
    }
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    DWORD t0 = GetTickCount();
    HMODULE m = LoadLibraryW(argv[1]);
    printf("LoadLibrary -> %p (%lu ms, error %lu)\n", (void *)m, GetTickCount() - t0, m ? 0 : GetLastError());
    if (!m)
        return 1;
    g_stage = 1;
    BOOL(*init)(void) = (BOOL(*)(void))(void *)GetProcAddress(m, "InitDll");
    if (init)
        printf("InitDll -> %d\n", init());
    g_stage = 2;
    IPluginFactory *(*get)(void) = (IPluginFactory * (*)(void))(void *)GetProcAddress(m, "GetPluginFactory");
    IPluginFactory *f = get ? get() : NULL;
    printf("GetPluginFactory -> %p\n", (void *)f);
    if (!f)
        return 1;
    g_stage = 3;
    int n = f->v->countClasses(f);
    printf("%d classes\n", n);
    g_stage = 4;
    static const char IID_FUnknown[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (char)0xC0, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    // Steinberg::Vst::IComponent {E831FF31-F2D5-4301-928E-BBEE25697802} (COM-compatible byte order on Windows);
    // vtable: FUnknown (3), IPluginBase::initialize(FUnknown *context), terminate()
    static const char IID_IComponent[16] = {0x31, (char)0xFF, 0x31, (char)0xE8, (char)0xD5, (char)0xF2, 0x01, 0x43,
                                            (char)0x92, (char)0x8E, (char)0xBB, (char)0xEE, 0x25, 0x69, 0x78, 0x02};
    for (int i = 0; i < n; i++) {
        PClassInfo ci = {0};
        f->v->getClassInfo(f, i, &ci);
        void *obj = NULL;
        long r = f->v->createInstance(f, ci.cid, IID_FUnknown, &obj);
        printf("  class %d \"%s\" (%s): createInstance -> %ld %p\n", i, ci.name, ci.category, r, obj);
        if (obj)
            ((IUnk *)obj)->v->release(obj);
        if (i == 0) { // a DAW opening the plug-in: IComponent::initialize (settings, device connection)
            void *c = NULL;
            r = f->v->createInstance(f, ci.cid, IID_IComponent, &c);
            printf("  class 0 as IComponent -> %ld %p\n", r, c);
            if (c) {
                g_stage = 5;
                long(__stdcall * *vt)(void *, void *) = *(long(__stdcall * **)(void *, void *))c;
                DWORD t1 = GetTickCount();
                printf("  initialize -> %ld (%lu ms)\n", vt[3](c, &g_host), GetTickCount() - t1);
                fflush(stdout);
                ((long(__stdcall *)(void *, void *))vt[4])(c, NULL); // terminate (2nd arg unused)
            }
        }
    }
    g_stage = 6;
    printf("OK after %lu ms\n", GetTickCount() - t0);
    fflush(stdout);
    ExitProcess(0); // skip DLL unload (hosts keep plug-ins loaded)
}
