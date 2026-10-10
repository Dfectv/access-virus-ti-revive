// vticore_winusb.c - Windows backend of vticore.h on the inbox WinUSB driver (src/inf/vti_winusb.inf binds
// WinUSB to MI_03 and MI_04, each with its own device interface GUID). Packing helpers are shared with the
// libusb backend via vticore_midi.c.
#include "vticore.h"
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>
#include <stdlib.h>

static const GUID GUID_MI03 = {0xCF293C20, 0xF175, 0x42AB, {0xB2, 0xBE, 0xA0, 0x2F, 0xEC, 0x94, 0x09, 0x37}};
static const GUID GUID_MI04 = {0x5B45FAF8, 0x316B, 0x4B94, {0xAA, 0xBF, 0xFF, 0x44, 0x2A, 0x85, 0xC5, 0xE7}};
static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE usb;
} iface;

struct vti {
    iface mi[2]; // [0] = MI_03, [1] = MI_04
    vti_rx_cb cb;
    void *user;
    volatile LONG stop;
    HANDLE rd[2];
};

static int open_iface(const GUID *g, iface *out)
{
    HDEVINFO di = SetupDiGetClassDevsW(g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE)
        return -1;
    SP_DEVICE_INTERFACE_DATA id = {.cbSize = sizeof id};
    int ok = -1;
    if (SetupDiEnumDeviceInterfaces(di, NULL, g, 0, &id)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *d = malloc(need);
        d->cbSize = sizeof *d;
        if (SetupDiGetDeviceInterfaceDetailW(di, &id, d, need, NULL, NULL)) {
            out->file = CreateFileW(d->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
            if (out->file != INVALID_HANDLE_VALUE && WinUsb_Initialize(out->file, &out->usb))
                ok = 0;
            else
                fprintf(stderr, "vti: open %ls failed: %lu\n", d->DevicePath, GetLastError());
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(di);
    return ok;
}

static iface *iface_for(vti *v, uint8_t ep)
{
    return &v->mi[(ep & 0x0f) == 0x06];
}

typedef struct {
    vti *v;
    uint8_t ep;
} rd_arg;

static DWORD WINAPI reader(LPVOID p)
{
    rd_arg a = *(rd_arg *)p;
    free(p);
    iface *f = iface_for(a.v, a.ep);
    ULONG timeout = 200; // ms, so the stop flag is noticed
    WinUsb_SetPipePolicy(f->usb, a.ep, PIPE_TRANSFER_TIMEOUT, sizeof timeout, &timeout);
    uint8_t buf[64];
    while (!a.v->stop) {
        ULONG got = 0;
        if (WinUsb_ReadPipe(f->usb, a.ep, buf, sizeof buf, &got, NULL) && got)
            a.v->cb(a.v->user, a.ep, buf, (int)got);
        else {
            DWORD e = GetLastError();
            if (e != ERROR_SEM_TIMEOUT && e != 0) {
                fprintf(stderr, "vti: read ep %02x: error %lu\n", a.ep, e);
                if (e == ERROR_BAD_COMMAND || e == ERROR_DEVICE_NOT_CONNECTED || e == ERROR_INVALID_HANDLE)
                    break;
            }
        }
    }
    return 0;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    vti *v = calloc(1, sizeof *v);
    v->cb = cb;
    v->user = user;
    if (open_iface(&GUID_MI03, &v->mi[0]) || open_iface(&GUID_MI04, &v->mi[1])) {
        fprintf(stderr, "vti: Virus TI WinUSB interfaces not found (is vti_winusb.inf installed?)\n");
        vti_close(v);
        return NULL;
    }
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE)) {
        vti_close(v);
        return NULL;
    }
    uint8_t eps[2] = {VTI_EP_MI03_IN, VTI_EP_MI04_IN};
    for (int i = 0; i < 2; i++) {
        rd_arg *a = malloc(sizeof *a);
        a->v = v;
        a->ep = eps[i];
        v->rd[i] = CreateThread(NULL, 0, reader, a, 0, NULL);
    }
    return v;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    v->stop = 1;
    for (int i = 0; i < 2; i++)
        if (v->rd[i]) {
            WaitForSingleObject(v->rd[i], 2000);
            CloseHandle(v->rd[i]);
        }
    for (int i = 0; i < 2; i++) {
        if (v->mi[i].usb)
            WinUsb_Free(v->mi[i].usb);
        if (v->mi[i].file && v->mi[i].file != INVALID_HANDLE_VALUE)
            CloseHandle(v->mi[i].file);
    }
    free(v);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    iface *f = iface_for(v, ep);
    ULONG done = 0;
    // WinUSB splits into wMaxPacketSize packets itself.
    if (!WinUsb_WritePipe(f->usb, ep, (PUCHAR)data, (ULONG)len, &done, NULL) || (int)done != len) {
        fprintf(stderr, "vti: write ep %02x: error %lu\n", ep, GetLastError());
        return -1;
    }
    return 0;
}

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    WINUSB_SETUP_PACKET sp = {bmRequestType, bRequest, wValue, wIndex, len};
    ULONG done = 0;
    // Issued through the MI_04 handle; WinUSB may rewrite wIndex for interface-recipient requests (E040).
    if (!WinUsb_ControlTransfer(v->mi[1].usb, sp, data, len, &done, NULL)) {
        fprintf(stderr, "vti: control %02x/%02x wIndex %04x: error %lu\n", bmRequestType, bRequest, wIndex,
                GetLastError());
        return -1;
    }
    return (int)done;
}
