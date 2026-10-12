// VirusClient.cpp - TI Revive's replacement for Access' libVirusUSB.dylib on macOS (findings/design-macos.md,
// E093-E096). Virus Control.bundle links "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
// and calls these VirusClient methods; the class itself (vtable, typeinfo, data members) is defined in Virus Control,
// so only the non-virtual member functions are provided here, with the exact signatures (= the same mangled names).
//
// Stage 1 / POC step 1: logging stub. Every call is logged with its arguments to /tmp/tirevive-libvirususb.log and
// answered as "library present, no device", so Virus Control loads and its call sequence can be observed. The real
// implementation (broker connection, Core Audio IOProc, callbacks) replaces the bodies step by step.
//
// Build (on the Mac, x86_64 for the Rosetta process that hosts the Intel-only Access plug-in):
//   clang++ -arch x86_64 -dynamiclib -std=c++17 -O2 -install_name \
//     "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib" -o libVirusUSB.dylib VirusClient.cpp
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <unistd.h>

// Types as used in the mangled names of the Access library (sizes from the decompiled code)
enum EReqDirection : int {};
enum EReqLevel : int {};
enum EReqRecipient : int {};
enum EReqRequest : int {};
enum E_VC_INT_PARAM : int {};
struct VC_DeviceInfo;       // 0x214 bytes: u16, u8, u16 x3, u8 x4, u32, char[256] x2 (E094)
struct VC_ClientInfoList;

class VirusClient {
  public:
    int vc_init();
    int vc_term();
    int vc_open(void *deviceID);
    int vc_close();
    bool vc_isOpen();
    int vc_start(unsigned long framesize);
    int vc_stop();
    unsigned long vc_getNumDevices();
    void *vc_getDeviceID(unsigned long index);
    void *vc_getCurrentDeviceID();
    bool vc_getDeviceInfo(unsigned long index, VC_DeviceInfo &info);
    bool vc_request(EReqDirection dir, EReqLevel level, EReqRecipient recipient, EReqRequest request,
                    unsigned short value, unsigned short index, char *data, unsigned short length);
    unsigned long vc_getBuffersize();
    unsigned long vc_getSamplerate();
    bool vc_setSamplerate(unsigned long rate);
    bool vc_getLatencies(unsigned long &in, unsigned long &out);
    long vc_getIntParam(E_VC_INT_PARAM param);
    long vc_setIntParam(E_VC_INT_PARAM param, long value);
    bool vc_isPluginMaster();
    unsigned long vc_getNumOfHubs();
    VC_ClientInfoList *vc_getClientInfo();
    void vc_freeClientInfo(VC_ClientInfoList *list);
    long vc_getConfigStatus() const;
    unsigned long vc_getLibraryVersion() const;
    unsigned long vc_getAudioDriverVersion() const;
    unsigned long vc_getHostInterfaceVersion() const;
    unsigned long vc_getBandwidthAvailable() const;
};

static void vlog(const char *fmt, ...)
{
    static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mx);
    FILE *f = fopen("/tmp/tirevive-libvirususb.log", "a");
    if (f) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(f, "%ld.%03ld pid %d ", (long)ts.tv_sec % 100000, ts.tv_nsec / 1000000, getpid());
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&mx);
}

__attribute__((constructor)) static void loaded() { vlog("TI Revive libVirusUSB (stub) loaded"); }

int VirusClient::vc_init() { vlog("%p vc_init", this); return 1; }
int VirusClient::vc_term() { vlog("%p vc_term", this); return 1; }
int VirusClient::vc_open(void *id) { vlog("%p vc_open %p", this, id); return 0; }
int VirusClient::vc_close() { vlog("%p vc_close", this); return 1; }
bool VirusClient::vc_isOpen() { vlog("%p vc_isOpen", this); return false; }
int VirusClient::vc_start(unsigned long fs) { vlog("%p vc_start framesize %lu", this, fs); return 0; }
int VirusClient::vc_stop() { vlog("%p vc_stop", this); return 1; }
unsigned long VirusClient::vc_getNumDevices() { vlog("%p vc_getNumDevices -> 0", this); return 0; }
void *VirusClient::vc_getDeviceID(unsigned long i) { vlog("%p vc_getDeviceID %lu", this, i); return nullptr; }
void *VirusClient::vc_getCurrentDeviceID() { vlog("%p vc_getCurrentDeviceID", this); return nullptr; }
bool VirusClient::vc_getDeviceInfo(unsigned long i, VC_DeviceInfo &) { vlog("%p vc_getDeviceInfo %lu", this, i); return false; }
bool VirusClient::vc_request(EReqDirection d, EReqLevel l, EReqRecipient r, EReqRequest q, unsigned short v,
                             unsigned short x, char *data, unsigned short n)
{
    vlog("%p vc_request dir %d level %d recipient %d request %d value 0x%04x index 0x%04x len %u data %p", this, d, l,
         r, q, v, x, n, (void *)data);
    return false;
}
unsigned long VirusClient::vc_getBuffersize() { vlog("%p vc_getBuffersize", this); return 0; }
unsigned long VirusClient::vc_getSamplerate() { vlog("%p vc_getSamplerate", this); return 44100; }
bool VirusClient::vc_setSamplerate(unsigned long r) { vlog("%p vc_setSamplerate %lu", this, r); return false; }
bool VirusClient::vc_getLatencies(unsigned long &in, unsigned long &out)
{
    vlog("%p vc_getLatencies", this);
    in = out = 0;
    return false;
}
long VirusClient::vc_getIntParam(E_VC_INT_PARAM p) { vlog("%p vc_getIntParam %d", this, p); return -1; }
long VirusClient::vc_setIntParam(E_VC_INT_PARAM p, long v) { vlog("%p vc_setIntParam %d %ld", this, p, v); return -1; }
bool VirusClient::vc_isPluginMaster() { vlog("%p vc_isPluginMaster", this); return true; }
unsigned long VirusClient::vc_getNumOfHubs() { vlog("%p vc_getNumOfHubs", this); return 0; }
VC_ClientInfoList *VirusClient::vc_getClientInfo() { vlog("%p vc_getClientInfo", this); return nullptr; }
void VirusClient::vc_freeClientInfo(VC_ClientInfoList *l) { vlog("%p vc_freeClientInfo %p", this, (void *)l); }
long VirusClient::vc_getConfigStatus() const { vlog("%p vc_getConfigStatus", this); return 0; }
unsigned long VirusClient::vc_getLibraryVersion() const { vlog("%p vc_getLibraryVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getAudioDriverVersion() const { vlog("%p vc_getAudioDriverVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getHostInterfaceVersion() const { vlog("%p vc_getHostInterfaceVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getBandwidthAvailable() const { vlog("%p vc_getBandwidthAvailable", this); return 1; }
