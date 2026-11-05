#include <kernel/types.h>
#include "fb.h"
#include "page.h"

/*
 * fb.c - the linear-framebuffer module (M5.6).
 *
 * Boot-ABI truth: stage 2 wrote the VBE handoff at FB_INFO_AT before
 * long mode; we parse that block the same way mem.c parses E820 (fixed
 * address, fixed layout, docs/disk-layout.md normative). Everything the
 * module does is derived from that block - nothing about the mode,
 * stride or base is guessed or hardcoded beyond the handoff address.
 *
 * Absence is a first-class state (negative control): width==0 means
 * "the card offered no 32bpp LFB" and every accessor branches on it.
 */

/* boot ABI fb block (docs/disk-layout.md): width, height, pitch, bpp
   as four LE u32s, then the u64 physical base. Lives below 0x100000,
   read once into statics - later consumers never re-touch boot RAM. */
#define FB_INFO_AT   0x0A00ULL

/* Kernel VA the LFB maps at. 96 MiB past the demand base would collide
   with the kmalloc heap span; 0x50000000 sits in PML4 slot 0 (the page.c
   machinery only grows PML4[0] = pdpt[1] territory), well clear of the
   demand region (ends 0x48000000) and inside one PDPT page of fresh VA. */
#define FB_VA        0x50000000ULL

struct fb_mode {
    u32 width, height, pitch, bpp;
    u64 phys;
};

static struct fb_mode mode;      /* zeroed = absent */
static u64 mapped_va;            /* 0 until fb_map() */

__attribute__((noinline)) static u32 rd_le32(u64 at)
{
    /* handoff fields are 4-byte aligned by the ABI (docs table). The
       volatile-asm form is the standard fixed-address device/boot read:
       gcc's array-bounds checker cannot see an object behind the
       integer constant and would flag the plain cast as read of a
       zero-size array once this inlines. */
    u32 v;
    __asm__ volatile("movl %1, %0" : "=r"(v) : "m"(*(volatile u32 *)at));
    return v;
}

void fb_init(void)
{
    mode.width  = rd_le32(FB_INFO_AT + 0);
    mode.height = rd_le32(FB_INFO_AT + 4);
    mode.pitch  = rd_le32(FB_INFO_AT + 8);
    mode.bpp    = rd_le32(FB_INFO_AT + 12);
    mode.phys   = (u64)rd_le32(FB_INFO_AT + 16) |
                  ((u64)rd_le32(FB_INFO_AT + 20) << 32);

    /* zero the raw block's meaning once copied: width==0 is THE absent
       flag; also refuse incoherent handoffs (a half-written block, a
       0-pitch, a bpps we don't own the packing for). */
    if (mode.width && (!mode.height || !mode.pitch || mode.bpp != 32 ||
                       !mode.phys))
        mode.width = 0;
}

u32 fb_present(void) { return mode.width; }
u32 fb_width(void)  { return mode.width; }
u32 fb_height(void) { return mode.height; }
u32 fb_pitch(void)  { return mode.pitch; }
u64 fb_phys(void)   { return mode.phys; }

u64 fb_map(void)
{
    if (!mode.width) return 0;
    if (mapped_va)   return mapped_va;

    u32 npages = (mode.pitch * mode.height + 0xFFF) / 0x1000;
    if (page_map_at(FB_VA, mode.phys, npages) < 0)
        return 0;

    mapped_va = FB_VA;
    return mapped_va;
}

static volatile u32 *fb_px(u32 x, u32 y)
{
    if (!mode.width || !mapped_va || x >= mode.width || y >= mode.height)
        return 0;
    return (volatile u32 *)(mapped_va + (u64)y * mode.pitch + (u64)x * 4);
}

void fb_pixel_set(u32 x, u32 y, u32 rgb)
{
    volatile u32 *p = fb_px(x, y);
    if (p)
        *p = rgb;                 /* caller's word: 0x00RRGGBB */
}

u32 fb_pixel_get(u32 x, u32 y)
{
    volatile u32 *p = fb_px(x, y);
    return p ? *p : 0;
}

/* M6.1 rect substrate. Clip math notes: an exclusive end past u32
   arithmetic's wrap is caught by (w + x < w) - true exactly when x+w
   wrapped - and clamps to the screen edge, so a rect spanning the
   far border paints the rest of the screen instead of vanishing. */
u64 fb_rect_fill(u32 x, u32 y, u32 w, u32 h, u32 rgb)
{
    u32 x_end, y_end, xx, yy;
    u64 painted = 0;

    if (!mode.width || !mapped_va || w == 0 || h == 0) return 0;

    x_end = (w + x < w) ? mode.width : w + x;
    if (x_end > mode.width) x_end = mode.width;
    y_end = (h + y < h) ? mode.height : h + y;
    if (y_end > mode.height) y_end = mode.height;

    for (yy = y; yy < y_end; ++yy) {
        volatile u32 *p = (volatile u32 *)
            (mapped_va + (u64)yy * mode.pitch + (u64)x * 4);
        for (xx = x; xx < x_end; ++xx)
            *p++ = rgb;
        painted += x_end - x;
    }
    return painted;
}

void fb_region_copy(u32 sx, u32 sy, u32 w, u32 h, u32 dx, u32 dy)
{
    u32 s_xe, d_xe, s_ye, d_ye, span_x, span_y, dsp, n, c;
    int ydn, xdn;

    if (!mode.width || !mapped_va || w == 0 || h == 0) return;

    /* shared clip: the copy is only as wide as BOTH the source and
       the dest survive inside the screen; same vertically. */
    s_xe = (w + sx < w) ? mode.width : w + sx;
    if (s_xe > mode.width) s_xe = mode.width;
    d_xe = (w + dx < w) ? mode.width : w + dx;
    if (d_xe > mode.width) d_xe = mode.width;
    span_x = sx < s_xe ? s_xe - sx : 0;
    dsp    = dx < d_xe ? d_xe - dx : 0;
    if (dsp < span_x)  span_x = dsp;
    s_ye = (h + sy < h) ? mode.height : h + sy;
    if (s_ye > mode.height) s_ye = mode.height;
    d_ye = (h + dy < h) ? mode.height : h + dy;
    if (d_ye > mode.height) d_ye = mode.height;
    span_y = sy < s_ye ? s_ye - sy : 0;
    dsp    = dy < d_ye ? d_ye - dy : 0;
    if (dsp < span_y)  span_y = dsp;
    if (span_x == 0 || span_y == 0) return;

    /* Overlap-safe walk: writing screen row dy+n can only destroy a
       source row read LATER when dy - sy > 0 - so when the dest sits
       at or above the source, copy top rows first (else bottom
       first). Same argument column-wise. */
    ydn = (dy <= sy);
    xdn = (dx <= sx);
    for (n = 0; n < span_y; ++n) {
        u32 syr = ydn ? sy + n : sy + span_y - 1 - n;
        u32 dyr = ydn ? dy + n : dy + span_y - 1 - n;
        volatile u32 *sp = (volatile u32 *)
            (mapped_va + (u64)syr * mode.pitch + (u64)sx * 4);
        volatile u32 *dp = (volatile u32 *)
            (mapped_va + (u64)dyr * mode.pitch + (u64)dx * 4);
        if (xdn)
            for (c = 0; c < span_x; ++c) dp[c] = sp[c];
        else
            for (c = span_x; c-- > 0; )  dp[c] = sp[c];
    }
}
