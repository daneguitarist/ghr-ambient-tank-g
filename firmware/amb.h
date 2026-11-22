/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GerbilHamster Records
 * amb.h: GHR AMBIENT, the Tank-G as an ambient board (firmware 0.11). GerbilHamster Records, 2026.
 *
 *   A  GRAB    freeze the last few seconds you played into a cloud of grains that hangs in the air, drifts through
 *              that memory and slowly rots. Stomp again: it fades away (3 s). Stomp during the fade: it comes back.
 *              HOLD it (0.6 s): the LIVE CLOUD (bright purple) - the memory keeps recording and the cloud trails
 *              behind whatever you play (~0.6 s, more with START), dancing around that spot. It blooms in over 4 s. Tap: freeze it where it is. Hold again: fade out.
 *              Hold on a frozen cloud: it comes back to life, the old memory running into the new.
 *   B  SPACE   a huge reverb (4 diffusers into 8 lines through a Hadamard matrix). Off: tails ring out. Acts when let go.
 *              HOLD it (0.6 s): the dry guitar fades out - wet only (ring magenta). Hold again: the dry comes back.
 *   C  SMEAR   a long dark echo that never attacks: every note you play swells into it (over SWELL) and each repeat
 *              is blurred a little more than the last. Turning its time bends the repeats.
 *   D  WARBLE  the whole pedal through a nervous old tape (how nervous: NERVE).
 *
 * Knobs (the panel's name -> what it does here):
 *   MASTER     output level                 RVB DECAY  SPACE length, 1.5 s .. a minute     RVB MIX  SPACE level
 *   MOD SPEED  DRIFT: how far and how fast the cloud wanders through its memory (all the way down: a still freeze)
 *   IR CAB     SIZE: grain length, 40 ms .. 0.6 s (bigger = smoother, smeared)
 *   DLY MIX    SMEAR level (the repeats grow with it)                                 DLY TIME SMEAR time, 60 ms .. 1.4 s
 *   MOD FX     NERVE: flutter, wow, sample-and-hold wobble, dropouts, motes - in the cloud, the smear and the
 *              space always, and on everything when WARBLE is on
 *   VOLUME     CLOUD level                  GAIN       input level                          MIDDLE   START: where in the
 *                                                                                           memory the cloud sits (min = newest)
 *   NOISE GATE SWELL: how slowly notes fade into SMEAR, 30 ms .. 3 s (all the way down: plain repeats)
 *   TYPE       the cloud's PITCH: -oct | -oct and unison | unison | unison, fifth and oct | +oct
 *   TREBLE     TONE: dark (min) .. open (max), for everything wet
 *
 * Always on, no knobs: a compressor on the input (2.5:1 from -20 dBFS, +3 dB back), an EQ (a little more bass, mids a
 * hair under, a little less treble) and a gentle hiss reducer under -62 dBFS (it replaced the noise gate).
 *
 * Integer only. Audio is Q15 (+-32767 = full scale) in s32, gains Q12. 64-bit products only as (a * b) >> constant,
 * which pi32v2 does in two instructions (r1_r0 = a * b; r1_r0 >>= n). The dry path and WARBLE run at 44.1 kHz; the
 * cloud, ECHO and SPACE run at 22.05 kHz ("ticks", one per two frames): dark on purpose, brought back up with linear
 * interpolation and a gentle low-pass. Nothing the knobs or the rot do may click: every gain glides, the memory only
 * ever changes in small steps, grains have smooth windows and stay away from the memory's seam.
 */
#ifndef AMB_H
#define AMB_H

#define FX_GRAB 1u
#define FX_SPACE 2u
#define FX_ECHO 4u
#define FX_WARBLE 8u

#define GLEN 88200u                     /* the memory: 4 s at 22.05 kHz, s16 */
#define GLENQ ((s32)(GLEN << 12))
#define GM 512u                         /* grains keep this far from the memory's ends (23 ms) */
#define BLK 11025u                      /* loudness is kept per 0.5 s block, 8 blocks */
#define EC_LEN 32768u                   /* echo line, s16 at 22.05 kHz: 1.49 s */
#define EC_MASK (EC_LEN - 1u)
#define WB_LEN 2048u                    /* warble lines, s16 at 44.1 kHz, L and R */
#define WB_MASK (WB_LEN - 1u)
#define NGR 8u                          /* grain slots for the cloud */
#define NGA 11u                         /* + 3 for the bender's clones and repeats */
#define NOV 4u                          /* grains overlapping, on average */
#define FD_PAD 64u                      /* extra room in the two lines whose read point moves */
#define FD_WORDS (13650u + 2u * FD_PAD + 1218u)
#define FD_ALL (FD_WORDS + 1747u + 1024u)  /* + SMEAR's blurs and the swell's 46 ms look-ahead */
#define NW_BASE (110 * 65536)           /* WARBLE's resting delay: 110 ticks = 5 ms */
#define CLOUD_TARGET 1700u              /* the cloud's level (|L| + |R|, averaged) before the CLOUD knob */

enum { C_LIVE = 0, C_IN = 1, C_HELD = 2, C_OUT = 3 };

static const u16 FD_N[8] = { 887, 1061, 1279, 1493, 1747, 2017, 2377, 2789 };   /* 40 .. 126 ms */
static const u16 DF_N[4] = { 173, 241, 337, 467 };
static const u16 SM_N[3] = { 379, 557, 811 };          /* SMEAR's blurs: 17, 25, 37 ms */

struct grain { s32 pos, inc; u32 ph, dph; s32 gl, gr; u32 on, bend, kk; s32 incq, dinc; };   /* kk: flat window */

/* NERVE: what makes things tremble. Sample-and-hold targets glided to through two smoothers (Dane's favourite kind
 * of wobble), a wow and a flutter whose speeds wander, dropouts (the level eases down, hangs, swells back), and a
 * level that wanders. Delay offsets are Q16 samples at 22.05 kHz. */
struct nerve {
    s32 s1, sh, sht;
    u32 shn, wph, winc, fph, finc;
    s32 d1, dg, dt;                     /* dropouts: smoother, level (Q16), target */
    u32 dhold;
    s32 am, amt;
    s32 off, offd, g, gd;
};

struct amb {
    /* from the knobs, once per half */
    u32 nerve, rot, drift, glen, startk, zone, ec_target, sw_rate;
    s32 sw_mk;
    s32 t_master, t_gain, t_cloud, t_echo, t_space, t_fb, tone, edamp, rdamp;
    s32 fdg[8], rcomp;                  /* rcomp: long SPACE decays are turned down a little (they pile up) */
    /* gliding gains, Q12 << 8 */
    s32 master, gain, cloudlv, echolv, spacelv, fb, wmix, dryg;
    u32 dryoff;                          /* B held: wet only */
    u32 on;
    /* in */
    s32 env_in, peak_in, peak_out, lpa;
    u32 clips, ccnt;
    s32 dci, ce, ct, cg, cmin, xg, xt, eq1, eq2, eq3, eq4;   /* input DC, compressor, hiss reducer, EQ */
    /* the memory and the cloud */
    s16 *gb;
    u32 gw, fresh, valid, newest, blk_n, blk_i, cstate, cenv, ngmax, nact, livec, wpause, wf, cut;
    s32 blk[8];
    s32 cfade, norm, ag, cmean, cage, dpos, dtgt, spawn_t, cl_env;
    struct grain gr[NGA];
    s32 bn_wait, br_t;                   /* the bender (A's extra layer) */
    u32 br_left, br_idx, br_s, bends;
    s32 tug1, tug, tug_t;                /* SMEAR's tugs */
    u32 tug_w, tug_n;
    /* rot */
    u32 rot_age, ev_kind, ev_age, ev_len, ev_left;
    s32 rot_prev, ev_wait;
    /* echo */
    s16 *ec;
    u32 ec_w, ec_d, ec_cnt, ec_flash;
    s32 ec_lp, ec_hp;
    s32 *sm[3], *la;                     /* SMEAR: its three blurs, the swell's look-ahead */
    u32 smp[3], la_i, sw_duck, sw_ref, sw_g, sw_g0, swells;
    s32 sw_f, sw_s;
    /* space */
    s32 *fl[8], *df[4];
    u32 fp[8], dp[4], mph0, mph1;
    s32 flp[8], sp_hp;
    /* warble */
    s16 *wbl, *wbr;
    u32 wb_w, nvc;
    struct nerve nw, ne, nc, ns;
    /* out */
    s32 tl, tr, hl[3], hr[3], dcl, dcr, ol, orr;
    u32 rng;
    u32 grabs, spawns, rot_passes, rot_events, guard, lives;
};

static inline s32 clamp_s(s32 v, s32 lim) { return v > lim ? lim : (v < -lim ? -lim : v); }
static inline s32 mq12(s32 a, s32 b) { return (s32)(((long long)a * b) >> 12); }
static inline s32 mq15(s32 a, s32 b) { return (s32)(((long long)a * b) >> 15); }
static inline s32 mq15r(s32 a, s32 b) { return (s32)(((long long)a * b + 16384) >> 15); }   /* rounded: no creeping DC in feedback */

static inline u32 rnd(struct amb *a) { u32 x = a->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; a->rng = x; return x; }
static inline s32 rnds(struct amb *a, s32 amp)   /* -amp .. amp, amp < 32768 */
{
    return (s32)(((rnd(a) >> 16) * (u32)(2 * amp + 1)) >> 16) - amp;
}

/* a rounded triangle, close to a sine: -4096 .. 4096 over one turn of ph */
static inline s32 lfo_bip(u32 ph)
{
    u32 t = ph >> 20;
    t = t < 2048u ? t : 4095u - t;
    t <<= 1;
    return (s32)((((t * t) >> 12) * (12288u - 2u * t)) >> 12) * 2 - 4096;
}

/* a smooth hump over p = 0..4095: 0 at both ends, 4096 in the middle (the grains' window) */
static inline s32 hump(u32 p)
{
    u32 t = p < 2048u ? p : 4095u - p;
    t <<= 1;
    return (s32)((((t * t) >> 12) * (12288u - 2u * t)) >> 12);
}

static u32 isqrt32(u32 x)
{
    u32 r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

/* 2^(-e/65536), Q15 */
static u32 exp2neg_q15(u32 e)
{
    u32 ip = e >> 16, f = e & 0xFFFFu, p;
    if (ip >= 15u) return 0;
    p = 3100u;                                   /* 1 - f (0.693147 - f (0.240227 - 0.0473 f)) */
    p = 15743u - ((p * f) >> 16);
    p = 45426u - ((p * f) >> 16);
    p = 65536u - ((p * f) >> 16);
    return (p >> 1) >> ip;
}

static void amb_init(struct amb *a, s16 *gb, s16 *ec, s32 *fd, s16 *wb)
{
    u32 k, o = 0;
    a->gb = gb; a->ec = ec; a->wbl = wb; a->wbr = wb + WB_LEN;
    for (k = 0; k < 8u; k++) { a->fl[k] = fd + o; o += FD_N[k] + ((k == 0u || k == 5u) ? FD_PAD : 0u); }
    for (k = 0; k < 4u; k++) { a->df[k] = fd + o; o += DF_N[k]; }
    for (k = 0; k < 3u; k++) { a->sm[k] = fd + o; o += SM_N[k]; }
    a->la = fd + o;
    a->rng = 0x9E3779B9u;
    a->ngmax = NGR;
    a->on = 0;                                       /* everything off at power-up */
    a->ec_d = a->ec_target = 1323u << 8;
    a->nw.off = NW_BASE;
    a->nw.g = a->ne.g = a->nc.g = a->ns.g = 4096 << 8;
    a->nw.am = a->ne.am = a->nc.am = a->ns.am = 4096 << 8;
    a->nw.amt = a->ne.amt = a->nc.amt = a->ns.amt = 4096 << 8;
    a->nw.dg = a->ne.dg = a->nc.dg = a->ns.dg = 65536;
    a->nw.d1 = a->ne.d1 = a->nc.d1 = a->ns.d1 = 65536;
    a->nw.dt = a->ne.dt = a->nc.dt = a->ns.dt = 65536;
    a->ev_wait = 2000;
    a->norm = 4096;
    a->tug_w = 40000u;
    a->dryg = 4096 << 8;
    a->wf = 256;
    a->ct = a->cg = a->cmin = 32768;
    a->xt = 4096; a->xg = 4096 << 8;
    a->sw_g = 1u << 24;
}

/* the knobs (0..1023, clockwise = up) -> parameters; once per half */
static void amb_params(struct amb *a, const u32 *k)
{
    u32 i, e, t60;
    a->t_master = (s32)k[0] * 8;                                  /* 0 .. 2x */
    e = k[1] * 341u;                                              /* 1.5 s x 40^(knob), Q16 log2 */
    t60 = k[1] >= 1012u ? 240000u : (((1500u << (e >> 16)) * (2u * exp2neg_q15(65536u - (e & 0xFFFFu)))) >> 15);
    if (t60 < 1000u) t60 = 1000u;
    for (i = 0; i < 8u; i++) {
        u32 e16 = (u32)FD_N[i] * 29620u / t60;                    /* 9.966 N / (T60 22050), Q16 */
        a->fdg[i] = (s32)((exp2neg_q15(e16) * 11585u) >> 15);    /* with the matrix's 1/sqrt(8) */
    }
    {   /* (1 - g^2) of a middle line against a 3 s room, to the 1/4 power: a minute-long SPACE is ~6 dB down, 10 s ~2 dB */
        u32 g = exp2neg_q15((u32)FD_N[3] * 29620u / t60), x = 65536u - ((g * g) >> 14), r = (x << 8) / 17498u;   /* r Q8 */
        if (r >= 256u) a->rcomp = 4096;
        else { u32 s1 = isqrt32(r << 16), s2 = isqrt32(s1 << 12); a->rcomp = (s32)s2; }   /* sqrt(r) Q12, then r^(1/4) Q12 */
    }
    a->t_space = (s32)k[2] * 6;
    a->drift = k[3];
    a->glen = 882u + ((((k[4] * k[4]) >> 10) * 12348u) >> 10);   /* 40 ms .. 0.6 s */
    a->t_echo = (s32)k[5] * 5;
    a->t_fb = (s32)((k[5] * 3200u) >> 10);                        /* .. 0.78 */
    a->ec_target = (1323u + ((k[6] * 29547u) >> 10)) << 8;        /* 60 ms .. 1.4 s, Q8 ticks */
    a->nerve = k[7];
    a->t_cloud = (s32)k[8] * 6;
    a->t_gain = 1024 + (s32)((((k[9] * k[9]) >> 8) * 3072u) >> 10);   /* 0.25x .. 1x (noon) .. 3.2x */
    a->startk = k[10];
    a->sw_rate = k[11] < 24u ? 0u : (1u << 24) / (660u + ((k[11] * k[11]) >> 10) * 64u);   /* SWELL: 30 ms .. 3 s */
    a->sw_mk = k[11] < 24u ? 4096 : 4096 + (s32)k[11] * 8;      /* (up to ~+9 dB for long swells) */
    a->rot = k[12];
    a->zone = (k[13] * 5u) >> 10;
    {
        u32 t = (k[14] * k[14]) >> 10;
        a->tone = 2690 + (s32)((t * 28800u) >> 10);               /* wet: ~300 Hz .. ~10 kHz */
        a->rdamp = 14000 + (s32)((t * 17000u) >> 10);             /* inside SPACE */
        a->edamp = 9000 + (s32)((t * 20000u) >> 10);              /* the echo's repeats */
    }
}

static inline u32 age_idx(const struct amb *a, u32 age) { return a->newest >= age ? a->newest - age : a->newest + GLEN - age; }

/* stop recording: the memory is now what was just played, trimmed of the silence around it */
static void amb_freeze(struct amb *a, int relevel)
{
    u32 was_live = a->livec;
    a->cut = 0;
    a->wpause = 0;
    u32 i;
    s32 pk = 0;
    for (i = 0; i < 8u; i++) if (a->blk[i] > pk) pk = a->blk[i];
    a->livec = 0;
    a->newest = a->gw ? a->gw - 1u : GLEN - 1u;
    a->valid = a->fresh < 4410u ? 4410u : (a->fresh > GLEN - 1u ? GLEN - 1u : a->fresh);
    if (a->fresh < a->valid) {   /* grabbed right after a cloud ended: the memory runs back into older audio - fade the join
                                    on both sides (no grain is playing yet) so nothing can click across it */
        u32 k;
        for (k = 0; k < 256u; k++) {
            u32 i;
            if (k < a->fresh) { i = age_idx(a, a->fresh - 1u - k); a->gb[i] = (s16)((a->gb[i] * (s32)k) >> 8); }
            i = age_idx(a, a->fresh + k); a->gb[i] = (s16)((a->gb[i] * (s32)k) >> 8);
        }
        a->fresh = a->valid;
    }
    a->norm = pk < 400 ? 4096 : clamp_s(12000 * 4096 / pk, 24576);
    if (a->norm < 4096) a->norm = 4096;
    if (relevel) { a->ag = a->norm << 8; a->cmean = 0; }
    if (pk >= 400) {   /* the memory starts where the sound starts and ends where it ends (0.5 s blocks) */
        s32 thr = pk / 12 > 300 ? pk / 12 : 300;
        u32 j, jn = 8u, jo = 0u, cut, keep;
        for (j = 0; j < 8u; j++) if (a->blk[(a->blk_i - j) & 7u] >= thr) { if (jn == 8u) jn = j; jo = j; }
        cut = jn == 0u ? 0u : a->blk_n + (jn - 1u) * BLK;                 /* silence after the sound */
        if (cut > 2205u) cut -= 2205u; else cut = 0u;                     /* (keep 0.1 s of its ring) */
        keep = a->blk_n + jo * BLK + 2205u;                                /* back to the oldest loud block */
        if (keep < a->valid) a->valid = keep;
        if (was_live) cut = 0;                                           /* (grains in flight rely on the newest end) */
        if (cut + 4410u < a->valid) {
            a->valid -= cut;
            a->newest = a->newest >= cut ? a->newest - cut : a->newest + GLEN - cut;
            a->cut = cut;
        }
        if (a->valid < 4410u) a->valid = 4410u;
    }
    a->rot_age = GM;
    a->rot_prev = a->gb[a->newest];
    a->ev_left = 0;
    a->ev_wait = 300;
}

/* footswitch A, tapped (acts the moment it goes down) */
static void amb_grab(struct amb *a)
{
    if (a->livec) {                                   /* live cloud -> freeze it right where it is */
        amb_freeze(a, 0);
        if (a->cstate == C_OUT) a->cstate = C_IN;
        a->grabs++;
    } else if (a->cstate == C_LIVE) {
        amb_freeze(a, 1);
        a->cenv = 0;
        a->cstate = C_IN;
        a->dpos = a->dtgt = 0;
        a->spawn_t = 1;
        a->bn_wait = 2000 + (s32)(rnd(a) % 3000u);
        a->grabs++;
    } else if (a->cstate == C_OUT) a->cstate = C_IN;
    else a->cstate = C_OUT;
}

/* footswitch A, held ~0.6 s (after the tap above already happened): was live -> fade out; otherwise -> live cloud.
 * Going live from a frozen memory: recording carries on right after its newest moment, the join faded to silence
 * on both sides (12 ms, where no grain reads), and if the memory is nearly full, recording waits until no grain that
 * is already playing could be overtaken by it. */
static void amb_hold(struct amb *a, u32 was_live)
{
    u32 j;
    if (was_live) { a->cstate = C_OUT; return; }
    if (a->cstate == C_LIVE) amb_grab(a);
    for (j = 0; j < 256u; j++) { u32 i = age_idx(a, j); a->gb[i] = (s16)((a->gb[i] * (s32)j) >> 8); }
    a->gw = a->newest + 1u >= GLEN ? 0u : a->newest + 1u;
    a->fresh = a->fresh > GLEN - 1u ? GLEN - 1u - a->cut : a->fresh - a->cut;   /* (all of it still runs on unbroken) */
    if (a->fresh < a->valid) a->fresh = a->valid;
    a->wpause = a->valid > GLEN - 17700u ? a->valid - (GLEN - 17700u) : 0u;
    a->wf = 0;
    a->ev_left = 0;
    a->livec = 1;
    a->bn_wait = 2000 + (s32)(rnd(a) % 3000u);
    if (a->cstate != C_HELD) a->cstate = C_IN;
    a->lives++;
}


/* put grain q where its whole life stays inside the memory: L ticks long, moving Ts through it (signed: + toward the
 * newest moment), its middle near age ca. In the live cloud the memory moves D = L under it meanwhile; the NERVE wobble
 * can stretch its path ~3 %: margins allow for that. 0 = it doesn't fit. */
static int grain_fit(struct amb *a, struct grain *q, u32 L, s32 Ts, s32 ca)
{
    u32 v = a->valid, rev = Ts < 0, T = (u32)(rev ? -Ts : Ts), D = a->livec ? L : 0u, ex = (T >> 5) + 16u, need, lo, hi, sa;
    /* (forward grains must also be safe if the memory freezes at any moment of their life: they always start at least
       their own path's length back from the newest moment) */
    need = 2u * GM + 2u * ex + 2u + (rev ? T + D : (T > D ? T : D));
    if (need > v) return 0;
    if (rev) { lo = GM + ex; hi = v - GM - ex - T - D; sa = (u32)(ca - (s32)((T + D) / 2u) > 0 ? ca - (s32)((T + D) / 2u) : 0); }
    else {
        s32 c2 = ca + (s32)(T / 2u) - (s32)(D / 2u);
        lo = GM + ex + T;
        hi = v - GM - ex - (D > T ? D - T : 0u);
        sa = c2 > 0 ? (u32)c2 : 0u;
    }
    if (sa < lo) sa = lo;
    if (sa > hi) sa = hi;
    q->pos = (s32)(age_idx(a, sa) << 12);
    q->ph = 0;
    q->dph = (1u << 26) / L;
    q->kk = 0; q->dinc = 0; q->bend = 0;
    return 1;
}

/* the same, but starting at a given place in the memory (the bender's repeats) */
static int grain_fit_at(struct amb *a, struct grain *q, u32 L, u32 T, u32 idx)
{
    u32 v = a->valid, D = a->livec ? L : 0u, ex = (T >> 5) + 16u, age = a->newest >= idx ? a->newest - idx : a->newest + GLEN - idx, end;
    if (age > v || age + D < T) return 0;
    end = age + D - T;
    if (age > v - GM - ex || age < GM + ex + T || end < GM + ex || end > v - GM - ex) return 0;   /* (freeze-safe too) */
    q->pos = (s32)(idx << 12);
    q->ph = 0;
    q->dph = (1u << 26) / L;
    q->kk = 0; q->dinc = 0; q->bend = 0;
    return 1;
}

static void grain_spawn(struct amb *a)
{
    struct grain *q = 0;
    u32 i, n = a->nerve, L, inc, rev, tries = 0;
    s32 ca, iv, amp, pan;
    L = a->glen;
    L = (u32)((s32)L + (((s32)L * rnds(a, (s32)((n * 1200u) >> 10))) >> 12));      /* +-30 % with NERVE */
    iv = (s32)(L / NOV);
    iv += (iv * rnds(a, 400 + (s32)((n * 2800u) >> 10))) >> 12;                    /* +-10 % .. +-78 % */
    a->spawn_t = iv < 64 ? 64 : iv;
    if (a->nact >= a->ngmax) return;
    if (rnd(a) % 1024u < (n >> 3)) return;                                          /* a mote: a gap (up to 1 in 8) */
    for (i = 0; i < NGR; i++) if (!a->gr[i].on) { q = &a->gr[i]; break; }
    if (!q) return;
    {
        u32 r = rnd(a) % 100u;
        switch (a->zone) {
        case 0: inc = 2048u; break;
        case 1: inc = r < 50u ? 2048u : 4096u; break;
        case 2: inc = 4096u; break;
        case 3: inc = r < 55u ? 4096u : r < 80u ? 6137u : 8192u; break;
        default: inc = 8192u; break;
        }
    }
    inc = (u32)((s32)inc + (((s32)inc * rnds(a, 7 + (s32)((n * 59u) >> 10))) >> 12));   /* +-3 .. +-28 cents */
    rev = rnd(a) % 1024u < 80u + ((n * 230u) >> 10);                               /* 8 % .. 30 % backwards */
    ca = a->cage + rnds(a, 441 + (s32)((n * 6000u) >> 10) + (s32)(L >> 3) + (a->livec ? 2205 : 0));
    for (;;) {
        s32 T = (s32)((L * inc) >> 12);
        if (grain_fit(a, q, L, rev ? -T : T, ca)) break;
        if (++tries > 5u || L < 512u) return;
        L >>= 1;
    }
    q->inc = rev ? -(s32)inc : (s32)inc;
    amp = 4096 - (s32)(rnd(a) % (601u + ((n * 2400u) >> 10)));
    pan = rnds(a, 1400 + (s32)((n * 2000u) >> 10));
    q->gl = (amp * (4096 - pan)) >> 12;                                             /* (grains add up in power) */
    q->gr = (amp * (4096 + pan)) >> 12;
    q->on = 1;
    a->nact++;
    a->spawns++;
}

/* THE BENDER (A's extra layer, after Qu-Bit's Data Bender - its BEND and BREAK, never its crunch): every 5-14 s, quietly
 * on top of the cloud, either a bent clone of the memory (half speed, backwards, backwards half speed, double speed, a
 * tape stop or a tape start; 0.3-2.2 s, swelling in and out) or a little piece of it repeated 2-6 times (70-250 ms). */
static struct grain *bend_slot(struct amb *a)
{
    u32 i;
    for (i = NGR; i < NGA; i++) if (!a->gr[i].on) return &a->gr[i];
    return 0;
}

static void bend_voice(struct amb *a, struct grain *q, s32 amp)
{
    s32 pan = rnds(a, 2600);
    q->gl = (amp * (4096 - pan)) >> 12;
    q->gr = (amp * (4096 + pan)) >> 12;
    q->bend = 1;
    q->on = 1;
    a->bends++;
}

static void bender_half(struct amb *a)
{
    struct grain *q;
    if (a->cstate == C_LIVE || a->cfade < 2048) { a->br_left = 0; return; }
    if (a->br_left && --a->br_t <= 0) {                          /* BREAK: the next repeat of the same moment */
        q = bend_slot(a);
        if (!q || !grain_fit_at(a, q, a->br_s + 220u, a->br_s + 220u, a->br_idx)) { a->br_left = 0; return; }
        q->inc = 4096;
        q->kk = (2048u << 12) / ((110u * q->dph) >> 12);          /* a flat window with 5 ms edges: repeats butt together */
        bend_voice(a, q, 1500 + (s32)(rnd(a) % 700u));
        a->bends--;
        a->br_t = (s32)(a->br_s >> 5);
        a->br_left--;
    }
    if (--a->bn_wait > 0) return;
    a->bn_wait = 3445 + (s32)(rnd(a) % 6200u);
    if (rnd(a) % 100u < 62u) {                                   /* BEND: a clone */
        u32 kind = rnd(a) % 6u, L;
        s32 Ts, ca = a->cage + rnds(a, 4410);
        q = bend_slot(a);
        if (!q) return;
        switch (kind) {
        case 0: L = 17640u + rnd(a) % 30870u; Ts = (s32)(L / 2u); break;          /* half speed */
        case 1: L = 13230u + rnd(a) % 22050u; Ts = -(s32)L; break;              /* backwards */
        case 2: L = 17640u + rnd(a) % 26460u; Ts = -(s32)(L / 2u); break;       /* backwards, half speed */
        case 3: L = 6615u + rnd(a) % 11025u; Ts = (s32)(2u * L); break;         /* double speed */
        default: L = 17640u + rnd(a) % 26460u; Ts = (s32)(L / 2u); break;       /* tape stop / tape start */
        }
        if (!grain_fit(a, q, L, Ts, ca)) {
            L >>= 1; Ts /= 2;
            if (!grain_fit(a, q, L, Ts, ca)) return;
        }
        switch (kind) {
        case 0: q->inc = 2048; break;
        case 1: q->inc = -4096; break;
        case 2: q->inc = -2048; break;
        case 3: q->inc = 8192; break;
        case 4: q->incq = 4096 << 12; q->dinc = -(s32)((4096u << 12) / L); q->inc = 4096; break;
        default: q->incq = 0; q->dinc = (s32)((4096u << 12) / L); q->inc = 0; break;
        }
        bend_voice(a, q, 1600 + (s32)(rnd(a) % 700u));
    } else {                                                     /* BREAK: pick a moment to repeat */
        u32 v = a->valid, S = 1543u + rnd(a) % 3969u, room = v > 2u * GM + 4u * S + 4410u ? v - 2u * GM - 4u * S - 4410u : 0u;
        if (!room) return;
        a->br_s = S;
        a->br_idx = age_idx(a, GM + 2u * S + 2205u + rnd(a) % room);
        a->br_left = 2u + rnd(a) % 5u;
        a->br_t = 1;
        a->bends++;
    }
}

static void nerve_ctl(struct amb *a, struct nerve *v, s32 base, s32 depth, u32 tape)   /* every 8 ticks; depth Q8 */
{
    u32 n = a->nerve, nn = (n * n) >> 10;
    s32 c, tgt, gt;
    if (v->shn == 0) {
        u32 span = 1930u - ((n * 1350u) >> 10);                  /* 0.7 s .. 0.21 s between targets */
        v->shn = span / 2u + rnd(a) % span;
        v->sht = rnds(a, 4096) * (s32)nn;                        /* +-64 ticks at the top */
        v->winc = tape ? 280000u + rnd(a) % 420000u : 545000u + rnd(a) % 860000u;   /* wow 0.35 .. 0.9 Hz (tape: 0.18 .. 0.45) */
        v->finc = 9350000u + rnd(a) % 7800000u;                  /* flutter 6 .. 11 Hz */
        v->amt = tape ? 4096 << 8 : (4096 - (s32)(rnd(a) % (1u + ((n * 750u) >> 10)))) << 8;   /* (tape: no tremolo) */
    } else v->shn--;
    v->s1 += (v->sht - v->s1) >> 9;
    v->sh += (v->s1 - v->sh) >> 9;
    v->wph += v->winc;
    v->fph += v->finc;
    /* dropouts: now and then the level eases down (~35 ms S-curve), hangs there 60..350 ms, and swells back (~0.15 s) -
       like tape losing its grip for a moment. Rare low on NERVE, every second or two at the top. No steps anywhere. */
    if (v->dhold) {
        if (--v->dhold == 0) v->dt = 65536;
    } else if (!tape && v->dt == 65536 && v->dg > 65000 && rnd(a) < nn * 915u) {
        v->dt = 65536 - (s32)(19660u + rnd(a) % (1u + ((n * 26000u) >> 10)));     /* down to 70 % .. 30 % */
        v->dhold = 413u + rnd(a) % 1240u;                                          /* 0.15 .. 0.6 s */
    }
    {
        u32 sh = v->dt < 65536 ? 7u : 8u;                         /* ~0.15 s down, ~0.35 s back up */
        v->d1 += (v->dt - v->d1) >> sh;
        v->dg += (v->d1 - v->dg) >> sh;
    }
    v->am += (v->amt - v->am) >> 8;
    c = v->sh + lfo_bip(v->wph) * (s32)((nn * 528u) >> 10) + lfo_bip(v->fph) * (s32)((n * (tape ? 10u : 53u)) >> 10);
    tgt = base + (s32)(((long long)c * depth) >> 8);
    v->offd = (tgt - v->off) >> 3;
    gt = (s32)(((long long)v->am * (65536 - (((65536 - v->dg) * depth) >> 8))) >> 16);
    if (gt < (256 << 8)) gt = 256 << 8;
    v->gd = (gt - v->g) >> 3;
}

/* once per half: where the cloud sits, and the rot */
static void ev_step(struct amb *a, u32 part)
{
    s16 *gb = a->gb;
    u32 len = a->ev_len, j0 = part ? len / 2u : 0u, j1 = part ? len : len / 2u, j, step = (4096u << 16) / len, ph;
    s32 lp = 0;
    ph = j0 * step;
    if (a->ev_kind == 1u) lp = gb[age_idx(a, a->ev_age + j0)];
    for (j = j0; j < j1; j++, ph += step) {
        u32 i = age_idx(a, a->ev_age + j);
        s32 w = hump(ph >> 16), s = gb[i], v;
        if (a->ev_kind == 0u) {                                  /* a hole wears in */
            s32 wd = (w * 40) >> 12;
            v = s - ((s * wd + 2048) >> 12);
        } else if (a->ev_kind == 1u) {                           /* the detail runs */
            lp += ((s - lp) * 1229) >> 12;
            v = s + (((lp - s) * ((w * 80) >> 12)) >> 12);
        } else {                                                 /* a moment slips over the next */
            s32 src = gb[age_idx(a, a->ev_age + len + j)], wt = w * 4;
            if (wt > 4096) wt = 4096;
            v = s + (((src - s) * ((wt * 80) >> 12)) >> 12);
        }
        gb[i] = (s16)v;
    }
}

static void amb_half(struct amb *a)
{
    if (a->cstate == C_LIVE) return;
    {   /* START + DRIFT */
        u32 v = a->valid, T0 = a->glen, span = v > T0 + 2u * GM ? v - T0 - 2u * GM : 0u, base, dr = a->drift;
        if (a->livec) {   /* the live cloud trails at least ~0.6 s behind you (more with START) and always dances a little */
            u32 lag = span > 13230u ? 13230u : span;
            base = GM + T0 / 2u + lag + ((a->startk * (span - lag)) >> 10);
            if (dr < 300u) dr = 300u;
        } else base = GM + T0 / 2u + ((a->startk * span) >> 10);
        if (dr < 8u) a->dpos -= a->dpos >> 6;
        else {
            s32 vel = 32 + (s32)((dr * dr) >> 8), d = a->dtgt - a->dpos;   /* Q8 ticks per half */
            if (d <= vel && d >= -vel) {
                u32 R = ((v / 2u) * dr) >> 10;
                a->dpos = a->dtgt;
                a->dtgt = ((s32)(rnd(a) % (2u * R + 1u)) - (s32)R) * 256;
            } else a->dpos += d > 0 ? vel : -vel;
        }
        a->cage = (s32)base + (a->dpos >> 8);
    }
    bender_half(a);
    {   /* ROT: a head that passes through the memory, darkening and fading it a little each time (frozen only) */
        u32 k = a->rot, n, top = a->valid - GM, age;
        s16 *gb = a->gb;
        s32 sm, fade, prev;
        if (k < 12u || a->livec) return;
        n = 1u + ((k * k) >> 12);
        sm = 100 + (s32)(k >> 2);
        fade = 4096 - (s32)((k * k) >> 13);
        prev = a->rot_prev;
        age = a->rot_age;
        while (n--) {
            u32 i;
            s32 s, y;
            if (age >= top) { age = GM; a->rot_passes++; prev = gb[age_idx(a, age)]; }
            i = age_idx(a, age);
            s = gb[i];
            y = s + (((prev - s) * sm + 2048) >> 12);
            y = (y * fade + 2048) >> 12;
            gb[i] = (s16)y;
            prev = y;
            age++;
        }
        a->rot_prev = prev;
        a->rot_age = age;
        /* and now and then an event, worked in over 0.1 .. 0.5 s in steps of 1-2 % */
        if (a->ev_left) { ev_step(a, a->ev_left & 1u); a->ev_left--; }
        else if (--a->ev_wait <= 0) {
            u32 base = 240000000u / (k * k + 1u), r = rnd(a) % 100u, v = a->valid, need;
            a->ev_wait = (s32)(base / 2u + rnd(a) % (base + 1u));
            a->ev_kind = r < 45u ? 0u : r < 75u ? 1u : 2u;
            a->ev_len = a->ev_kind == 0u ? 1323u + rnd(a) % 1764u : a->ev_kind == 1u ? 1100u + rnd(a) % 1100u : 880u + rnd(a) % 884u;
            need = 2u * GM + (a->ev_kind == 2u ? 2u : 1u) * a->ev_len + 2u;
            if (v > need) {
                a->ev_age = GM + rnd(a) % (v - need);
                a->ev_left = 2u * (40u + rnd(a) % 120u);
                a->rot_events++;
            }
        }
    }
}

static inline s32 knee(s32 x)    /* unity up to 20000, then a soft knee (no corner) into full scale */
{
    s32 ax = x < 0 ? -x : x;
    if (ax <= 20000) return x;
    ax -= 20000;
    if (ax > 300000) ax = 300000;
    ax = 20000 + (s32)(((u32)ax * 12767u) / (u32)(ax + 12767));
    return x < 0 ? -ax : ax;
}

/* log2 in Q16, for the compressor (x >= 1) */
static u32 log2_q16(u32 x)
{
    s32 e = 15;
    u32 f;
    if (!x) return 0;
    while (x >= 0x10000u) { x >>= 1; e++; }
    while (x < 0x8000u) { x <<= 1; e--; }
    f = (x - 32768u) << 1;                                       /* the mantissa's fraction, Q16 */
    f = (u32)(((unsigned long long)f * (88244u - ((22708u * f) >> 16))) >> 16);   /* log2(1+f) ~ f (1.3465 - 0.3465 f) */
    return (u32)(e << 16) + f;
}

#define COMP_T 3277u                    /* -20 dBFS */
#define EXP_T 26                        /* -62 dBFS: below this, hiss is gently turned down (up to -15 dB) */

/* the input: GAIN (noon = as it comes), then always a compressor (2.5:1 from -20 dBFS, ~6 ms / ~190 ms, +3 dB back), an EQ (+3 dB under ~160 Hz, -1 dB
   around 600 Hz, -2 dB at 4 kHz .. -3.5 dB at 10 kHz) and a gentle hiss reducer, then the soft knee */
static inline s32 amb_in(struct amb *a, s32 raw)
{
    s32 x, ax = raw < 0 ? -raw : raw, y;
    if (ax > a->peak_in) a->peak_in = ax;
    a->dci += (raw * 256 - a->dci) >> 12;                        /* no DC from the input (~2 Hz) */
    x = mq12(raw - (a->dci >> 8), a->gain >> 8);
    ax = x < 0 ? -x : x;
    if (ax * 256 > a->ce) a->ce += (ax * 256 - a->ce) >> 8; else a->ce -= (a->ce - ax * 256) >> 13;
    if ((++a->ccnt & 15u) == 0u) {
        u32 e = (u32)a->ce >> 8;
        a->ct = e > COMP_T ? (s32)exp2neg_q15(((log2_q16(e) - log2_q16(COMP_T)) * 3u) / 5u) : 32768;   /* 2.5:1 */
        if (a->ct < a->cmin) a->cmin = a->ct;
        if ((s32)e < EXP_T) { s32 t = ((s32)e << 12) / EXP_T; a->xt = t < 740 ? 740 : t; } else a->xt = 4096;
    }
    a->cg += (a->ct - a->cg) >> 5;
    a->xg += ((a->xt << 8) - a->xg) >> (a->xt << 8 > a->xg ? 5 : 11);
    x = mq12(mq15(x, a->cg), 5786);                              /* (+3 dB) */
    ax = x < 0 ? -x : x;
    if (ax > a->env_in) a->env_in = ax; else a->env_in -= a->env_in >> 12;   /* the meter */
    a->eq1 += mq15(x * 256 - a->eq1, 739);                       /* ~160 Hz */
    a->eq2 += mq15(x * 256 - a->eq2, 1597);                      /* ~350 Hz */
    a->eq3 += mq15(x * 256 - a->eq3, 4360);                      /* ~1 kHz */
    a->eq4 += mq15(x * 256 - a->eq4, 10790);                     /* ~2.8 kHz */
    y = x + mq12(a->eq1 >> 8, 1679) - mq12((a->eq3 - a->eq2) >> 8, 1024) - mq12(x - (a->eq4 >> 8), 1638);
    y = mq12(y, a->xg >> 8);
    return knee(y);                                              /* GAIN all the way up saturates, softly */
}

static inline s32 soft_out(struct amb *a, s32 x)
{
    s32 ax = x < 0 ? -x : x;
    if (ax > a->peak_out) a->peak_out = ax;
    if (ax > 32767) a->clips++;                                  /* (the limiter squashing hard: turn something down) */
    return knee(x);
}

#define GLIDE(v, t) ((v) += (((t) << 8) - (v)) >> 7)

/* one half (64 frames) of audio */
static void amb_audio(struct amb *a, volatile s32 *rx, volatile s32 *tx, int have_rx)
{
    u32 t;
    s16 *gb = a->gb, *ec = a->ec;
    for (t = 0; t < 32u; t++) {
        s32 d0, d1, xh, l0, cl = 0, cr = 0, el, er, sl, sr, wl, wr;
        d0 = amb_in(a, have_rx ? s24((u32)rx[4u * t]) >> 8 : 0);
        d1 = amb_in(a, have_rx ? s24((u32)rx[4u * t + 2u]) >> 8 : 0);
        a->lpa += mq15(d0 - a->lpa, 16700);
        l0 = a->lpa;
        a->lpa += mq15(d1 - a->lpa, 16700);
        xh = (l0 + a->lpa) >> 1;

        GLIDE(a->master, a->t_master); GLIDE(a->gain, a->t_gain); GLIDE(a->cloudlv, a->t_cloud);
        GLIDE(a->echolv, a->t_echo); GLIDE(a->spacelv, a->t_space); GLIDE(a->fb, a->t_fb);
        GLIDE(a->wmix, (a->on & FX_WARBLE) ? 4096 : 0);
        GLIDE(a->dryg, a->dryoff ? 0 : 4096);
        if ((++a->nvc & 7u) == 0u) {
            nerve_ctl(a, &a->nw, NW_BASE, 256, 1);       /* WARBLE: slow tape bends, hardly any flutter, no tremolo */
            nerve_ctl(a, &a->ne, 0, 200, 0);
            nerve_ctl(a, &a->nc, 0, 256, 0);
            nerve_ctl(a, &a->ns, 0, 64, 1);
            /* SMEAR's tugs: every 12-35 s its time is pulled 4-12 ms one way (~0.5 s) and eases back (~1.5 s) */
            if (a->tug_n) { if (--a->tug_n == 0) a->tug_t = 0; }
            else if (a->tug_w) a->tug_w--;
            else {
                s32 amt = (s32)((88u + rnd(a) % 177u) << 16);              /* 4-12 ms: a bend of ~10-35 cents, Q16 ticks */
                a->tug_t = rnd(a) & 1u ? amt : -amt;
                a->tug_n = 1650u;
                a->tug_w = 33000u + rnd(a) % 63000u;
            }
            {
                u32 sh = a->tug_t ? 9u : 10u;
                a->tug1 += (a->tug_t - a->tug1) >> sh;
                a->tug += (a->tug1 - a->tug) >> sh;
            }
        }
        a->nw.off += a->nw.offd; a->nw.g += a->nw.gd;
        a->ne.off += a->ne.offd; a->ne.g += a->ne.gd;
        a->nc.off += a->nc.offd; a->nc.g += a->nc.gd;
        a->ns.off += a->ns.offd;

        /* the memory: recording while there's no cloud, and its loudness per 0.5 s */
        {
            s32 ax = xh < 0 ? -xh : xh;
            if (ax > a->blk[a->blk_i]) a->blk[a->blk_i] = ax;
            if (++a->blk_n >= BLK) { a->blk_n = 0; a->blk_i = (a->blk_i + 1u) & 7u; a->blk[a->blk_i] = 0; }
        }
        if (a->cstate == C_LIVE || a->livec) {               /* recording (the live cloud records too) */
            if (a->wpause) a->wpause--;
            else {
                s32 v = clamp_s(xh, 32767);
                if (a->wf < 256u) { v = (v * (s32)a->wf) >> 8; a->wf++; }   /* (fading in after a join) */
                gb[a->gw] = (s16)v;
                if (a->livec) { a->newest = a->gw; if (a->valid < GLEN - 1u) a->valid++; }
                if (++a->gw >= GLEN) a->gw = 0;
                if (a->fresh < GLEN) a->fresh++;
            }
        }
        if (a->cstate != C_LIVE) {
            s32 accl = 0, accr = 0, cw = -(a->nc.off >> 13), g;
            u32 gi;
            if (a->cstate == C_IN) {
                a->cenv += 190u;                                   /* 4 s in: it blooms */
                if (a->cenv >= (1u << 24)) { a->cenv = 1u << 24; a->cstate = C_HELD; }
            } else if (a->cstate == C_OUT) {
                if (a->cenv <= 254u) {                             /* 3 s out, then back to recording */
                    a->cenv = 0; a->cstate = C_LIVE; a->nact = 0; a->wpause = 0;
                    if (!a->livec) a->fresh = 0;                    /* (after a freeze there's a gap in time) */
                    a->livec = 0;
                    for (gi = 0; gi < NGA; gi++) a->gr[gi].on = 0;
                    a->br_left = 0;
                } else a->cenv -= 254u;
            }
            a->cfade = hump(a->cenv >> 13);                        /* (the hump's rising half: a smoothstep) */
            if (a->cenv >= (1u << 24)) a->cfade = 4096;
            if (a->cstate != C_LIVE && --a->spawn_t <= 0) grain_spawn(a);
            for (gi = 0; gi < NGA; gi++) {
                struct grain *q = &a->gr[gi];
                u32 i, i1, f;
                s32 w, s0, s;
                if (!q->on) continue;
                if (q->kk) {                                       /* flat window, smooth 5 ms edges */
                    u32 t = q->ph < (1u << 25) ? q->ph : (1u << 26) - q->ph, u = ((t >> 12) * q->kk) >> 12;
                    w = hump(u > 2047u ? 2047u : u);
                } else w = hump(q->ph >> 14);
                if (q->dinc) { q->incq += q->dinc; q->inc = q->incq >> 12; }   /* tape stop / start */
                i = (u32)q->pos >> 12;
                f = (u32)q->pos & 4095u;
                i1 = i + 1u;
                if (i1 >= GLEN) i1 = 0;
#ifdef AMB_DEBUG
                amb_chk(a, i, q);
#endif
                s0 = gb[i];
                s = s0 + (((gb[i1] - s0) * (s32)f) >> 12);
                s = (s * w) >> 12;
                accl += s * q->gl;
                accr += s * q->gr;
                q->pos += q->inc + ((q->inc * cw) >> 16);
                if (q->pos >= GLENQ) q->pos -= GLENQ;
                else if (q->pos < 0) q->pos += GLENQ;
                q->ph += q->dph;
                if (q->ph >= (1u << 26)) { q->on = 0; if (!q->bend) a->nact--; }
            }
            accl >>= 12;
            accr >>= 12;
            {   /* the cloud keeps its own level (0.5x .. 4x, slow): it holds steady at CLOUD however quiet the
                   moment you froze was. It waits while the cloud wanders through silence. */
                s32 e = (accl < 0 ? -accl : accl) + (accr < 0 ? -accr : accr), want;
                a->cmean += ((e << 4) - a->cmean) >> 12;                   /* Q4, ~0.2 s */
                if (a->cmean > (40 << 4)) {
                    want = (s32)((CLOUD_TARGET << 16) / (u32)a->cmean);     /* Q12 */
                    want = want < 2048 ? 2048 : (want > 16384 ? 16384 : want);
                    a->ag += ((want << 8) - a->ag) >> 12;
                }
            }
            g = mq12(mq12(mq12(a->cfade, a->ag >> 8), a->cloudlv >> 8), a->nc.g >> 8);
            cl = mq12(accl, g);
            cr = mq12(accr, g);
            {
                s32 e = ((cl < 0 ? -cl : cl) + (cr < 0 ? -cr : cr)) >> 1;
                if (e > a->cl_env) a->cl_env = e; else if (a->cl_env > 0) a->cl_env -= (a->cl_env >> 10) + 1;
            }
        }

        /* SMEAR (footswitch C): a long dark echo whose notes swell in and blur more with every repeat */
        {
            u32 di, fr, rp;
            s32 s0, s1, w, fbv, dq, g;
            if (a->ec_d < a->ec_target) a->ec_d += (a->ec_target - a->ec_d) >> 10;
            else a->ec_d -= (a->ec_d - a->ec_target) >> 10;
            dq = (s32)a->ec_d + (a->ne.off >> 8) + (a->tug >> 8);
            if (dq > (32200 << 8)) dq = 32200 << 8;
            di = (u32)dq >> 8;
            fr = (u32)dq & 255u;
            rp = a->ec_w - di;
            s0 = ec[rp & EC_MASK]; s1 = ec[(rp - 1u) & EC_MASK];
            el = s0 + (((s1 - s0) * (s32)fr) >> 8);
            rp -= 331u;
            s0 = ec[rp & EC_MASK]; s1 = ec[(rp - 1u) & EC_MASK];
            er = s0 + (((s1 - s0) * (s32)fr) >> 8);
            a->ec_lp += mq15(el - a->ec_lp, a->edamp);
            fbv = mq12(a->ec_lp, a->fb >> 8);
            a->ec_hp += (fbv * 256 - a->ec_hp) >> 5;
            fbv -= a->ec_hp >> 8;
            {   /* the swell: each note you play is heard coming (46 ms look-ahead), the feed eases down to 10 % just before it
                   arrives, then the note swells in over SWELL - so the repeats never attack */
                s32 ax = xh < 0 ? -xh : xh, xd = a->la[a->la_i], gs;
                a->la[a->la_i] = xh;
                a->la_i = (a->la_i + 1u) & 1023u;
                if (ax > a->sw_f) a->sw_f += (ax - a->sw_f) >> 4; else a->sw_f -= a->sw_f >> 9;
                a->sw_s += (a->sw_f * 16 - a->sw_s) >> 11;
                if (a->sw_ref) a->sw_ref--;
                else if (a->sw_rate && a->sw_f > (a->sw_s >> 3) + 150) { a->sw_duck = 1024u; a->sw_g0 = a->sw_g; a->sw_ref = 1100u; a->swells++; }
                if (a->sw_duck) {                    /* an S-curve down to 10 % over the 46 ms before the note arrives */
                    u32 fl = 1677722u, g0 = a->sw_g0 > fl ? a->sw_g0 : fl;
                    a->sw_duck--;
                    a->sw_g = g0 - ((g0 - fl) >> 12) * (u32)hump((1024u - a->sw_duck) * 2u - 1u);
                }
                else if (!a->sw_rate) a->sw_g = 1u << 24;
                else if (a->sw_g < (1u << 24)) { a->sw_g += a->sw_rate; if (a->sw_g > (1u << 24)) a->sw_g = 1u << 24; }
                {   /* (between straight and squared: a slow start, like a volume pedal) */
                    s32 g1 = (s32)(a->sw_g >> 12);
                    gs = (g1 + ((g1 * g1) >> 12)) >> 1;
                }
                w = ((a->on & FX_ECHO) ? mq12(mq12(xd, gs), a->sw_mk) + ((cl + cr) >> 1) : 0) + fbv;   /* (a long swell loses the
                    note's start, so it's given back up to ~+9 dB) */
            }
            {   /* the smear: three allpass blurs on everything that goes in, so each repeat is a little more washed */
                u32 k;
                for (k = 0; k < 3u; k++) {
                    s32 *b = a->sm[k], d = b[a->smp[k]], yv = d - mq15r(w, 20320);
                    b[a->smp[k]] = w + mq15r(yv, 20320);
                    if (++a->smp[k] >= SM_N[k]) a->smp[k] = 0;
                    w = yv;
                }
            }
            {   /* tape: a soft knee instead of a wall */
                s32 aw = w < 0 ? -w : w;
                if (aw > 16384) { aw = 16384 + ((aw - 16384) >> 1); if (aw > 32767) aw = 32767; w = w < 0 ? -aw : aw; }
            }
            ec[a->ec_w & EC_MASK] = (s16)w;
            a->ec_w++;
            if (++a->ec_cnt >= di) { a->ec_cnt = 0; a->ec_flash = 1100u; } else if (a->ec_flash) a->ec_flash--;
            g = mq12(a->echolv >> 8, a->ne.g >> 8);
            el = mq12(el, g);
            er = mq12(er, g);
        }

        /* SPACE */
        {
            s32 rin, y[8], h[8], m0, m1;
            u32 k, ri[8];
            rin = (a->on & FX_SPACE) ? xh + mq12(cl + cr, 2900) + mq12(el + er, 2900) : 0;
            a->sp_hp += (rin * 256 - a->sp_hp) >> 6;              /* nothing under ~55 Hz goes in (state Q8) */
            rin = (rin - (a->sp_hp >> 8)) >> 2;
            for (k = 0; k < 4u; k++) {
                s32 *b = a->df[k], d, yv;
                u32 i = a->dp[k];
                d = b[i];
                yv = d - mq15r(rin, 20320);
                b[i] = rin + mq15r(yv, 20320);
                if (++i >= DF_N[k]) i = 0;
                a->dp[k] = i;
                rin = yv;
            }
            a->mph0 += 58000u; a->mph1 += 91500u;
            m0 = (32 << 8) + ((lfo_bip(a->mph0) * 3) >> 3) + (a->ns.off >> 8);
            m1 = (32 << 8) + ((lfo_bip(a->mph1) * 3) >> 3) - (a->ns.off >> 8);
            m0 = m0 < (4 << 8) ? (4 << 8) : (m0 > (60 << 8) ? (60 << 8) : m0);
            m1 = m1 < (4 << 8) ? (4 << 8) : (m1 > (60 << 8) ? (60 << 8) : m1);
            for (k = 0; k < 8u; k++) {
                s32 *b = a->fl[k];
                u32 i = a->fp[k];
                if (k == 0u || k == 5u) {                           /* these two breathe: a moving read point */
                    u32 S = FD_N[k] + FD_PAD, D = FD_N[k] + ((u32)(k ? m1 : m0) >> 8), fr = (u32)(k ? m1 : m0) & 255u, r0, r1;
                    s32 s0;
                    r0 = i >= D ? i - D : i + S - D;
                    r1 = r0 ? r0 - 1u : S - 1u;
                    s0 = b[r0];
                    y[k] = s0 + (((b[r1] - s0) * (s32)fr) >> 8);
                } else y[k] = b[i];
                ri[k] = i;
                a->flp[k] += mq15r(y[k] - a->flp[k], a->rdamp);
                y[k] = a->flp[k];
            }
            {
                s32 a0 = y[0] + y[1], a1 = y[0] - y[1], a2 = y[2] + y[3], a3 = y[2] - y[3];
                s32 a4 = y[4] + y[5], a5 = y[4] - y[5], a6 = y[6] + y[7], a7 = y[6] - y[7];
                s32 b0 = a0 + a2, b2 = a0 - a2, b1 = a1 + a3, b3 = a1 - a3;
                s32 b4 = a4 + a6, b6 = a4 - a6, b5 = a5 + a7, b7 = a5 - a7;
                h[0] = b0 + b4; h[4] = b0 - b4; h[1] = b1 + b5; h[5] = b1 - b5;
                h[2] = b2 + b6; h[6] = b2 - b6; h[3] = b3 + b7; h[7] = b3 - b7;
            }
            for (k = 0; k < 8u; k++) {
                s32 *b = a->fl[k];
                u32 i = ri[k], S = FD_N[k] + ((k == 0u || k == 5u) ? FD_PAD : 0u);
                b[i] = clamp_s(((k & 1u) ? -rin : rin) + mq15r(h[k], a->fdg[k]), 1 << 22);
                if (++i >= S) i = 0;
                a->fp[k] = i;
            }
            sl = (y[0] + y[2] - y[4] + y[6]) >> 1;
            sr = (y[1] - y[3] + y[5] + y[7]) >> 1;
            sl = mq12(mq12(sl, a->spacelv >> 8), a->rcomp);
            sr = mq12(mq12(sr, a->spacelv >> 8), a->rcomp);
        }

#ifdef AMB_DEBUG
        amb_dbg(xh, cl, cr, el, er, sl, sr);
#endif
        /* the wet side: no DC, TONE, then back up to 44.1 kHz (a 4-point cubic halfway sample: the images of
           bright transients stay ~25 dB lower than with straight lines) */
        wl = cl + el + sl;
        wr = cr + er + sr;
        a->dcl += (wl * 256 - a->dcl) >> 10;                      /* (states Q8: no stuck offsets) */
        a->dcr += (wr * 256 - a->dcr) >> 10;
        a->tl += mq15r(wl - (a->dcl >> 8) - a->tl, a->tone);
        a->tr += mq15r(wr - (a->dcr >> 8) - a->tr, a->tone);
        {
            s32 o[4], f;
            u32 j;
            o[0] = a->hl[1]; o[1] = a->hr[1];
            o[2] = (9 * (a->hl[1] + a->hl[0]) - a->hl[2] - a->tl) >> 4;
            o[3] = (9 * (a->hr[1] + a->hr[0]) - a->hr[2] - a->tr) >> 4;
            a->hl[2] = a->hl[1]; a->hl[1] = a->hl[0]; a->hl[0] = a->tl;
            a->hr[2] = a->hr[1]; a->hr[1] = a->hr[0]; a->hr[0] = a->tr;
            for (f = 0; f < 2; f++) {
                s32 L, R, dry = mq12(f ? d1 : d0, a->dryg >> 8);
                a->ol += mq15r(o[2 * f] - a->ol, 22938);
                a->orr += mq15r(o[2 * f + 1] - a->orr, 22938);
                /* MASTER, then the soft limiter - nothing hard-clips anywhere inside (the sum can be far over full
                   scale before MASTER turns it down) */
                L = soft_out(a, mq12(dry + a->ol, a->master >> 8));
                R = soft_out(a, mq12(dry + a->orr, a->master >> 8));
                /* WARBLE: everything through the old tape */
                j = a->wb_w & WB_MASK;
                a->wbl[j] = (s16)L;
                a->wbr[j] = (s16)R;
                if (a->wmix > 256) {
                    s32 oq = a->nw.off >> 7, s0, wlv, wrv, mx = a->wmix >> 8, gq = a->nw.g >> 8;
                    u32 di = (u32)oq >> 8, fr = (u32)oq & 255u, r0 = (a->wb_w - di) & WB_MASK, r1 = (r0 - 1u) & WB_MASK;
                    s0 = a->wbl[r0]; wlv = s0 + (((a->wbl[r1] - s0) * (s32)fr) >> 8);
                    s0 = a->wbr[r0]; wrv = s0 + (((a->wbr[r1] - s0) * (s32)fr) >> 8);
                    wlv = mq12(wlv, gq); wrv = mq12(wrv, gq);
                    L += mq12(wlv - L, mx);
                    R += mq12(wrv - R, mx);
                }
                a->wb_w++;
                L = clamp_s(L, 32767);
                R = clamp_s(R, 32767);
                tx[4u * t + 2u * (u32)f] = L * 256;
                tx[4u * t + 2u * (u32)f + 1u] = R * 256;
            }
        }
    }
}

#endif
