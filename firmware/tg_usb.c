/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments - Felucca's src/usb.c (via MOTES 0.5)
 * Tank-G changes (GerbilHamster Records, 2026): cut down to class-compliant USB-MIDI only (no CDC, no USB audio,
 * no OTA), our own names and PID, polled from the main loop instead of a timer ISR, and two SysEx commands:
 *   F0 22 24 35 7D F7            (the M-VAVE "soft key")       -> tg_usb.uboot_req: the main loop goes to the loader
 *   F0 00 32 45 00 00 00 40 7F F7 (the M-VAVE identity request) -> we answer in the factory's format with our name,
 *                                                                 so the tools can tell our firmware is running.
 * Included into main.c (one translation unit). Needs: delay_fed(), and the ring publish barrier below. */
#include "tg_usb_hw.h"

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

enum { S_FADDR = 0, S_POWER = 1, S_INTRTX1 = 2, S_INTRTX2 = 3, S_INTRRX1 = 4, S_INTRRX2 = 5, S_INTRUSB = 6,
       S_INTRTX1E = 7, S_INTRTX2E = 8, S_INTRRX1E = 9, S_INTRRX2E = 10, S_INTRUSBE = 11, S_FRAME1 = 12,
       S_FRAME2 = 13, S_INDEX = 14,
       S_TXMAXP = 16, S_CSR0 = 17, S_TXCSR1 = 17, S_TXCSR2 = 18, S_RXMAXP = 19, S_RXCSR1 = 20, S_RXCSR2 = 21,
       S_COUNT0 = 22, S_RXCOUNT1 = 22, S_RXCOUNT2 = 23 };

/* whole 32-byte cache lines each, so flushinv never writes another variable's stale line over DMA data */
static uint8_t ep0buf[96] __attribute__((aligned(32)));
static uint8_t ep4tx[64] __attribute__((aligned(32)));
static uint8_t ep4rx[96] __attribute__((aligned(32)));

static struct {
    uint8_t up, config, pend_addr, has_pend_addr, e0_tx, e0_zlp;
    const uint8_t *e0_src;
    uint16_t e0_left;
    uint32_t resets, setups, rx_pkts, tx_pkts, timeouts, retries, idents, tx_done;
    uint8_t sysex[16];
    uint8_t sx_len, sx_on;
    volatile uint8_t uboot_req;
    uint8_t rx_pend;
} tg_usb;

/* MIDI rings: 4-byte USB-MIDI event packets (in: kept for later use; out: our replies) */
#define MQ 64u
static uint32_t midi_in_q[MQ], midi_out_q[MQ];
static volatile uint32_t mi_w, mi_r, mo_w, mo_r;
static void midi_out_event(uint32_t pkt)
{
    if (tg_usb.config && mo_w - mo_r < MQ) {
        midi_out_q[mo_w % MQ] = pkt;
        RING_PUBLISH();
        mo_w++;
    }
}
/* a whole SysEx message (F0 .. F7) as USB-MIDI packets: CIN 4 = 3 bytes that go on, CIN 5/6/7 = the last 1/2/3 */
static void midi_out_sysex(const uint8_t *m, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t left = n - i, k = left > 3u ? 3u : left, cin;
        uint32_t b0 = m[i], b1 = k > 1u ? m[i + 1] : 0, b2 = k > 2u ? m[i + 2] : 0;
        cin = left > 3u ? 4u : (k == 1u ? 5u : k == 2u ? 6u : 7u);
        midi_out_event(cin | b0 << 8 | b1 << 16 | b2 << 24);
        i += k;
    }
}

/* ------------------------------------------------------- descriptors --- */
#define TG_USB_PID 0x000Au     /* pid.codes test VID 0x1209, our own test PID (MOTES uses 0x0001) */
#define CFG_LEN 101
static const uint8_t DEV_DESC[18] = {18, 1, 0x10, 0x01, 0, 0, 0, 64, 0x09, 0x12, TG_USB_PID & 0xFF, TG_USB_PID >> 8,
                                     0x02, 0x00, 1, 2, 0, 1};
static const uint8_t CFG_DESC[] = {
    9, 2, CFG_LEN & 0xFF, CFG_LEN >> 8, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 1, 1, 0, 0,
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,
    9, 4, 1, 0, 2, 1, 3, 0, 0,
    7, 0x24, 1, 0x00, 0x01, 0x41, 0x00,
    6, 0x24, 2, 1, 1, 0,
    6, 0x24, 2, 2, 2, 0,
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,
    9, 5, 0x04, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, 0x84, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 3,
};
typedef char cfg_len_ok[sizeof CFG_DESC == CFG_LEN ? 1 : -1];
static const uint8_t STR0[4] = {4, 3, 0x09, 0x04};
static const uint8_t STR1[] = {44, 3, 'G', 0, 'e', 0, 'r', 0, 'b', 0, 'i', 0, 'l', 0, 'H', 0, 'a', 0, 'm', 0, 's', 0, 't', 0,
                               'e', 0, 'r', 0, ' ', 0, 'R', 0, 'e', 0, 'c', 0, 'o', 0, 'r', 0, 'd', 0, 's', 0};
static const uint8_t STR2[] = {22, 3, 'T', 0, 'a', 0, 'n', 0, 'k', 0, '-', 0, 'G', 0, ' ', 0, 'G', 0, 'H', 0, 'R', 0};
typedef char str1_ok[sizeof STR1 == 44 ? 1 : -1];
typedef char str2_ok[sizeof STR2 == 22 ? 1 : -1];

static int get_desc(uint32_t wvalue, const uint8_t **d, uint16_t *l)
{
    switch (wvalue >> 8) {
    case 1: *d = DEV_DESC; *l = sizeof DEV_DESC; return 1;
    case 2: *d = CFG_DESC; *l = sizeof CFG_DESC; return 1;
    case 3:
        switch (wvalue & 0xFFu) {
        case 0: *d = STR0; *l = sizeof STR0; return 1;
        case 1: *d = STR1; *l = sizeof STR1; return 1;
        case 2: *d = STR2; *l = sizeof STR2; return 1;
        default: return 0;
        }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------- the SIE --- */
static void sie_wr(uint32_t r, uint32_t v)
{
    uint32_t n = 20000;
    if (!fm1_usb_sie_on())
        return;
    fm1_usb_sie_wr_start(r, v);
    while (!fm1_usb_sie_done() && --n)
        ;
    if (n)
        tg_usb.timeouts = 0;
    else if (++tg_usb.timeouts > 50u)
        tg_usb.up = 0;
}

static uint32_t sie_rd(uint32_t r)
{
    uint32_t n = 20000;
    if (!fm1_usb_sie_on())
        return 0;
    fm1_usb_sie_rd_start(r);
    while (!fm1_usb_sie_done())
        if (!--n) {
            if (++tg_usb.timeouts > 50u)
                tg_usb.up = 0;
            return 0;
        }
    tg_usb.timeouts = 0;
    return fm1_usb_sie_data();
}

/* 0.5: MIDI moved from EP1 to EP4, the endpoint the factory firmware uses (EP1 IN never went out after a real
   power-up - EP1 is the ROM loader's disk endpoint and only worked with the loader's leftovers). Same values as the
   factory's usb_g_ep_config: TXMAXP 0xFF, TXCSR 0x0048, INTRTXE |= 1<<ep; RXMAXP 0xFF, RXCSR 0x0090, INTRRXE |= 1<<ep. */
#define MIDI_EP 4u
static void ep_midi_config(void)
{
    fm1_usb_ep4_txbuf(ep4tx);
    FM1_USB_EP4_RADR = (uint32_t)(uintptr_t)ep4rx;
    sie_wr(S_INDEX, MIDI_EP);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0);
    sie_wr(S_RXMAXP, 0xFF);
    sie_wr(S_RXCSR1, 0x90);
    sie_wr(S_RXCSR2, 0);
    sie_wr(S_INTRTX1E, 0x01u | (1u << MIDI_EP));
    sie_wr(S_INTRRX1E, 1u << MIDI_EP);
    fm1_usb_ep_enable(1u << MIDI_EP);
}

static void e0_chunk(void)
{
    uint32_t n = tg_usb.e0_left > 64u ? 64u : tg_usb.e0_left, i;
    int last;
    for (i = 0; i < n; i++)
        ep0buf[i] = tg_usb.e0_src[i];
    tg_dc_flushinv(ep0buf, 64);
    fm1_usb_ep0_send(ep0buf, n);
    tg_usb.e0_src += n;
    tg_usb.e0_left = (uint16_t)(tg_usb.e0_left - n);
    last = tg_usb.e0_left == 0 && !(n == 64u && tg_usb.e0_zlp);
    if (tg_usb.e0_left == 0 && n == 64u)
        tg_usb.e0_zlp = 0;
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, last ? 0x0A : 0x02);
    if (last)
        tg_usb.e0_tx = 0;
}

static void e0_send(const uint8_t *p, uint16_t len, uint16_t wlen)
{
    if (len > wlen)
        len = wlen;
    tg_usb.e0_src = p;
    tg_usb.e0_left = len;
    tg_usb.e0_zlp = (len < wlen) && (len % 64u == 0);
    tg_usb.e0_tx = 1;
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x40);
    e0_chunk();
}

static void ep0_service(void)
{
    static const uint8_t zero2[2];
    uint32_t csr, i;
    uint16_t wvalue, wlength;
    uint8_t s[8];
    if (tg_usb.has_pend_addr) {
        sie_wr(S_FADDR, tg_usb.pend_addr);
        tg_usb.has_pend_addr = 0;
    }
    sie_wr(S_INDEX, 0);
    csr = sie_rd(S_CSR0);
    if (csr & 0x04u) {                                  /* SentStall */
        sie_wr(S_CSR0, 0);
        tg_usb.e0_tx = 0;
        return;
    }
    if (csr & 0x10u) {                                  /* SetupEnd: the host abandoned the transfer */
        sie_wr(S_CSR0, 0x80);
        tg_usb.e0_tx = 0;
    }
    if (tg_usb.e0_tx) {
        if (!(csr & 0x02u))
            e0_chunk();
        return;
    }
    if (!(csr & 0x01u))
        return;
    fm1_usb_rx_sync();
    tg_dc_flushinv(ep0buf, 64);
    for (i = 0; i < 8u; i++)
        s[i] = ep0buf[i];
    tg_usb.setups++;
    wvalue = (uint16_t)(s[2] | s[3] << 8);
    wlength = (uint16_t)(s[6] | s[7] << 8);
    switch ((uint32_t)s[0] << 8 | s[1]) {
    case 0x0005:                                        /* SET_ADDRESS */
        tg_usb.pend_addr = s[2] & 0x7Fu;
        tg_usb.has_pend_addr = 1;
        goto ack;
    case 0x8006: {                                      /* GET_DESCRIPTOR */
        const uint8_t *d;
        uint16_t l;
        if (get_desc(wvalue, &d, &l)) {
            if (!wlength)
                goto ack;
            e0_send(d, l, wlength);
            return;
        }
        goto stall;
    }
    case 0x0009:                                        /* SET_CONFIGURATION: 0 or 1 only */
        if (s[2] > 1u)
            goto stall;
        tg_usb.config = s[2];
        if (tg_usb.config == 1u)
            ep_midi_config();
        else {
            sie_wr(S_INTRRX1E, 0);
            sie_wr(S_INTRTX1E, 0x01);
        }
        goto ack;
    case 0x8008:
        e0_send(&tg_usb.config, 1, wlength);
        return;
    case 0x8000:
    case 0x8100:
    case 0x8200:
        e0_send(zero2, 2, wlength);
        return;
    case 0x010B:                                        /* SET_INTERFACE: alt 0 only */
        if (wvalue == 0)
            goto ack;
        goto stall;
    case 0x810A:
        e0_send(zero2, 1, wlength);
        return;
    case 0x0201: {                                      /* CLEAR_FEATURE(ENDPOINT_HALT): data toggle reset */
        uint32_t ep = s[4] & 0x0Fu;
        if (wvalue != 0 || (ep != 0u && ep != MIDI_EP))
            goto stall;
        if (ep) {
            sie_wr(S_INDEX, ep);
            if (s[4] & 0x80u)
                sie_wr(S_TXCSR1, 0x40);
            else
                sie_wr(S_RXCSR1, 0x80);
        }
        goto ack;
    }
    default:
        goto stall;
    }
ack:
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x48);
    return;
stall:
    sie_wr(S_INDEX, 0);
    sie_wr(S_CSR0, 0x60);
}

/* our answer to the M-VAVE identity request, in the factory's format: 34 bytes (00 59 11 1B 00 00, the name in 27
   bytes, ~sum of those 27) packed 7 bits at a time, low bits first, between F0 and F7 */
static const char TG_ID_NAME[] = "GHR-AMBIENT-0.11 R";      /* + the reset source in hex: R01 = power-up */
static void ident_reply(void)
{
    uint8_t d[34], m[41];
    uint32_t i, acc = 0, nb = 0, k = 1, sum = 0;
    d[0] = 0x00; d[1] = 0x59; d[2] = 0x11; d[3] = 0x1B; d[4] = 0; d[5] = 0;
    for (i = 0; i < 27u; i++) d[6 + i] = i < sizeof TG_ID_NAME - 1u ? (uint8_t)TG_ID_NAME[i] : 0;
    {
        static const char HX[] = "0123456789ABCDEF";
        uint32_t rs = STAT[3] & 0xFFu, at = sizeof TG_ID_NAME - 1u;
        d[6 + at] = (uint8_t)HX[rs >> 4];
        d[7 + at] = (uint8_t)HX[rs & 15u];
    }
    for (i = 0; i < 27u; i++) sum += d[6 + i];
    d[33] = (uint8_t)~sum;
    m[0] = 0xF0;
    for (i = 0; i < 34u; i++) {
        acc |= (uint32_t)d[i] << nb;
        nb += 8u;
        while (nb >= 7u) { m[k++] = (uint8_t)(acc & 0x7Fu); acc >>= 7; nb -= 7u; }
    }
    if (nb) m[k++] = (uint8_t)(acc & 0x7Fu);
    m[k++] = 0xF7;
    midi_out_sysex(m, k);
    tg_usb.idents++;
}

static void sysex_byte(uint8_t b)
{
    static const uint8_t UBOOT_KEY[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    static const uint8_t IDENT_Q[10] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
    if (b >= 0xF8u)
        return;
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        tg_usb.sx_on = 0;
        return;
    }
    if (b == 0xF0) {
        tg_usb.sx_on = 1;
        tg_usb.sx_len = 0;
    }
    if (!tg_usb.sx_on)
        return;
    if (tg_usb.sx_len < sizeof tg_usb.sysex)
        tg_usb.sysex[tg_usb.sx_len++] = b;
    if (b == 0xF7) {
        uint32_t i, ok = tg_usb.sx_len == 6u, id = tg_usb.sx_len == 10u;
        for (i = 0; ok && i < 6u; i++)
            ok = tg_usb.sysex[i] == UBOOT_KEY[i];
        for (i = 0; id && i < 10u; i++)
            id = tg_usb.sysex[i] == IDENT_Q[i];
        if (ok)
            tg_usb.uboot_req = 1;
        else if (id)
            ident_reply();
        tg_usb.sx_on = 0;
    }
}

static void midi_in_event(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin >= 4u && cin <= 7u) {
        uint32_t k, nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (k = 0; k < nb; k++)
            sysex_byte((uint8_t)(pkt >> (8u * (k + 1u))));
        return;
    }
    if (st >= 0x80u && st < 0xF8u)
        tg_usb.sx_on = 0;
    if (cin >= 8u && cin <= 0xEu && (st >> 4) == cin && mi_w - mi_r < MQ) {
        midi_in_q[mi_w % MQ] = pkt;       /* notes / CCs: kept for a later version */
        RING_PUBLISH();
        mi_w++;
    }
}

static void ep_midi_rx(void)
{
    uint32_t csr, n, i;
    sie_wr(S_INDEX, MIDI_EP);
    csr = sie_rd(S_RXCSR1) | (sie_rd(S_RXCSR2) << 8);
    if (!(csr & 1u)) {
        tg_usb.rx_pend = 0;
        return;
    }
    n = sie_rd(S_RXCOUNT1) | (sie_rd(S_RXCOUNT2) << 8);
    if (n > 64u)
        n = 64u;
    fm1_usb_rx_sync();
    tg_dc_flushinv(ep4rx, 64);
    for (i = 0; i + 3u < n; i += 4u)
        midi_in_event((uint32_t)ep4rx[i] | (uint32_t)ep4rx[i + 1] << 8 | (uint32_t)ep4rx[i + 2] << 16 |
                      (uint32_t)ep4rx[i + 3] << 24);
    mi_r = mi_w;                          /* nothing reads them yet */
    tg_usb.rx_pend = 0;
    tg_usb.rx_pkts++;
    csr = (csr & ~0x164u) | 0x10u;
    sie_wr(S_RXCSR1, csr & 0xFFu);
    sie_wr(S_RXCSR2, csr >> 8);
}

static void ep_midi_tx(void)
{
    uint32_t csr, n = 0;
    if (mo_w == mo_r)
        return;
    sie_wr(S_INDEX, MIDI_EP);
    csr = sie_rd(S_TXCSR1);
    if (csr & 0x01u)
        return;                                         /* previous packet still pending */
    if (csr & 0x80u)
        sie_wr(S_TXCSR1, csr & ~0x80u);
    while (mo_r != mo_w && n < 64u) {
        uint32_t pkt = midi_out_q[mo_r % MQ];
        ep4tx[n] = (uint8_t)pkt;
        ep4tx[n + 1] = (uint8_t)(pkt >> 8);
        ep4tx[n + 2] = (uint8_t)(pkt >> 16);
        ep4tx[n + 3] = (uint8_t)(pkt >> 24);
        n += 4u;
        mo_r++;
    }
    tg_dc_flushinv(ep4tx, 64);
    fm1_usb_ep4_send(ep4tx, n);
    sie_wr(S_TXCSR1, sie_rd(S_TXCSR1) | 0x01u);
    tg_usb.tx_pkts++;
}

static void usb_poll(void)
{
    uint32_t iu, it, ir;
    if (!tg_usb.up)
        return;
    (void)fm1_usb_sof_take();
    iu = sie_rd(S_INTRUSB);
    it = sie_rd(S_INTRTX1);
    ir = sie_rd(S_INTRRX1);
    if (iu & 0x04u) {                                   /* bus reset */
        tg_usb.resets++;
        sie_wr(S_FADDR, 0);
        tg_usb.config = 0;
        mo_r = mo_w;
        tg_usb.sx_on = 0;
        tg_usb.e0_tx = 0;
        tg_usb.has_pend_addr = 0;
        tg_usb.rx_pend = 0;
        fm1_usb_ep0_buf(ep0buf);
        sie_wr(S_INTRUSBE, 0x07);
        sie_wr(S_INTRTX1E, 0x01);
        sie_wr(S_INTRRX1E, 0);
    }
    if (it & (1u << MIDI_EP))
        tg_usb.tx_done++;                               /* the host took a MIDI IN packet */
    if (it & 0x01u)
        ep0_service();
    if (ir & (1u << MIDI_EP))
        tg_usb.rx_pend = 1;
    if (tg_usb.rx_pend)
        ep_midi_rx();
    if (tg_usb.config)
        ep_midi_tx();
}

static void usb_start(void)
{
    tg_usb.timeouts = 0;
    fm1_usb_reset();                                    /* whatever the ROM / bootloader left */
    delay_fed(600000u);                                 /* >= 25 ms at any clock we run at */
    fm1_usb_attach(ep0buf);
    sie_wr(S_POWER, 0x60);
    sie_wr(S_INTRUSBE, 0x07);
    sie_wr(S_INTRTX1E, 0x01);
    sie_wr(S_INTRTX2E, 0);
    sie_wr(S_INTRRX1E, 0);
    sie_wr(S_INTRRX2E, 0);
    fm1_usb_ep_enable(0x1Fu);
    tg_usb.up = tg_usb.timeouts < 3u;
}

static void usb_detach(void)
{
    tg_usb.up = 0;
    fm1_usb_off();
}
