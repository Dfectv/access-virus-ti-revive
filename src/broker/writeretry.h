/* writeretry.h - a device write in packets, with retries when a packet times out.
 *
 * Why: the Virus' USB chip NAKs an OUT packet while its 64-byte link ring is full, and that ring drains only as
 * fast as the host CPU acknowledges every byte over a toggle handshake (E239) - the link is lossless, so a timed-out
 * packet was never accepted and a timeout only means the Virus was busy for that long (a patch load, a multi
 * set-up). Dropping the rest of the message there (the old behaviour, E231) broke a SysEx dump or lost a stamped
 * event; retrying the same packet a few times keeps the message intact. A real error stops at once.
 *
 * xfer(ctx, data, n, &done) sends one packet of n bytes: returns 0 when accepted (done = n), VTI_XFER_TIMEOUT when
 * it timed out (done = bytes accepted before that, 0 for an interrupt packet), anything else on error.
 */
#pragma once
#include <stdint.h>

#define VTI_XFER_TIMEOUT 1

typedef int (*vti_xfer_fn)(void *ctx, const uint8_t *data, int n, int *done);

/* Writes len bytes in packets of at most max bytes. A packet is retried after a timeout, up to `attempts` tries in
 * a row; `timeouts` (optional) counts the retried ones. Returns 0 when everything was accepted, -1 otherwise. */
static inline int vti_write_chunks(void *ctx, vti_xfer_fn xfer, const uint8_t *data, int len, int max, int attempts,
                                   int *timeouts)
{
    int tries = 0;
    for (int off = 0; off < len;) {
        int n = len - off < max ? len - off : max, done = 0;
        int r = xfer(ctx, data + off, n, &done);
        if (done > 0)
            off += done, tries = 0;
        if (r == 0) {
            if (done <= 0) /* accepted nothing without an error: never spin */
                return -1;
            continue;
        }
        if (r != VTI_XFER_TIMEOUT)
            return -1;
        if (timeouts)
            (*timeouts)++;
        if (++tries >= attempts)
            return -1;
    }
    return 0;
}
