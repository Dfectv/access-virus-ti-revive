// vtiasio.c - "Virus TI (TI Revive)" ASIO driver: a COM in-proc server that wraps the user's original Access ASIO
// driver (VirusUSB_X64.dll, Ploytec PGAsioDriver, findings E082) the way the VST3 wrapper wraps the plug-in.
//
// The original talks to VirusUSB.sys through SetupDi + CreateFileA(<device path>\IO_ASIO) + DeviceIoControl:
// input AudioSM mapped with 0x220010, output AudioSM with 0x220030, threaded buffer-switch event 0x2200E4 - the
// same interface as the plug-in. DllGetClassObject loads it, points its imports at our IOCTL emulation (a second
// copy of vtiemu.c in this DLL, patching only that module; the audio comes from vtibroker's shared ring) and
// returns the original's class factory, so the DAW gets the original IASIO object.
// Registration: HKLM\SOFTWARE\ASIO\Virus TI (TI Revive) -> CLSID_TIREVIVE_ASIO -> this DLL (MSI or regsvr32).
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

void vtiemu_init_log(void);              // vtiemu.c
void vtiemu_init_module(HMODULE target);
void vtiemu_log(const char *fmt, ...);

// {6FD63640-1E0A-4EE0-9158-732A1077C8B5}
static const CLSID CLSID_TIREVIVE_ASIO = {0x6fd63640, 0x1e0a, 0x4ee0, {0x91, 0x58, 0x73, 0x2a, 0x10, 0x77, 0xc8, 0xb5}};
// {0FA56CA7-26E1-4FC3-B2CA-ECBBBC9B7C3D} "Virus USB ASIO driver" (Access MSI Registry table)
static const CLSID CLSID_ACCESS_ASIO = {0x0fa56ca7, 0x26e1, 0x4fc3, {0xb2, 0xca, 0xec, 0xbb, 0xbc, 0x9b, 0x7c, 0x3d}};
#define CLSID_STR L"{6FD63640-1E0A-4EE0-9158-732A1077C8B5}"
#define DRIVER_NAME L"Virus TI (TI Revive)"

static HMODULE g_self, g_orig;

// The original's path from its COM registration, else the Access default locations.
static HMODULE load_original(void)
{
    if (g_orig)
        return g_orig;
    wchar_t path[MAX_PATH];
    DWORD sz = sizeof path;
    int found = !RegGetValueW(HKEY_CLASSES_ROOT, L"CLSID\\{0FA56CA7-26E1-4FC3-B2CA-ECBBBC9B7C3D}\\InprocServer32", NULL,
                              RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, path, &sz) &&
                GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    static const wchar_t *const rel[] = {L"\\Access Music\\Virus TI\\Common\\VirusUSB_X64.dll",
                                         L"\\Common Files\\VirusUSB_X64.dll"};
    wchar_t pf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH);
    for (int i = 0; !found && n && n < MAX_PATH && i < 2; i++) {
        _snwprintf(path, MAX_PATH, L"%ls%ls", pf, rel[i]);
        found = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!found) {
        vtiemu_log("asio: original VirusUSB_X64.dll not found (Access software not installed?)");
        return NULL;
    }
    g_orig = LoadLibraryW(path);
    char a[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, a, sizeof a, NULL, NULL);
    vtiemu_log("asio: loaded %s -> %p", a, (void *)g_orig);
    if (g_orig)
        vtiemu_init_module(g_orig);
    return g_orig;
}

// ASIO hosts create the driver with riid = the driver's CLSID (the IASIO "interface id" is the class id), and the
// original object only answers its own CLSID. This factory forwards to the original's and maps that one riid.
typedef struct {
    IClassFactory iface;
    IClassFactory *orig;
    LONG refs;
} factory;

static HRESULT STDMETHODCALLTYPE f_qi(IClassFactory *This, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE f_addref(IClassFactory *This)
{
    return (ULONG)InterlockedIncrement(&((factory *)This)->refs);
}
static ULONG STDMETHODCALLTYPE f_release(IClassFactory *This)
{
    factory *f = (factory *)This;
    LONG r = InterlockedDecrement(&f->refs);
    if (!r) {
        f->orig->lpVtbl->Release(f->orig);
        free(f);
    }
    return (ULONG)r;
}
// COM also queries the new object for riid = our CLSID (CoCreateInstance creates for IUnknown, then QIs), which
// the original's QueryInterface only knows as its own CLSID: its (shared, read-only) vtable slot 0 is replaced
// once with a QueryInterface that maps our CLSID to the original's.
typedef HRESULT(STDMETHODCALLTYPE *qi_fn)(IUnknown *, REFIID, void **);
static qi_fn g_orig_qi;

static HRESULT STDMETHODCALLTYPE obj_qi(IUnknown *This, REFIID riid, void **ppv)
{
    return g_orig_qi(This, IsEqualIID(riid, &CLSID_TIREVIVE_ASIO) ? &CLSID_ACCESS_ASIO : riid, ppv);
}

// IASIO::canSampleRate (vtable slot 12): the original decides from a list of Ploytec VID/PIDs the Virus is not on
// and then from descriptor tables it parses out of the replayed configuration (E084), answering ASE_NoClock even for
// 44100, so Ableton reported "device not available". The stream runs at 44.1 kHz only: answer exactly that.
#define ASE_OK 0
#define ASE_NOCLOCK (-995)
static long can_sample_rate(void *self, double rate)
{
    (void)self;
    return rate == 44100.0 ? ASE_OK : ASE_NOCLOCK;
}

static void set_slot(void **vt, int i, void *fn, void **orig)
{
    DWORD old;
    if (vt[i] != fn && VirtualProtect(&vt[i], sizeof(void *), PAGE_READWRITE, &old)) {
        if (orig)
            *orig = vt[i];
        vt[i] = fn;
        VirtualProtect(&vt[i], sizeof(void *), old, &old);
    }
}

static void hook_object_qi(IUnknown *obj)
{
    void **vt = *(void ***)obj;
    if (vt[0] == (void *)obj_qi)
        return;
    set_slot(vt, 0, (void *)obj_qi, (void **)&g_orig_qi);
    set_slot(vt, 12, (void *)can_sample_rate, NULL);
}

static HRESULT STDMETHODCALLTYPE f_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **ppv)
{
    IClassFactory *o = ((factory *)This)->orig;
    HRESULT hr = o->lpVtbl->CreateInstance(o, outer, IsEqualIID(riid, &CLSID_TIREVIVE_ASIO) ? &CLSID_ACCESS_ASIO : riid,
                                           ppv);
    if (SUCCEEDED(hr) && *ppv)
        hook_object_qi((IUnknown *)*ppv);
    vtiemu_log("asio: CreateInstance -> 0x%08lx", (unsigned long)hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE f_lock(IClassFactory *This, BOOL lock)
{
    IClassFactory *o = ((factory *)This)->orig;
    return o->lpVtbl->LockServer(o, lock);
}
static IClassFactoryVtbl g_factory_vtbl = {f_qi, f_addref, f_release, f_create, f_lock};

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (!IsEqualCLSID(rclsid, &CLSID_TIREVIVE_ASIO))
        return CLASS_E_CLASSNOTAVAILABLE;
    vtiemu_init_log();
    HMODULE m = load_original();
    HRESULT(WINAPI * get)(REFCLSID, REFIID, void **) =
        m ? (HRESULT(WINAPI *)(REFCLSID, REFIID, void **))(void *)GetProcAddress(m, "DllGetClassObject") : NULL;
    if (!get)
        return CLASS_E_CLASSNOTAVAILABLE;
    IClassFactory *orig = NULL;
    HRESULT hr = get(&CLSID_ACCESS_ASIO, &IID_IClassFactory, (void **)&orig);
    vtiemu_log("asio: original DllGetClassObject -> 0x%08lx", (unsigned long)hr);
    if (FAILED(hr))
        return hr;
    factory *f = calloc(1, sizeof *f);
    if (!f) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    f->iface.lpVtbl = &g_factory_vtbl;
    f->orig = orig;
    f->refs = 1;
    hr = f_qi(&f->iface, riid, ppv);
    f_release(&f->iface);
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE; // the original stays loaded with patched imports for the life of the process
}

static LONG set_str(HKEY root, const wchar_t *key, const wchar_t *name, const wchar_t *val)
{
    return RegSetKeyValueW(root, key, name, REG_SZ, val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
}

__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(g_self, self, MAX_PATH);
    LONG e = set_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_STR, NULL, DRIVER_NAME);
    e |= set_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_STR L"\\InprocServer32", NULL, self);
    e |= set_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_STR L"\\InprocServer32", L"ThreadingModel",
                 L"Apartment");
    e |= set_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\" DRIVER_NAME, L"CLSID", CLSID_STR);
    e |= set_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\" DRIVER_NAME, L"Description", DRIVER_NAME);
    return e ? SELFREG_E_CLASS : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void)
{
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" CLSID_STR);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\" DRIVER_NAME);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
