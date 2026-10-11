// vtiwrap.c - "Virus TI (TI Revive)" VST3: loads the user's installed Access "Virus TI.vst3" with the IOCTL
// emulation (vtiemu.c) in place, so the original plugin talks to the Virus TI over WinUSB (findings/design.md,
// milestone M2). No Access files are modified or shipped.
//
// Only the plugin factory is wrapped: class IDs get one byte XORed (so the DAW does not see duplicates of the
// original plugin, which stays installed) and names get a " (WinUSB)" suffix; createInstance maps IDs back.
// All component/controller objects are the original plugin's own.
#include <windows.h>
#include <shlobj.h>
#include <stdint.h>
#include <string.h>

void vtiemu_init(HMODULE self);
void vtiemu_patch_access(void);

// ---- minimal VST3 ABI (pluginterfaces/base/ipluginbase.h; Windows x64, COM-compatible layout) ----
typedef int32_t tresult;
typedef char TUID[16];
typedef struct {
    char vendor[64], url[256], email[128];
    int32_t flags;
} PFactoryInfo;
typedef struct {
    TUID cid;
    int32_t cardinality;
    char category[32], name[64];
} PClassInfo;
typedef struct {
    TUID cid;
    int32_t cardinality;
    char category[32], name[64];
    uint32_t classFlags;
    char subCategories[128], vendor[64], version[64], sdkVersion[64];
} PClassInfo2;
typedef struct {
    TUID cid;
    int32_t cardinality;
    char category[32];
    uint16_t name[64];
    uint32_t classFlags;
    char subCategories[128];
    uint16_t vendor[64], version[64], sdkVersion[64];
} PClassInfoW;

typedef struct factory factory;
typedef struct {
    tresult (*queryInterface)(factory *, const TUID, void **);
    uint32_t (*addRef)(factory *);
    uint32_t (*release)(factory *);
    tresult (*getFactoryInfo)(factory *, PFactoryInfo *);
    int32_t (*countClasses)(factory *);
    tresult (*getClassInfo)(factory *, int32_t, PClassInfo *);
    tresult (*createInstance)(factory *, const char *cid, const char *iid, void **);
    tresult (*getClassInfo2)(factory *, int32_t, PClassInfo2 *); // IPluginFactory2
    tresult (*getClassInfoUnicode)(factory *, int32_t, PClassInfoW *); // IPluginFactory3
    tresult (*setHostContext)(factory *, void *);
} factory_vtbl;
struct factory {
    const factory_vtbl *vt;
};

#define CID_XOR 0x5a // applied to byte 15 of every class ID
static const char SUFFIX[] = " (TI Revive)";

typedef struct {
    factory base;   // our object, IPluginFactory3 layout
    factory *orig;  // the original plugin's factory
    volatile LONG refs;
} wrapper;

static void map_cid(char *cid)
{
    cid[15] ^= CID_XOR;
}

static void suffix_a(char *s, size_t cap)
{
    if (strlen(s) + sizeof SUFFIX <= cap)
        strcat(s, SUFFIX);
}

static void suffix_w(uint16_t *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n])
        n++;
    if (n + sizeof SUFFIX <= cap)
        for (size_t i = 0; i < sizeof SUFFIX; i++)
            s[n + i] = (uint16_t)SUFFIX[i];
}

static tresult w_query(factory *f, const TUID iid, void **obj)
{
    wrapper *w = (wrapper *)f;
    void *p = NULL;
    tresult r = w->orig->vt->queryInterface(w->orig, iid, &p);
    if (r == 0 && p) { // same factory level available in the original: hand out ourselves
        ((factory *)p)->vt->release(p);
        InterlockedIncrement(&w->refs);
        *obj = w;
    } else {
        *obj = NULL;
    }
    return r;
}

static uint32_t w_addref(factory *f)
{
    return (uint32_t)InterlockedIncrement(&((wrapper *)f)->refs);
}

static uint32_t w_release(factory *f)
{
    return (uint32_t)InterlockedDecrement(&((wrapper *)f)->refs); // static singleton: never freed
}

static tresult w_info(factory *f, PFactoryInfo *i)
{
    return ((wrapper *)f)->orig->vt->getFactoryInfo(((wrapper *)f)->orig, i);
}

static int32_t w_count(factory *f)
{
    return ((wrapper *)f)->orig->vt->countClasses(((wrapper *)f)->orig);
}

static tresult w_class(factory *f, int32_t idx, PClassInfo *i)
{
    factory *o = ((wrapper *)f)->orig;
    tresult r = o->vt->getClassInfo(o, idx, i);
    if (r == 0) {
        map_cid(i->cid);
        suffix_a(i->name, sizeof i->name);
    }
    return r;
}

static tresult w_class2(factory *f, int32_t idx, PClassInfo2 *i)
{
    factory *o = ((wrapper *)f)->orig;
    tresult r = o->vt->getClassInfo2(o, idx, i);
    if (r == 0) {
        map_cid(i->cid);
        suffix_a(i->name, sizeof i->name);
    }
    return r;
}

static tresult w_classw(factory *f, int32_t idx, PClassInfoW *i)
{
    factory *o = ((wrapper *)f)->orig;
    tresult r = o->vt->getClassInfoUnicode(o, idx, i);
    if (r == 0) {
        map_cid(i->cid);
        suffix_w(i->name, sizeof i->name / sizeof *i->name);
    }
    return r;
}

// Accept our mapped IDs (from getClassInfo*) and the original ones (the processor reports its controller
// by the original ID).
static tresult w_create(factory *f, const char *cid, const char *iid, void **obj)
{
    factory *o = ((wrapper *)f)->orig;
    int32_t n = o->vt->countClasses(o);
    for (int32_t i = 0; i < n; i++) {
        PClassInfo ci;
        if (o->vt->getClassInfo(o, i, &ci) != 0)
            continue;
        char mapped[16];
        memcpy(mapped, ci.cid, 16);
        map_cid(mapped);
        if (!memcmp(cid, mapped, 16))
            return o->vt->createInstance(o, ci.cid, iid, obj);
    }
    return o->vt->createInstance(o, cid, iid, obj);
}

static tresult w_host(factory *f, void *ctx)
{
    return ((wrapper *)f)->orig->vt->setHostContext(((wrapper *)f)->orig, ctx);
}

static const factory_vtbl VT = {w_query, w_addref, w_release, w_info, w_count, w_class,
                                w_create, w_class2, w_classw, w_host};

static wrapper g_wrap = {{&VT}, NULL, 1};
static HMODULE g_self, g_orig;
static tresult (*orig_init)(void), (*orig_exit)(void);

// Load the original plugin with the emulation installed first. Path: %VTIWRAP_ORIGINAL% or
// <Common Files>\VST3\Virus TI.vst3.
static int load_original(void)
{
    if (g_orig)
        return 1;
    vtiemu_init(g_self);
    wchar_t path[MAX_PATH];
    if (!GetEnvironmentVariableW(L"VTIWRAP_ORIGINAL", path, MAX_PATH)) {
        PWSTR common = NULL;
        if (SHGetKnownFolderPath(&FOLDERID_ProgramFilesCommon, 0, NULL, &common) != S_OK)
            return 0;
        lstrcpynW(path, common, MAX_PATH - 32);
        lstrcatW(path, L"\\VST3\\Virus TI.vst3");
        CoTaskMemFree(common);
    }
    g_orig = LoadLibraryW(path);
    if (!g_orig)
        return 0;
    vtiemu_patch_access(); // Virus TI.vst3 loads Virus Control.dll in its DllMain (E042)
    orig_init = (void *)GetProcAddress(g_orig, "InitDll");
    orig_exit = (void *)GetProcAddress(g_orig, "ExitDll");
    if (orig_init)
        orig_init();
    return 1;
}

__declspec(dllexport) BOOL InitDll(void)
{
    return load_original();
}

__declspec(dllexport) BOOL ExitDll(void)
{
    if (orig_exit)
        orig_exit();
    return TRUE;
}

__declspec(dllexport) void *GetPluginFactory(void)
{
    if (!load_original())
        return NULL;
    if (!g_wrap.orig) {
        factory *(*get)(void) = (void *)GetProcAddress(g_orig, "GetPluginFactory");
        g_wrap.orig = get ? get() : NULL;
        if (!g_wrap.orig)
            return NULL;
    }
    InterlockedIncrement(&g_wrap.refs);
    return &g_wrap;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        g_self = inst;
    }
    return TRUE;
}
