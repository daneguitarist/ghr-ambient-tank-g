/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GerbilHamster Records
 * TANK-G: GHR AMBIENT 0.11 - our firmware for the M-VAVE Tank-G v2 (GerbilHamster Records), the ambient board:
 *   A GRAB (a frozen, drifting, rotting cloud of what you just played; held: a live cloud), B SPACE (a huge reverb),
 *   C SMEAR (an echo whose notes swell in and blur), D WARBLE (everything through a nervous tape), NERVE on the MOD FX knob. The sound is amb.h.
 * Started by the pedal's own bootloader from flash (FIRST LIGHT 0.1 .. 0.5 built the platform: lights, display,
 *   rings, footswitches, knobs, stereo I2S). USB-MIDI ("Tank-G GHR", tg_usb.c after Felucca): it answers the M-VAVE
 *   identity request as GHR-AMBIENT-0.11 Rxx and takes the M-VAVE soft key (F0 22 24 35 7D F7) -> the USB loader, so
 *   the tools can reach it without the footswitches.
 * Escape hatches (they run before anything else):
 *   - footswitch A held while the pedal powers up -> the chip's USB loader (for the restore tool)
 *   - the last reset was our watchdog (= we hung or crashed) -> the USB loader, instead of looping
 *   - A + D held for 2 s while running -> three beeps -> the USB loader
 *   - B + C held for 5 s: a deliberate hang (stops feeding the watchdog), to prove the watchdog escape works
 * Before going to the loader it leaves a status block at 0x1C3F000 (RAM survives) that the tools read and print.
 * Interrupts stay off: everything is polled, paced by the I2S clock, as in the RAM tests.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef short s16;
typedef unsigned char u8;

#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))

#define PORT(p) (0x50000u + (p) * 0x40u)
#define PA 0
#define PB 1
#define PC 2
#define PH 7
#define P_IN 0x04u
#define P_OUT 0x00u
#define P_DIR 0x08u
#define P_DIE 0x0Cu
#define P_PU 0x10u
#define P_PD 0x14u
#define PR(p, r) R32(PORT(p) + (r))

#define ALNK 0x12E00u
#define CON0 R16(ALNK + 0x00u)
#define CON1 R16(ALNK + 0x04u)
#define CON2 R8(ALNK + 0x08u)
#define CON3 R8(ALNK + 0x0Cu)
#define ADR0 R32(ALNK + 0x10u)
#define ADR1 R32(ALNK + 0x14u)
#define ALEN R16(ALNK + 0x20u)
#define CLK_CON2 R32(0x10014u)
#define IOMAP_CON5 R32(0x51030u)
#define IOMAP_CON1 R32(0x51020u)
#define ADC_CON R32(0x13100u)
#define ADC_RES R32(0x13104u)
#define WLA_CON0 R32(0x11900u)

#define SPI1 0x11D00u
#define S_CON R32(SPI1 + 0x00u)
#define S_BAUD R32(SPI1 + 0x04u)
#define S_ADR R32(SPI1 + 0x0Cu)
#define S_CNT R32(SPI1 + 0x10u)
#define S_PND 0x8000u
#define S_PCLR 0x4000u

/* RAM map: code runs from flash (XIP 0x02000120); .data/.bss 0x1C40000 (app.ld); stacks 0x1C74100-0x1C7C000;
   0x1C7FD50.. belongs to the boot mailbox / boot info / vectors. Our fixed buffers: */
#define ECBUF ((s16 *)0x1C08000)            /* ECHO line, 64 KB -> 0x1C18000 */
#define FDBUF ((s32 *)0x1C18000)            /* SPACE (8 lines + 4 diffusers) + SMEAR's blurs + look-ahead, 66 KB -> 0x1C2899C */
#define WBBUF ((s16 *)0x1C30800)            /* WARBLE lines L + R, 8 KB -> 0x1C32800 */
#define GBUF ((s16 *)0x1C44000)             /* the memory GRAB freezes: 4 s, 172 KB -> 0x1C6F110 (.bss ends below) */
#define TXB ((volatile s32 *)0x1C30000)
#define RXB ((volatile s32 *)0x1C30400)
#define HALF 128u       /* words per half = 64 stereo frames */
#define ZBUF ((volatile u8 *)0x1C38000)    /* 4096 zero bytes, for the SPI1 timing */
#define ZLEN 4096u
#define ZREP 4u
#define RBUF ((volatile u8 *)0x1C3A000)    /* the ring frame */
#define NLED 12u
#define RLEN (36u + 24u * NLED)
#define STATE 0x1C3B000u                   /* our state, < 4 KB */
#define STAT ((volatile u32 *)0x1C3F000)   /* status block for the tools, 256 words */
#define FW_MAGIC 0x7A6B0001u
#define FW_VERSION 0x0000000Bu
/* 0.3: a snapshot of the clock and USB registers as we find them (STAT[128..]) and after usb_start (STAT[160..]),
   to see why USB starts from the loader but not from a normal boot */
static const u32 SNAP_REGS[24] = { 0x16A00u, 0x16A04u, 0x16A08u, 0x16A0Cu, 0x10000u, 0x10004u, 0x10008u, 0x1000Cu,
                                   0x10010u, 0x10014u, 0x119A0u, 0x40404u, 0x4040Cu, 0x5101Cu, 0x51000u, 0x11800u,
                                   0x11804u, 0x1EEE008u, 0x1EEE00Cu, 0x10018u, 0x1001Cu, 0x16A10u, 0x16A14u, 0x13E04u };
static void snap(u32 at) { u32 i; for (i = 0; i < 24u; i++) STAT[at + i] = R32(SNAP_REGS[i]); STAT[at + 24] = 0x5A5A0000u | at; }

static inline s32 s24(u32 x) { return ((s32)(x << 8)) >> 8; }
#include "amb.h"

#define SEC 44100u
#define F_TONE 6615u
#define F_GAP 4410u
#define CAL_HALVES 40u
#define SHOW_HALVES 1034u   /* 1.5 s */
#define EXIT_HALVES 1378u   /* 2 s */
#define HOLD_HALVES 413u    /* 0.6 s: A held = the live cloud */
#define IDLE_MAX 20000000u

struct st {
    struct amb d;
    s32 kf[16];          /* filtered knob, Q4 */
    u32 kv[16];          /* 0..1023 */
    u32 kshown[16];
    u32 presses[4], deb[4], on_halves[4];
    u8 rgb[NLED * 3];
};
typedef char st_fits[(sizeof(struct st) < 4096u) ? 1 : -1];

static void delay(u32 n) { volatile u32 i; for (i = 0; i < n; i++) { } }

/* P33 (power-domain) register access, own implementation of the documented byte protocol */
#define P33_CON R32(0x13E08u)
#define P33_DAT R32(0x13E0Cu)
static u8 p33_xfer(u8 b)
{
    u32 n = 100000u;
    P33_DAT = b;
    P33_CON |= 1u << 4;
    (void)P33_CON;
    while ((P33_CON & (1u << 1)) && --n) { }
    return (u8)P33_DAT;
}
static u8 p33_read(u32 a)
{
    u8 v;
    P33_CON &= ~(1u << 8); P33_CON |= 1u;
    p33_xfer((u8)(0x80u | ((a >> 8) & 3u)));
    p33_xfer((u8)a);
    v = p33_xfer(0x5E);
    P33_CON &= ~((1u << 0) | (1u << 8));
    return v;
}
static void p33_op(u32 op, u32 a, u8 d)     /* 0 write, 1 or, 2 and */
{
    P33_CON &= ~(1u << 8); P33_CON |= 1u;
    p33_xfer((u8)((op << 5) | ((a >> 8) & 3u)));
    p33_xfer((u8)a);
    p33_xfer(d);
    P33_CON &= ~((1u << 0) | (1u << 8));
}
#define P3_WDT_CON 0x80u
#define P3_VLD_KEEP 0x17u
#define P3_RST_SRC 0x12u
#define P3_PR_PWR 0xA0u
static void wdt_feed(void) { p33_op(1, P3_WDT_CON, 0x40u); }
static void wdt_arm(u8 t)    /* reset-mode watchdog: 0x0C = about 4 s */
{
    p33_op(0, P3_WDT_CON, 0);
    p33_op(2, P3_VLD_KEEP, (u8)~0x40u);
    p33_op(0, P3_WDT_CON, (u8)((t & 0x0Fu) | 0x40u));
    p33_op(1, P3_WDT_CON, 0x10u);
}
static void delay_fed(u32 n) { while (n > 20000u) { delay(20000u); wdt_feed(); n -= 20000u; } delay(n); wdt_feed(); }

/* the chip's USB loader: "usb_update_mode" in the boot mailbox + a chip reset through P33 (RAM is kept) */
static void enter_loader(u32 why)
{
    static const char key[16] = "usb_update_mode";
    volatile u8 *mb = (volatile u8 *)0x01C7FD80u;
    u32 i;
    __asm__ volatile("cli");
    STAT[1] = why;
    STAT[63] = 0x600DF00Du;
    if (!(R32(0x1EEE240u) & 1u)) R32(0x1EEE240u) = 0xE7u;   /* drop any CPU write limits over the mailbox */
    R32(0x1EEE348u) = 0;
    for (i = 0; i < 16u; i++) mb[i] = (u8)key[i];
    p33_op(1, P3_PR_PWR, 0x10u);
    for (;;) { }
}

#include "tg_usb.c"

static void pin_out(u32 p, u32 bit, u32 v)   /* = factory 0x02003964: OUT = v, DIR = output */
{
    u32 m = 1u << bit;
    if (v) PR(p, P_OUT) |= m; else PR(p, P_OUT) &= ~m;
    PR(p, P_DIR) &= ~m;
}
static void pin_in(u32 p, u32 bit, u32 pu, u32 die)
{
    u32 m = 1u << bit;
    PR(p, P_DIR) |= m;
    PR(p, P_PD) &= ~m;
    if (pu) PR(p, P_PU) |= m; else PR(p, P_PU) &= ~m;
    if (die) PR(p, P_DIE) |= m; else PR(p, P_DIE) &= ~m;
}

static void led_write(u32 w, u32 dl)
{
    int b;
    PR(PA, P_OUT) &= ~1u; delay(dl);
    for (b = 28; b >= 0; b--) {
        PR(PA, P_OUT) &= ~4u; delay(dl);
        if (w & (1u << b)) PR(PA, P_OUT) &= ~8u; else PR(PA, P_OUT) |= 8u;
        delay(dl);
        PR(PA, P_OUT) |= 4u; delay(dl);
    }
    delay(dl);
    PR(PA, P_OUT) |= 1u;
}

static s32 adc_read(u32 ch)   /* = MOTES fm1_adc_read */
{
    u32 t, v;
    ADC_CON = 0;
    if (WLA_CON0 & (1u << 14)) WLA_CON0 &= ~(1u << 14);
    ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    ADC_CON |= 0x10u;
    ADC_CON |= 0x40u;
    for (t = 0; t < 200000u; t++) if (ADC_CON & 0x80u) break;
    v = ADC_RES & 0x3FFu;
    ADC_CON = 0x40u;
    ADC_CON = 0;
    return t == 200000u ? -1 : (s32)v;
}

static void ring_encode(const u8 *rgb)
{
    u32 i, c, b;
    for (i = 0; i < 36u; i++) RBUF[i] = 0;
    for (i = 0; i < NLED; i++) {
        u32 v3[3];
        v3[0] = rgb[i * 3 + 1]; v3[1] = rgb[i * 3 + 0]; v3[2] = rgb[i * 3 + 2];
        for (c = 0; c < 3u; c++)
            for (b = 0; b < 8u; b++)
                RBUF[36u + i * 24u + c * 8u + b] = (v3[c] & (0x80u >> b)) ? 0x7Eu : 0x60u;
    }
}

static u32 i2s_poll(void)
{
    u8 p = CON2;
    u32 n = 0;
    if (p & 0x10u) { CON2 |= 1u; n = 1; }
    if (p & 0x20u) CON2 |= 2u;
    return n;
}

static u32 spi_time(u32 b)
{
    u32 k, h = 0, spin = 0;
    S_BAUD = b;
    for (k = 0; k < ZREP; k++) {
        S_CON |= S_PCLR;
        S_ADR = (u32)ZBUF;
        S_CNT = ZLEN;
        while (!(S_CON & S_PND)) {
            h += i2s_poll();
            if ((++spin & 0xFFFFu) == 0) wdt_feed();
            if (h > 3000u || spin > 400000000u) return 0;
        }
    }
    return h ? h : 1u;
}

struct osc { s32 x, y, k; };
static inline s32 osc_step(struct osc *o) { o->x += (o->k * o->y) >> 12; o->y -= (o->k * o->x) >> 12; return o->x; }

static void set_ring(u8 *rgb, u32 ring, u32 r, u32 g, u32 b)
{
    u32 i;
    for (i = ring * 3u; i < ring * 3u + 3u; i++) { rgb[i * 3] = (u8)r; rgb[i * 3 + 1] = (u8)g; rgb[i * 3 + 2] = (u8)b; }
}

static void ring_send_wait(const u8 *rgb)
{
    u32 spin = 0;
    ring_encode(rgb);
    S_CON |= S_PCLR; S_ADR = (u32)RBUF; S_CNT = RLEN;
    while (!(S_CON & S_PND) && ++spin < 20000000u) { }
}

/* ------------------------------------------------------------------ the very first thing */
static u32 boot_rst, boot_footA;

void tg_cstart(void)
{
    extern u32 _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
    u32 *s, *d, i, low = 0;
    u8 p3 = p33_read(P3_RST_SRC);
    /* escape hatch 1: footswitch A (PA10, pull-up, low = pressed) held while powering up */
    pin_in(PA, 10, 1, 1);
    for (i = 0; i < 16u; i++) { delay(20000u); if (!((PR(PA, P_IN) >> 10) & 1u)) low++; }
    STAT[0] = FW_MAGIC; STAT[2] = FW_VERSION; STAT[3] = p3; STAT[4] = low;
    if (low >= 14u) enter_loader(1);
    /* escape hatch 2: the last reset was the watchdog = we hung */
    if (p3 & 4u) enter_loader(2);
    wdt_arm(0x0C);
    snap(128);    /* the clock / USB registers as the bootloader (or loader) left them - after the escape hatches and
                     the watchdog, so even a bad read here can only end in the loader */
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    boot_rst = p3; boot_footA = low;
    {
        extern void tg_main(void);
        tg_main();
    }
    enter_loader(9);
}

void tg_main(void)
{
    volatile u32 *o = STAT;
    struct st *S = (struct st *)STATE;
    struct amb *d = &S->d;
    u32 i;
    for (i = 5; i < 128; i++) o[i] = 0;
    for (i = 160; i < 256; i++) o[i] = 0;
    o[1] = 0;
    {
        volatile u32 *z = (volatile u32 *)STATE;
        for (i = 0; i < sizeof(struct st) / 4u; i++) z[i] = 0;
    }
    for (i = 0; i < 16u; i++) S->kv[i] = 512u;   /* if the knobs can't be read: everything at noon */

    /* the light chain: PA0 latch, PA2 clock, PA3 data, PA1 = output enable (low = on) */
    u32 ledd = 200u;
    pin_out(PA, 0, 1); pin_out(PA, 1, 1); pin_out(PA, 2, 1); pin_out(PA, 3, 1);
    led_write(0, ledd);
    PR(PA, P_OUT) &= ~2u;

    /* knobs: mux select PC6-8 out (0), mux outputs PB1 / PC9 analog in; footswitches: inputs with pull-up */
    pin_out(PC, 6, 0); pin_out(PC, 7, 0); pin_out(PC, 8, 0);
    pin_in(PB, 1, 0, 0); pin_in(PC, 9, 0, 0);
    pin_in(PA, 10, 1, 1); pin_in(PH, 6, 1, 1); pin_in(PH, 8, 1, 1); pin_in(PH, 9, 1, 1);

    /* board init as the factory does it */
    pin_out(PC, 0, 0);
    pin_out(PC, 10, 0);
    pin_out(PA, 9, 0);
    delay_fed(100000);

    /* ALNK0 bring-up, factory order */
    CON0 = 0; CON1 = 0; CON2 = 0; CON3 = 0;
    IOMAP_CON5 = (IOMAP_CON5 & ~0xC0u) | 0x40u;
    CON3 |= 3u;
    pin_out(PA, 4, 1);
    CON0 |= 0x100u;
    pin_out(PA, 6, 1);
    pin_out(PA, 5, 1);
    CON0 |= 0x80u;
    CON0 &= ~0x40u;
    ALEN = HALF;
    CON0 &= ~0x400u;
    CON0 &= ~0x200u;
    CLK_CON2 &= 0xFFFFF0FFu;
    CON3 &= ~0x1Cu;
    CON3 = (u8)((CON3 & 0x1Fu) | 0x80u);
    for (i = 0; i < 2 * HALF; i++) { TXB[i] = 0; RXB[i] = 0; }
    CON1 |= 1u << 0; CON1 |= 1u << 2;
    ADR0 = (u32)TXB;
    pin_out(PA, 7, 1);
    CON1 &= ~(1u << 3);
    CON1 |= 1u << 4; CON1 |= 1u << 6;
    ADR1 = (u32)RXB;
    PR(PA, P_DIR) |= 1u << 8;
    PR(PA, P_PD) &= ~(1u << 8);
    PR(PA, P_PU) |= 1u << 8;
    PR(PA, P_DIE) |= 1u << 8;
    CON1 |= 1u << 7;
    CON0 |= 0x800u;
    delay_fed(100000);
    pin_out(PA, 9, 1);
    pin_out(PC, 0, 1);

    for (i = 0; i < EC_LEN; i++) ECBUF[i] = 0;
    for (i = 0; i < FD_ALL; i++) FDBUF[i] = 0;
    for (i = 0; i < 2u * WB_LEN; i++) WBBUF[i] = 0;
    wdt_feed();
    for (i = 0; i < GLEN; i++) GBUF[i] = 0;
    wdt_feed();
    amb_init(d, GBUF, ECBUF, FDBUF, WBBUF);

    usb_start();          /* USB-MIDI: polled once per audio half and from the idle loop */
    snap(160);
    o[190] = tg_usb.up; o[191] = tg_usb.timeouts;

    /* SPI1 for the rings: measure against the I2S clock, pick BAUD for ~1.2 us ring bits */
    for (i = 0; i < ZLEN; i++) ZBUF[i] = 0;
    IOMAP_CON1 |= 0x10u;
    S_CON = 0x4021u;
    u32 mh = spi_time(99u), baud = 6u, spi_ok = mh != 0, mh2 = 0;
    if (spi_ok) {
        u32 bp1 = (2u * 1355u + mh) / (2u * mh);
        if (bp1 < 1u) bp1 = 1u;
        if (bp1 > 256u) bp1 = 256u;
        baud = bp1 - 1u;
        mh2 = spi_time(baud);
    }
    S_BAUD = baud;
    o[8] = mh; o[9] = spi_ok; o[10] = baud; o[11] = mh2;

    /* the delay loop's speed -> light chain edges of ~1.5 us */
    {
        u32 h = 0, n = 0, spin = 0;
        while (!i2s_poll() && ++spin < 50000000u) { }
        while (h < 16u && n < 10000000u) { delay(64u); n++; h += i2s_poll(); if ((n & 1023u) == 0) wdt_feed(); }
        o[14] = n;
        ledd = (n * 96u + 23219u) / 23220u;
        if (ledd < 2u) ledd = 2u;
        if (ledd > 400u) ledd = 400u;
        o[33] = ledd;
    }

    /* hello: a white sweep across the rings */
    if (spi_ok) {
        u32 r;
        for (r = 0; r < 5u; r++) {
            for (i = 0; i < NLED * 3u; i++) S->rgb[i] = 0;
            if (r < 4u) set_ring(S->rgb, r, 40, 40, 40);
            ring_send_wait(S->rgb);
            delay_fed(400000u);
        }
    }

    struct osc beep = { 3000, 0, 257 };
    u32 frame = 0, idle = 0, halves = 0, rxhalves = 0;
    u32 txonly = 0, rxdead = 0;
    u32 mux = 0, adc_ok = 0, adc_to = 0, kseen = 0;
    u32 sw = 0, exit_h = 0, hang_h = 0, ending = 0, f_b3 = 0, f_end = 0xFFFFFFFFu;
    u32 i0sum = 0, spins_min = 0xFFFFFFFFu, spins_sum = 0, spins_n = 0, overruns = 0;
    u32 show_t = 0, show_slot = 0, cliphold = 0;
    u32 shown_w = 0xFFFFFFFFu, last_w = 0, chain_writes = 0;
    u32 busy = 0, sends = 0, done = 0, waits = 0;
    u32 f_start = 0, wraps = 0;
    static const u8 dig[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    static const u8 slot_light[16] = { 0, 3, 2, 6, 1, 5, 4, 7, 8, 12, 10, 14, 9, 13, 11, 15 };
    /* the knob lights that belong to each footswitch: A the cloud's (DRIFT SIZE CLOUD START ROT PITCH), B SPACE's
       (DECAY MIX), C ECHO's (MIX TIME), D NERVE */
    static const u32 fx_lights[4] = { (1u << 6) | (1u << 1) | (1u << 8) | (1u << 10) | (1u << 9) | (1u << 13),
                                      (1u << 3) | (1u << 2), (1u << 5) | (1u << 4), 1u << 7 };
    u32 over_s = 0, a_hold = 0, a_was_live = 0, b_hold = 0, b_used = 0;
    for (;;) {
        u8 p = CON2;
        if (p & 0x10u) { CON2 |= 1u; if (!(p & 0x20u) && !rxdead && ++txonly > 64u) rxdead = 1; }
        int tick = rxdead ? (p & 0x10u) != 0 : (p & 0x20u) != 0;
        if (!tick) {
            ++idle;
            if ((idle & 2047u) == 0) usb_poll();
            if ((idle & 0xFFFFu) == 0) wdt_feed();
            if (idle > IDLE_MAX) { o[5] = 1; enter_loader(3); }   /* the audio clock stopped */
            continue;
        }
        if (p & 0x20u) { CON2 |= 2u; txonly = 0; }
        u32 spins = idle;
        idle = 0;
        u16 c0 = CON0;
        u32 hr = ((c0 >> (rxdead ? 12 : 13)) & 1u) ^ 1u;
        volatile s32 *tx = TXB + hr * HALF, *rx = RXB + hr * HALF;
        int have_rx = (p & 0x20u) != 0;
        if (have_rx) rxhalves++;
        if (++halves == 0) wraps++;
        usb_poll();
        if (tg_usb.uboot_req && !ending) { ending = 3; break; }          /* the soft key: straight to the loader */
        if (!tg_usb.up && (halves % 689u) == 0) { tg_usb.retries++; usb_start(); }   /* no cable yet: retry 1/s */
        u32 quiet = halves > 8u && halves <= CAL_HALVES;

        if (halves <= CAL_HALVES) {
            if (halves > 9u) i0sum += spins;
            for (i = 0; i < HALF; i++) tx[i] = 0;
        } else {
            if (halves == CAL_HALVES + 1u) {
                amb_params(d, S->kv);
                d->ec_d = d->ec_target;
                for (i = 0; i < 16u; i++) S->kshown[i] = S->kv[i];
                f_start = frame;
                o[15] = i0sum / (CAL_HALVES - 9u);
            } else {
                if (spins < spins_min) spins_min = spins;
                spins_sum += spins; spins_n++;
                if (spins == 0) { overruns++; over_s++; }
            }
            amb_audio(d, rx, tx, have_rx);
            for (i = 0; i < HALF; i += 2, frame++) {      /* the beeps go on top */
                s32 bs = osc_step(&beep);
                u32 st = 0, nb = 0, k;
                if (frame - f_start >= 4410u && frame - f_start < 4410u + F_TONE) { st = f_start + 4410u; nb = 1; }
                else if (ending && frame >= f_b3) { st = f_b3; nb = 3; }
                for (k = 0; k < nb; k++) {
                    u32 s0 = st + k * (F_TONE + F_GAP);
                    if (frame >= s0 && frame < s0 + F_TONE) {
                        u32 tt = frame - s0;
                        s32 e = tt < 256u ? (s32)tt : (F_TONE - tt < 256u ? (s32)(F_TONE - tt) : 256);
                        s32 v = ((bs * e) >> 8) * 128, l = tx[i] + v, r = tx[i + 1] + v;
                        tx[i] = l > 8388607 ? 8388607 : (l < -8388607 ? -8388607 : l);
                        tx[i + 1] = r > 8388607 ? 8388607 : (r < -8388607 ? -8388607 : r);
                    }
                }
            }
            if (ending && frame >= f_end) break;
        }

        if (quiet) continue;
        if (adc_to <= 64u) {
            s32 a3 = adc_read(3), a8 = adc_read(8);
            u32 k;
            for (k = 0; k < 2u; k++) {
                s32 v = k ? a8 : a3;
                u32 s = mux + k * 8u;
                if (v < 0) { adc_to++; continue; }
                adc_ok++;
                if (!(kseen & (1u << s))) { S->kf[s] = v * 16; kseen |= 1u << s; }
                else S->kf[s] += (v * 16 - S->kf[s]) >> 2;
                S->kv[s] = (u32)(S->kf[s] + 8) >> 4;
                if (S->kv[s] > 1023u) S->kv[s] = 1023u;
                if (halves > CAL_HALVES + 1u && s != 15u) {
                    s32 df = (s32)S->kv[s] - (s32)S->kshown[s];
                    if (df > 24 || df < -24) { S->kshown[s] = S->kv[s]; show_t = SHOW_HALVES; show_slot = s; }
                }
            }
            mux = (mux + 1u) & 7u;
            PR(PC, P_OUT) = (PR(PC, P_OUT) & ~(7u << 6)) | (mux << 6);
        }

        {
            u32 ia = PR(PA, P_IN), ih = PR(PH, P_IN), k;
            u32 raw[4];
            raw[0] = !((ia >> 10) & 1u); raw[1] = !((ih >> 9) & 1u); raw[2] = !((ih >> 8) & 1u); raw[3] = !((ih >> 6) & 1u);
            for (k = 0; k < 4u; k++) {
                u32 cur = (sw >> k) & 1u;
                if (raw[k] != cur) {
                    if (++S->deb[k] >= 8u) {
                        S->deb[k] = 0; sw ^= 1u << k;
                        if (raw[k] && halves > CAL_HALVES && !ending) {
                            S->presses[k]++;
                            if (k == 0u) { a_was_live = d->livec; a_hold = 1; amb_grab(d); }
                            else if (k == 1u) { b_hold = 1; b_used = 0; }   /* B acts when it's let go (a tap) or held */
                            else d->on ^= 1u << k;
                        } else if (!raw[k] && k == 1u && b_hold && !b_used && !ending) d->on ^= FX_SPACE;   /* B tapped */
                    }
                } else S->deb[k] = 0;
                if (k ? (d->on & (1u << k)) != 0 : d->cstate != C_LIVE) S->on_halves[k]++;
            }
            /* B held ~0.6 s (not with C): the dry guitar out / back in */
            if (sw & 2u) {
                if (b_hold && !b_used && !(sw & 4u) && !ending && ++b_hold >= HOLD_HALVES) { d->dryoff ^= 1u; b_used = 1; }
                if (sw & 4u) b_used = 1;
            } else b_hold = 0;
            /* A held ~0.6 s on its own: the live cloud (or, from the live cloud, fade it out) */
            if (!(sw & 1u)) a_hold = 0;
            else if (a_hold && !(sw & 8u) && !ending && ++a_hold == HOLD_HALVES) { amb_hold(d, a_was_live); a_hold = 0; }
            if ((sw & 9u) == 9u && !ending) { if (++exit_h >= EXIT_HALVES) ending = 1; }
            else exit_h = 0;
            if ((sw & 6u) == 6u && !ending) {
                if (++hang_h >= 3445u) {          /* 5 s: hang on purpose, silently, lights as they are */
                    o[1] = 5; o[6] = halves;
                    for (i = 0; i < 2 * HALF; i++) TXB[i] = 0;
                    __asm__ volatile("cli");
                    for (;;) { }
                }
            } else hang_h = 0;
        }
        if (ending == 1) { ending = 2; f_b3 = frame + 4410u; f_end = f_b3 + 3u * (F_TONE + F_GAP) + 4410u; }

        if (halves > CAL_HALVES) { amb_params(d, S->kv); amb_half(d); }
        /* if the sound ever runs late (it shouldn't), fewer grains at once, a step at a time */
        if (halves % 689u == 0u) {
            if (over_s > 4u && d->ngmax > 3u) { d->ngmax--; d->guard++; }
            over_s = 0;
        }

        if (busy) {
            if (S_CON & S_PND) { busy = 0; done++; }
            else if (++busy > 64u) { busy = 0; waits++; }
        }
        if (spi_ok && !busy && (halves & 3u) == 0) {
            u8 *c = S->rgb;
            u32 on = ending ? 0u : d->on;
            for (i = 0; i < NLED * 3u; i++) c[i] = 0;
            if (!ending && d->cstate != C_LIVE) {     /* A: the cloud - pale and breathing, amber while it fades */
                u32 f = (u32)d->cfade, br = 24u + (u32)((lfo_bip(halves << 21) + 4096) >> 8), e = (u32)d->cl_env >> 7;
                if (e > 24u) e = 24u;
                br = ((br + e) * f) >> 12;
                if (d->cstate == C_OUT) set_ring(c, 0, br * 3u / 2u, br * 2u / 3u, br / 8u);
                else if (d->livec) set_ring(c, 0, br * 3u / 2u, br / 10u, br * 2u + 20u > 255u ? 255u : br * 2u + 20u);   /* live: bright purple */
                else set_ring(c, 0, br, br, br * 3u / 2u);
            }
            if (!ending && d->dryoff) { if (on & FX_SPACE) set_ring(c, 1, 80, 8, 64); else set_ring(c, 1, 16, 2, 12); }   /* B: magenta = no dry */
            else if (on & FX_SPACE) set_ring(c, 1, 30, 6, 56);
            if (on & FX_ECHO) { u32 g = (d->sw_g >> 12) * 40u >> 12; set_ring(c, 2, 4u + g / 3u, 8u + g / 2u, 24u + g); }   /* C: blue, swelling */
            if (on & FX_WARBLE) { s32 g = (d->nw.g >> 8) - 2048; if (g < 0) g = 0; set_ring(c, 3, 0, 8u + ((u32)g * 40u >> 11), 14); }
            ring_encode(c);
            S_CON |= S_PCLR;
            S_ADR = (u32)RBUF;
            S_CNT = RLEN;
            busy = 1; sends++;
        }

        {
            u32 w;
            if (halves <= CAL_HALVES) w = 0;
            else if (ending) w = 1u << 25;
            else {
                u32 seg;
                if (show_t) {
                    show_t--;
                    seg = dig[S->kv[show_slot] * 10u / 1024u];
                    w = (1u << slot_light[show_slot]) | (1u << 28) | (seg << 16);
                } else {
                    s32 e = d->env_in;
                    if (e > 30000) cliphold = 300u;
                    seg = e > 8000 ? 0x7Fu : e > 2600 ? 0x7Eu : e > 900 ? 0x5Cu : e > 300 ? 0x1Cu : e > 100 ? 0x08u : 0u;
                    if (cliphold) { cliphold--; seg |= 0x80u; }
                    w = 1u | (1u << 27) | (seg << 16);
                    if (d->cstate != C_LIVE) w |= fx_lights[0];
                    for (i = 1; i < 4u; i++) if (d->on & (1u << i)) w |= fx_lights[i];
                }
                if (exit_h ? ((halves >> 6) & 1u) : ((halves % 689u) < 48u)) w |= 1u << 25;
            }
            if (w != shown_w && halves - last_w >= 8u) { led_write(w, ledd); shown_w = w; last_w = halves; chain_writes++; }
        }

        /* the status block, once a second */
        if ((halves & 63u) == 0) {
            wdt_feed();
            if ((halves & 511u) == 0) {
                o[6] = halves; o[7] = wraps;
                o[16] = spins_min; o[17] = overruns; o[18] = spins_n ? spins_sum / spins_n : 0;
                o[19] = adc_ok; o[20] = adc_to;
                for (i = 0; i < 4u; i++) { o[21 + i] = S->presses[i]; o[80 + i] = S->on_halves[i]; }
                o[25] = sends; o[26] = done; o[27] = waits; o[28] = chain_writes;
                o[29] = (u32)d->peak_in; o[30] = (u32)d->peak_out; o[31] = d->clips; o[32] = d->on | d->dryoff << 8;
                o[34] = rxhalves; o[35] = rxdead; o[36] = boot_rst; o[37] = boot_footA;
                o[38] = tg_usb.up | (u32)tg_usb.config << 8; o[39] = tg_usb.setups; o[40] = tg_usb.idents;
                o[41] = tg_usb.rx_pkts; o[42] = tg_usb.tx_pkts; o[43] = tg_usb.resets; o[44] = tg_usb.retries; o[45] = tg_usb.tx_done;
                for (i = 0; i < 16u; i++) o[64 + i] = S->kv[i];
                o[46] = d->cstate | d->livec << 4 | d->nact << 8 | d->ngmax << 16; o[47] = d->grabs; o[48] = d->spawns;
                o[49] = d->rot_passes; o[50] = d->rot_events; o[51] = (u32)d->ag >> 8; o[52] = d->valid; o[53] = d->guard;
                o[54] = d->lives; o[55] = (u32)d->cmin; o[56] = d->swells; o[57] = d->bends;   /* (cmin: the whole session) */
            }
        }
    }

    /* A + D held, or the soft key: rings and lights off, USB off, then the USB loader */
    o[38] = tg_usb.up | (u32)tg_usb.config << 8; o[39] = tg_usb.setups; o[40] = tg_usb.idents;
    o[41] = tg_usb.rx_pkts; o[42] = tg_usb.tx_pkts; o[43] = tg_usb.resets; o[44] = tg_usb.retries; o[45] = tg_usb.tx_done;
    o[6] = halves; o[17] = overruns; o[18] = spins_n ? spins_sum / spins_n : 0; o[16] = spins_min;
    for (i = 0; i < 4u; i++) { o[21 + i] = S->presses[i]; o[80 + i] = S->on_halves[i]; }
    for (i = 0; i < 16u; i++) o[64 + i] = S->kv[i];
    o[29] = (u32)d->peak_in; o[30] = (u32)d->peak_out; o[31] = d->clips;
    o[46] = d->cstate | d->livec << 4 | d->nact << 8 | d->ngmax << 16; o[47] = d->grabs; o[48] = d->spawns;
    o[49] = d->rot_passes; o[50] = d->rot_events; o[51] = (u32)d->ag >> 8; o[52] = d->valid; o[53] = d->guard;
    o[54] = d->lives; o[55] = (u32)d->cmin; o[56] = d->swells; o[57] = d->bends;
    if (spi_ok) {
        for (i = 0; i < NLED * 3u; i++) S->rgb[i] = 0;
        ring_send_wait(S->rgb);
    }
    led_write(0, ledd);
    PR(PA, P_OUT) |= 2u;
    for (i = 0; i < 2 * HALF; i++) TXB[i] = 0;
    usb_detach();
    delay_fed(600000);
    enter_loader(ending == 3 ? 6u : 4u);
}
