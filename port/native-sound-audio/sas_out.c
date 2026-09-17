/* sas_out.c - play the DS sound channels through the PSP's own voice mixer, sceSasCore (VITAPOKE_SAS).
 *
 * The DS sound engine keeps running on the game thread exactly as in muted mode: the sequencer and
 * exchannel code write the 16 channel registers (s_SIM_*), and SIM_Audio_AdvanceChannelSilent advances
 * each channel's position and end flag arithmetically, so song timing never depends on this file.
 *
 * After every sound pump the game thread copies the 16 channel registers into a snapshot ring stamped
 * with DS time. The audio thread replays those snapshots onto 16 sceSas voices when its playback clock
 * reaches their DS time, asks sceSasCore to mix 256-sample grains at 44.1 kHz, and outputs them. On a
 * real PSP sceSasCore mixes on the Media Engine, so the main CPU only pays for sample conversion (done
 * once per distinct sample and cached) and this thread's bookkeeping.
 *
 * Sample conversion reproduces sim_audio.cpp exactly: PCM8 -> s16 (<<8), PCM16 as is, IMA-ADPCM decoded
 * with the same table/clamps and a loop point at nibble 2*loopStart (the decoder state saved there is
 * the state it restores, so the decoded buffer loops identically). PSG is its 8-step duty pattern and
 * noise is the full 32767-step LFSR sequence, both as looping PCM.
 */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psputility.h>
#include <pspsascore.h>
#include <pspthreadman.h>
#include <string.h>

extern void VitaNativeMemLog(const char *fmt, ...);
extern u32 s_SIM_sndcnt[16];
extern u8 *s_SIM_sndsad[16];
extern u16 s_SIM_sndtmr[16];
extern u16 s_SIM_sndpnt[16];
extern u32 s_SIM_sndlen[16];
extern u32 VitaNativeSasStartGen[16];      /* sim_audio.cpp: bumped by SIM_Audio_StartChannel */

#define SAS_GRAIN        256
#define SAS_RATE         44100
#define DS_CLOCK         16756991u        /* DS sound channel timer clock: sample rate = DS_CLOCK / timer */
#define SYS_CLOCK        33514000u        /* OS_SYSTEM_CLOCK: units of the snapshot time stamps (174592 = one 5.2 ms pump) */
#define SNAP_RING        256              /* power of two; one snapshot per 10.4 ms pump */
#define SNAP_MASK        (SNAP_RING - 1)
#ifndef LATENCY_PUMPS                     /* overridable: make EXTRA_CFLAGS=-DLATENCY_PUMPS=16 (audio A/B builds) */
#define LATENCY_PUMPS    10               /* start playback this many pumps behind the newest snapshot (> one 33 ms frame of pumps, so the game refills before playback catches up) */
#endif
#define CACHE_BUDGET     (3u << 20)       /* bytes of converted samples kept (LRU) */
#define PCM_MAX_SAMPLES  0x10000          /* sceSasSetVoicePCM limit */

typedef struct { u32 cnt; u8 *sad; u16 tmr, pnt; u32 len, gen; } SasChan;
typedef struct { u64 time; SasChan ch[16]; } SasSnap;

static SasSnap s_snaps[SNAP_RING];
static volatile u32 s_snapW, s_snapR;
static u64 s_dsTime;
static int s_ready;
static SceSasCore s_core __attribute__((aligned(64)));
static short s_grainBuf[2][SAS_GRAIN * 4] __attribute__((aligned(64)));   /* two buffers: the audio hardware may still read the previous one */
static u32 st_coreErr;
static SceUID s_thread = -1;
static int s_audioCh = -1;

/* stats for the card log */
static u32 st_dropSnaps, st_keyOns, st_convBytes, st_cacheHits, st_cacheMiss, st_evict, st_decimated, st_pitchClamp, st_skipped, st_underfeed;
static u32 st_threadUs, st_grains; static int st_peak; static u32 st_pitchClamp, st_hdrLogs;

/* ---- converted-sample cache -------------------------------------------------------------------- */
typedef struct SasSample {
    const u8 *src; u32 srcLen; u8 type; u8 duty; u8 shiftDown; u8 used;
    u32 hash;
    short *data; u32 count; int loop;
    u32 lastUse; u32 bytes; SceUID block; u32 minPeriod;
} SasSample;
#define CACHE_SLOTS 384
static SasSample s_cache[CACHE_SLOTS];
static u32 s_cacheBytes, s_useClock;
static SasSample *s_voiceSample[16];

static u32 HashHead(const u8 *p, u32 n)
{
    u32 h = 2166136261u; u32 m = n < 64 ? n : 64;
    for (u32 i = 0; i < m; i++) h = (h ^ p[i]) * 16777619u;
    if (n > 64) for (u32 i = n - 32; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h ^ n;
}

static short *AllocSamples(u32 count, SceUID *block)
{
    SceUID id = sceKernelAllocPartitionMemory(2, "sas_sample", PSP_SMEM_Low, count * sizeof(short), NULL);
    if (id < 0) return NULL;
    *block = id;
    return (short *)sceKernelGetBlockHeadAddr(id);
}

static int SampleInUse(const SasSample *s)
{
    for (int v = 0; v < 16; v++) if (s_voiceSample[v] == s) return 1;
    return 0;
}

static void EvictFor(u32 bytes)
{
    while (s_cacheBytes + bytes > CACHE_BUDGET) {
        SasSample *oldest = NULL;
        for (int i = 0; i < CACHE_SLOTS; i++) {
            SasSample *s = &s_cache[i];
            if (s->used && !SampleInUse(s) && (!oldest || s->lastUse < oldest->lastUse)) oldest = s;
        }
        if (!oldest) return;
        sceKernelFreePartitionMemory(oldest->block);
        s_cacheBytes -= oldest->bytes;
        memset(oldest, 0, sizeof(*oldest));
        st_evict++;
    }
}

static const u16 s_adpcmTable[89] = {
    0x0007,0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x0010,0x0011,0x0013,0x0015,0x0017,0x0019,0x001C,0x001F,
    0x0022,0x0025,0x0029,0x002D,0x0032,0x0037,0x003C,0x0042,0x0049,0x0050,0x0058,0x0061,0x006B,0x0076,0x0082,0x008F,
    0x009D,0x00AD,0x00BE,0x00D1,0x00E6,0x00FD,0x0117,0x0133,0x0151,0x0173,0x0198,0x01C1,0x01EE,0x0220,0x0256,0x0292,
    0x02D4,0x031C,0x036C,0x03C3,0x0424,0x048E,0x0502,0x0583,0x0610,0x06AB,0x0756,0x0812,0x08E0,0x09C3,0x0ABD,0x0BD0,
    0x0CFF,0x0E4C,0x0FBA,0x114C,0x1307,0x14EE,0x1706,0x1954,0x1BDC,0x1EA5,0x21B6,0x2515,0x28CA,0x2CDF,0x315B,0x364B,
    0x3BB9,0x41B2,0x4844,0x4F7E,0x5771,0x602F,0x69CE,0x7462,0x7FFF };
static const s8 s_adpcmIndex[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const s16 s_psgTable[8][8] = {
    {-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF,-0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF},
    {-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF,-0x7FFF} };

/* Build (or find) the PCM buffer for a channel. type: 0 PCM8, 1 PCM16, 2 ADPCM, 3 PSG, 4 noise. */
static SasSample *GetSample(int type, const SasChan *c)
{
    u32 period = 0x10000u - c->tmr; if (!period) period = 1;
    int duty = (c->cnt >> 24) & 7;
    int looping = ((c->cnt >> 27) & 3) & 1;
    u32 total = (u32)c->pnt + c->len;             /* bytes (PCM8/ADPCM) or bytes of s16 (PCM16) */
    u32 hash = 0;
    if (type <= 2) {
        if (!c->sad || total < 16) return NULL;
        hash = HashHead(c->sad, total);
    }
    for (int i = 0; i < CACHE_SLOTS; i++) {
        SasSample *s = &s_cache[i];
        if (!s->used || s->type != type) continue;
        if (s->minPeriod > period) continue;   /* built for a lower top pitch: would need more halving */
        if (type <= 2 ? (s->src == c->sad && s->srcLen == total && s->hash == hash && (s->loop >= 0) == (looping != 0))
                      : (type == 4 || s->duty == duty)) {
            s->lastUse = ++s_useClock; st_cacheHits++; return s;
        }
    }
    st_cacheMiss++;
    u32 count; int loop;
    if (type == 0) { count = total; loop = looping ? (int)c->pnt : -1; }
    else if (type == 1) { count = total / 2; loop = looping ? (int)(c->pnt / 2) : -1; }
    else if (type == 2) { count = total * 2 - 8; loop = looping ? (int)(c->pnt * 2) - 8 : -1; if (loop < 0 && looping) loop = 0; }
    else if (type == 3) { count = 64; loop = 0; }
    else { count = 32767; loop = 0; }
    if (count == 0) return NULL;

    /* sceSas PCM voices hold at most 0x10000 samples: halve long samples (a halved buffer plays at half the pitch). */
    u8 shiftDown = 0; u32 outCount = count;
    while (outCount > PCM_MAX_SAMPLES) { outCount = (outCount + 1) / 2; shiftDown++; }
    /* sceSas pitch tops out at 4x: very high DS sample rates also need halving (each halving halves the pitch needed). */
    while (shiftDown < 6 && ((u64)DS_CLOCK * PSP_SAS_PITCH_BASE >> shiftDown) / ((u64)SAS_RATE * period) > PSP_SAS_PITCH_MAX) {
        if (outCount <= 64) break;
        outCount = (outCount + 1) / 2; shiftDown++;
    }
    /* the smallest timer period this buffer can still play without clamping */
    u32 minPeriod = (u32)(((u64)DS_CLOCK * PSP_SAS_PITCH_BASE >> shiftDown) / ((u64)SAS_RATE * PSP_SAS_PITCH_MAX)) + 1;
    if (shiftDown) st_decimated++;

    u32 bytes = outCount * sizeof(short);
    EvictFor(bytes);
    SasSample *slot = NULL;
    for (int i = 0; i < CACHE_SLOTS && !slot; i++) if (!s_cache[i].used) slot = &s_cache[i];
    if (!slot) { /* table full: drop the least recently used unused entry */
        for (int i = 0; i < CACHE_SLOTS; i++) { SasSample *s = &s_cache[i]; if (!SampleInUse(s) && (!slot || s->lastUse < slot->lastUse)) slot = s; }
        if (!slot) return NULL;
        sceKernelFreePartitionMemory(slot->block); s_cacheBytes -= slot->bytes; memset(slot, 0, sizeof(*slot)); st_evict++;
    }
    SceUID block; short *out = AllocSamples(outCount, &block);
    if (!out) return NULL;

    u32 step = 1u << shiftDown;
    if (type == 0) {
        const s8 *p = (const s8 *)c->sad;
        for (u32 i = 0, j = 0; j < outCount; i += step, j++) out[j] = (short)(p[i] << 8);
    } else if (type == 1) {
        const s16 *p = (const s16 *)(const void *)c->sad;
        for (u32 i = 0, j = 0; j < outCount; i += step, j++) out[j] = p[i];
    } else if (type == 2) {
        const u8 *p = c->sad;
        u32 header = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
        s32 val = (s32)(s16)(header & 0xFFFF), index = (header >> 16) & 0x7F;
        if (index > 88) index = 88;
        u32 j = 0;
        for (u32 pos = 8; pos < total * 2 && j < outCount; pos++) {
            u8 nib = (pos & 1) ? (p[pos >> 1] >> 4) : (p[pos >> 1] & 0xF);
            u16 t = s_adpcmTable[index]; u16 diff = t >> 3;
            if (nib & 1) diff += t >> 2;
            if (nib & 2) diff += t >> 1;
            if (nib & 4) diff += t;
            if (nib & 8) { val -= diff; if (val < -0x7FFF) val = -0x7FFF; }
            else         { val += diff; if (val >  0x7FFF) val =  0x7FFF; }
            index += s_adpcmIndex[nib & 7];
            if (index < 0) index = 0; else if (index > 88) index = 88;
            if (((pos - 8) & (step - 1)) == 0) out[j++] = (short)val;
        }
        while (j < outCount) out[j++] = 0;
    } else if (type == 3) {
        for (u32 j = 0; j < outCount; j++) out[j] = s_psgTable[duty][j & 7];
    } else {
        u16 lfsr = 0x7FFF;
        for (u32 j = 0; j < outCount; j++) {
            if (lfsr & 1) { lfsr = (u16)((lfsr >> 1) ^ 0x6000); out[j] = -0x7FFF; }
            else          { lfsr >>= 1; out[j] = 0x7FFF; }
        }
    }
    /* sceSasCore mixes on the Media Engine, which reads this buffer straight from RAM: flush the CPU data cache
       first, or it can read stale bytes (static or silence on hardware, fine in emulators). */
    sceKernelDcacheWritebackRange(out, outCount * sizeof(short));
    slot->used = 1; slot->type = (u8)type; slot->duty = (u8)duty; slot->shiftDown = shiftDown; slot->minPeriod = minPeriod;
    slot->src = c->sad; slot->srcLen = total; slot->hash = hash;
    slot->data = out; slot->count = outCount; slot->loop = loop >= 0 ? (int)(loop >> shiftDown) : -1;
    slot->bytes = bytes; slot->block = block; slot->lastUse = ++s_useClock;
    s_cacheBytes += bytes; st_convBytes += bytes;
    return slot;
}

/* ---- voice state -------------------------------------------------------------------------------- */
static struct { u32 gen; u32 cnt; u16 tmr; u8 keyed; int pitch, vl, vr; } s_cur[16];

static int PitchFor(u16 tmr, u8 shiftDown)
{
    u32 period = 0x10000u - tmr;
    if (!period) period = 1;
    u64 pitch = ((u64)DS_CLOCK * PSP_SAS_PITCH_BASE >> shiftDown) / ((u64)SAS_RATE * period);
    if (pitch < PSP_SAS_PITCH_MIN) pitch = PSP_SAS_PITCH_MIN;
    if (pitch > PSP_SAS_PITCH_MAX) { pitch = PSP_SAS_PITCH_MAX; st_pitchClamp++; }
    return (int)pitch;
}

/* DS mixer (sim_audio.cpp): ((s >> div) * vol / 128), pan: L*(128-pan)>>10, R*pan>>10, then << gain.
 * DS hardware volume divider: 0 = /1, 1 = /2, 2 = /4, 3 = /16. */
#ifndef PSP_SAS_GAIN_SHIFT
#define PSP_SAS_GAIN_SHIFT 2
#endif
static void VolumeFor(u32 cnt, int *l, int *r)
{
    static const u8 div[4] = { 0, 1, 2, 4 };
    int vol = cnt & 0x7F, pan = (cnt >> 16) & 0x7F, d = div[(cnt >> 8) & 3];
    int base = (PSP_SAS_VOLUME_MAX * vol) >> d;                       /* 0..0x1000*127/128 */
    int lv = (int)(((s64)base * (128 - pan) << PSP_SAS_GAIN_SHIFT) / (128 * 1024 / 128) / 128);
    int rv = (int)(((s64)base * pan << PSP_SAS_GAIN_SHIFT) / (128 * 1024 / 128) / 128);
    if (lv > PSP_SAS_VOLUME_MAX) lv = PSP_SAS_VOLUME_MAX;
    if (rv > PSP_SAS_VOLUME_MAX) rv = PSP_SAS_VOLUME_MAX;
    *l = lv; *r = rv;
}

static void ApplyChannel(int v, const SasChan *c)
{
    int en = (int)((c->cnt >> 31) & 1);
    int fmt = (c->cnt >> 29) & 3, type = fmt;
    if (fmt == 3) { if (v >= 14) type = 4; else if (v >= 8) type = 3; else en = 0; }

    if (en && (c->gen != s_cur[v].gen || !s_cur[v].keyed)) {
        SasSample *s = GetSample(type, c);
        if (s) {
            if (s_cur[v].keyed) __sceSasSetKeyOff(&s_core, v);
            s_voiceSample[v] = s;
            int rp = __sceSasSetVoicePCM(&s_core, v, s->data, (int)s->count, s->loop);
            int pitch = PitchFor(c->tmr, s->shiftDown), l, r;
            VolumeFor(c->cnt, &l, &r);
            int rt = __sceSasSetPitch(&s_core, v, pitch);
            int rv = __sceSasSetVolume(&s_core, v, l, r, 0, 0);
            int rk = __sceSasSetKeyOn(&s_core, v);
            if (type <= 2 && st_hdrLogs < 10) { const u8 *h = c->sad - 12; u16 hrate = (u16)(h[2] | (h[3] << 8)), htimer = (u16)(h[4] | (h[5] << 8)), hls = (u16)(h[6] | (h[7] << 8)); u32 hll = (u32)h[8] | ((u32)h[9] << 8) | ((u32)h[10] << 16) | ((u32)h[11] << 24);
                VitaNativeMemLog("[AUDIO-SAS] hdr v=%d fmt=%u loopflag=%u rate=%u timer=%u rate*timer=%u loopStart=%u loopLen=%u | chan period=%u pnt=%u len=%u", v, h[0], h[1], hrate, htimer, (u32)hrate * htimer, hls, hll, 0x10000u - c->tmr, c->pnt, c->len); st_hdrLogs++; }
            if (st_keyOns < 12 || rp < 0 || rt < 0 || rv < 0 || rk < 0)
                VitaNativeMemLog("[AUDIO-SAS] keyon v=%d type=%d count=%u loop=%d pitch=%d vol=%d/%d rc pcm=%08x pitch=%08x vol=%08x key=%08x", v, type, s->count, s->loop, pitch, l, r, rp, rt, rv, rk);
            s_cur[v].keyed = 1; s_cur[v].pitch = pitch; s_cur[v].vl = l; s_cur[v].vr = r;
            st_keyOns++;
        } else {
            st_skipped++;
        }
    } else if (!en && s_cur[v].keyed) {
        __sceSasSetKeyOff(&s_core, v);
        s_cur[v].keyed = 0;
        s_voiceSample[v] = NULL;
    } else if (en && s_cur[v].keyed) {
        if (c->tmr != s_cur[v].tmr) {
            int pitch = PitchFor(c->tmr, s_voiceSample[v] ? s_voiceSample[v]->shiftDown : 0);
            if (pitch != s_cur[v].pitch) { __sceSasSetPitch(&s_core, v, pitch); s_cur[v].pitch = pitch; }
        }
        if ((c->cnt ^ s_cur[v].cnt) & 0x007F037Fu) {
            int l, r; VolumeFor(c->cnt, &l, &r);
            if (l != s_cur[v].vl || r != s_cur[v].vr) { __sceSasSetVolume(&s_core, v, l, r, 0, 0); s_cur[v].vl = l; s_cur[v].vr = r; }
        }
    }
    s_cur[v].gen = c->gen; s_cur[v].cnt = c->cnt; s_cur[v].tmr = c->tmr;
}

static void ConfigureEnvelope(int v)
{
    /* The DS sound engine computes its own envelopes into the channel volume, so every voice plays
       flat: full height the moment it is keyed on, held while keyed, silent at once when keyed off. */
    /* The PSP only accepts an even (increasing) attack curve and odd (decreasing) decay/release curves, so
       "flat" is: attack linear-increase at the maximum rate (full height in one step), decay and sustain
       linear-decrease at rate 0 (held), release direct at 0 (silent at once), sustain level at maximum. */
    int r1 = __sceSasSetADSRmode(&s_core, v, PSP_SAS_ADSR_EVERYTHING,
                        PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE, PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE,
                        PSP_SAS_ADSR_CURVE_MODE_LINEAR_DECREASE, PSP_SAS_ADSR_CURVE_MODE_DIRECT);
    int r2 = __sceSasSetADSR(&s_core, v, PSP_SAS_ADSR_EVERYTHING, PSP_SAS_ENVELOPE_HEIGHT_MAX, 0, 0, 0);
    int r3 = __sceSasSetSL(&s_core, v, PSP_SAS_ENVELOPE_HEIGHT_MAX);
    if (v == 0 || r1 < 0 || r2 < 0 || r3 < 0) VitaNativeMemLog("[AUDIO-SAS] envelope voice %d: mode %08x rates %08x sl %08x", v, r1, r2, r3);
}

/* ---- audio thread ------------------------------------------------------------------------------- */
static int SasThread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    u64 playTime = 0; u64 frac = 0; int started = 0;
    const u64 grainNum = (u64)SAS_GRAIN * SYS_CLOCK;    /* snapshot-clock units per grain = grainNum / SAS_RATE */
    for (;;) {
        u32 t0 = sceKernelGetSystemTimeLow();
        u32 w = s_snapW, r = s_snapR;
        if (!started && w != r) {
            u32 newest = (w - 1) & SNAP_MASK;
            u64 lat = (u64)LATENCY_PUMPS * 174592u;
            playTime = s_snaps[newest].time > lat ? s_snaps[newest].time - lat : 0;
            started = 1;
        }
        if (started) {
            /* too far behind the game (e.g. after a stall): jump to a few pumps before the newest */
            if (((w - r) & SNAP_MASK) > 32) {
                u32 keep = LATENCY_PUMPS;
                while (((w - r) & SNAP_MASK) > keep) { u32 i = r & SNAP_MASK; for (int v = 0; v < 16; v++) ApplyChannel(v, &s_snaps[i].ch[v]); playTime = s_snaps[i].time; r++; }
                s_snapR = r;
            }
            while (r != w && s_snaps[r & SNAP_MASK].time <= playTime) {
                const SasSnap *s = &s_snaps[r & SNAP_MASK];
                for (int v = 0; v < 16; v++) ApplyChannel(v, &s->ch[v]);
                r++; s_snapR = r;
            }
            if (r == w) {
                /* game has not produced newer sound state yet (load, stall): hold the clock */
                u32 newest = (w - 1) & SNAP_MASK;
                if (playTime > s_snaps[newest].time + 174592u) { playTime = s_snaps[newest].time + 174592u; st_underfeed++; }
            }
        }
        static int gi; short *s_grain = s_grainBuf[gi ^= 1];
        sceKernelDcacheInvalidateRange(s_grain, SAS_GRAIN * 2 * sizeof(short));
        { int rc = __sceSasCore(&s_core, s_grain); if (rc) st_coreErr++; }
        sceKernelDcacheWritebackInvalidateRange(s_grain, SAS_GRAIN * 2 * sizeof(short));
        { static u32 g; int pk = 0; for (int i = 0; i < SAS_GRAIN * 2; i++) { int a = s_grain[i] < 0 ? -s_grain[i] : s_grain[i]; if (a > pk) pk = a; }
          if (pk > st_peak) st_peak = pk;
          if ((++g % 344) == 0) { int h[32]; __sceSasGetAllEnvelopeHeights(&s_core, h); int keyed = 0; for (int v = 0; v < 16; v++) keyed += s_cur[v].keyed;
            VitaNativeMemLog("[AUDIO-SAS] grain %u peak %d keyed %d env0 %08x env1 %08x env2 %08x env3 %08x", g, st_peak, keyed, h[0], h[1], h[2], h[3]); st_peak = 0; } }
        frac += grainNum; playTime += frac / SAS_RATE; frac %= SAS_RATE;
        st_threadUs += sceKernelGetSystemTimeLow() - t0; st_grains++;
        sceAudioOutputPannedBlocking(s_audioCh, VITAPOKE_AUDIO_VOLUME_MAX, VITAPOKE_AUDIO_VOLUME_MAX, s_grain);
    }
    return 0;
}

int VitaNativeSasInit(void)
{
    int rc = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    if (rc < 0 && rc != (int)0x80020139) VitaNativeMemLog("[AUDIO-SAS] load AVCODEC %08x", rc);
    rc = sceUtilityLoadModule(PSP_MODULE_AV_SASCORE);
    if (rc < 0 && rc != (int)0x80020139) { VitaNativeMemLog("[AUDIO-SAS] load SASCORE failed %08x", rc); return -1; }
    rc = __sceSasInit(&s_core, SAS_GRAIN, 16, PSP_SAS_OUTPUTMODE_STEREO, SAS_RATE);
    if (rc < 0) { VitaNativeMemLog("[AUDIO-SAS] __sceSasInit failed %08x", rc); return -1; }
    for (int v = 0; v < 16; v++) ConfigureEnvelope(v);
    s_audioCh = sceAudioChReserve(VITAPOKE_AUDIO_NEXT_CHANNEL, SAS_GRAIN, VITAPOKE_AUDIO_FORMAT_STEREO);
    if (s_audioCh < 0) { VitaNativeMemLog("[AUDIO-SAS] sceAudioChReserve failed %08x", s_audioCh); return -1; }
    s_thread = sceKernelCreateThread("native_sas_out", SasThread, 0x11, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread < 0) { VitaNativeMemLog("[AUDIO-SAS] thread create failed %08x", s_thread); sceAudioChRelease(s_audioCh); return -1; }
    sceKernelStartThread(s_thread, 0, NULL);
    s_ready = 1;
    VitaNativeMemLog("[AUDIO-SAS] ready: sceSasCore 16 voices, grain %d @ %d Hz, audio ch %d", SAS_GRAIN, SAS_RATE, s_audioCh);
    return 0;
}

int VitaNativeSasReady(void) { return s_ready; }

/* Game thread, once per sound pump (after the sequencer and the silent channel advance). */
void VitaNativeSasSnapshot(u32 cycles)
{
    s_dsTime += cycles;
    if (!s_ready) return;
    u32 w = s_snapW;
    if (((w + 1) & SNAP_MASK) == (s_snapR & SNAP_MASK)) { st_dropSnaps++; return; }
    SasSnap *s = &s_snaps[w & SNAP_MASK];
    s->time = s_dsTime;
    for (int v = 0; v < 16; v++) {
        s->ch[v].cnt = s_SIM_sndcnt[v]; s->ch[v].sad = s_SIM_sndsad[v]; s->ch[v].tmr = s_SIM_sndtmr[v];
        s->ch[v].pnt = s_SIM_sndpnt[v]; s->ch[v].len = s_SIM_sndlen[v]; s->ch[v].gen = VitaNativeSasStartGen[v];
    }
    s_snapW = w + 1;
}

void VitaNativeSasStatsLine(char *buf, unsigned len)
{
    u32 fill = (s_snapW - s_snapR) & SNAP_MASK;
    snprintf(buf, len, "[AUDIO-SAS] ready=%d fill=%u keyons=%u hits=%u miss=%u conv_kb=%u cache_kb=%u evict=%u halved=%u pitchclamp=%u skipped=%u dropsnap=%u underfeed=%u thread_us_per_grain=%u coreerr=%u",
             s_ready, fill, st_keyOns, st_cacheHits, st_cacheMiss, st_convBytes >> 10, s_cacheBytes >> 10, st_evict, st_decimated, st_pitchClamp, st_skipped, st_dropSnaps, st_underfeed,
             st_grains ? st_threadUs / st_grains : 0, st_coreErr);
}

