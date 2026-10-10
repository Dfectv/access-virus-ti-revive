// vticore.c - see vticore.h
#include "vticore.h"
#include <libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

struct vti {
    libusb_context *ctx;
    libusb_device_handle *h;
    vti_rx_cb cb;
    void *user;
    volatile int stop;
    pthread_t rd[2];
    uint8_t rd_ep[2];
};

typedef struct {
    vti *v;
    int i;
} rd_arg;

static void *reader(void *p)
{
    rd_arg a = *(rd_arg *)p;
    free(p);
    vti *v = a.v;
    uint8_t ep = v->rd_ep[a.i], buf[64];
    while (!v->stop) {
        int got = 0, r = libusb_interrupt_transfer(v->h, ep, buf, sizeof buf, &got, 200);
        if (r == 0 && got > 0)
            v->cb(v->user, ep, buf, got);
        else if (r != LIBUSB_ERROR_TIMEOUT && r != 0) {
            fprintf(stderr, "vti: read ep %02x: %s\n", ep, libusb_error_name(r));
            if (r == LIBUSB_ERROR_NO_DEVICE)
                break;
        }
    }
    return NULL;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    vti *v = calloc(1, sizeof *v);
    v->cb = cb;
    v->user = user;
    int r = libusb_init(&v->ctx);
    if (r) {
        fprintf(stderr, "vti: libusb_init: %s\n", libusb_error_name(r));
        free(v);
        return NULL;
    }
    v->h = libusb_open_device_with_vid_pid(v->ctx, VTI_VID, VTI_PID);
    if (!v->h) {
        fprintf(stderr, "vti: Virus TI %04x:%04x not found or no access\n", VTI_VID, VTI_PID);
        goto fail;
    }
    libusb_set_auto_detach_kernel_driver(v->h, 1);
    for (int i = 3; i <= 4; i++)
        if ((r = libusb_claim_interface(v->h, i))) {
            fprintf(stderr, "vti: claim interface %d: %s\n", i, libusb_error_name(r));
            goto fail;
        }
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE))
        goto fail;
    v->rd_ep[0] = VTI_EP_MI03_IN;
    v->rd_ep[1] = VTI_EP_MI04_IN;
    for (int i = 0; i < 2; i++) {
        rd_arg *a = malloc(sizeof *a);
        a->v = v;
        a->i = i;
        pthread_create(&v->rd[i], NULL, reader, a);
    }
    return v;
fail:
    if (v->h)
        libusb_close(v->h);
    libusb_exit(v->ctx);
    free(v);
    return NULL;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    v->stop = 1;
    for (int i = 0; i < 2; i++)
        pthread_join(v->rd[i], NULL);
    for (int i = 3; i <= 4; i++)
        libusb_release_interface(v->h, i);
    libusb_close(v->h);
    libusb_exit(v->ctx);
    free(v);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    int max = ep == VTI_EP_MI03_OUT ? 8 : 32; // wMaxPacketSize (lsusb -v)
    for (int off = 0; off < len;) {
        int n = len - off < max ? len - off : max, done = 0;
        int r = libusb_interrupt_transfer(v->h, ep, (uint8_t *)data + off, n, &done, 1000);
        if (r) {
            fprintf(stderr, "vti: write ep %02x: %s\n", ep, libusb_error_name(r));
            return -1;
        }
        off += done;
    }
    return 0;
}

int vti_pack_sysex(uint8_t cable, const uint8_t *msg, int len, uint8_t *out, int outsz)
{
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        if (o + 4 > outsz)
            return -1;
        int rest = len - i;
        uint8_t cin = rest > 3 ? 0x4 : rest == 3 ? 0x7 : rest == 2 ? 0x6 : 0x5; // continue / end with 3,2,1
        out[o] = (uint8_t)(cable << 4 | cin);
        for (int k = 0; k < 3; k++)
            out[o + 1 + k] = k < rest && (cin == 0x4 || k < cin - 4) ? msg[i + k] : 0;
        o += 4;
    }
    return o;
}

void vti_unpack(vti_unpacker *u, const uint8_t *data, int len, vti_sysex_cb done, vti_event_cb ev, void *user)
{
    static const int cin_len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    for (int i = 0; i + 4 <= len; i += 4) {
        uint8_t cable = data[i] >> 4, cin = data[i] & 0xf;
        if (!data[i])
            continue;
        const uint8_t *e = data + i + 1;
        int n = cin_len[cin];
        if (cin >= 0x4 && cin <= 0x7) { // SysEx start/continue (4) or end (5, 6, 7)
            for (int k = 0; k < n && u->len[cable] < (int)sizeof u->buf[0]; k++)
                u->buf[cable][u->len[cable]++] = e[k];
            if (cin != 0x4) {
                if (done)
                    done(user, cable, u->buf[cable], u->len[cable]);
                u->len[cable] = 0;
            }
        } else if (ev) {
            ev(user, cable, e, n);
        }
    }
}
