#pragma once

#include <kernel/types.h>

/*
 * Linear framebuffer (M5.5->M5.6). Stage 2 set a VBE LFB mode while the
 * BIOS was still reachable and handed the mode's frame down in the boot
 * ABI fb block (0x0A00; docs/disk-layout.md normative): u32 width,
 * u32 height, u32 pitch (bytes/scanline), u32 bpp, u64 physical base.
 * width == 0 means the card offered nothing - an honest absence; the
 * kernel keeps the VGA text console and every consumer must branch on
 * fb_present() before touching pixels.
 *
 * The module itself never paints on its own: init parses, map walks
 * the shared page machinery (page_map_at, one CR3 flush) and ONLY the
 * caller drives pixels through fb_pixel_set/get. Port rule: no prints
 * on absence - the caller turns results into evidence.
 */

void fb_init(void);
u64 fb_map(void);

/* Handoff truth (0 values = absent). */
u32 fb_present(void);      /* width, or 0 */
u32 fb_width(void);
u32 fb_height(void);
u32 fb_pitch(void);        /* bytes per scanline */
u64 fb_phys(void);         /* physical base of the LFB */

/* Pixel poke at (x,y) with 0x00RRGGBB - transparent to layout: the
   module owns the b/g/r byte packing for the little-endian xRGB frame.
   Out-of-bounds is a silent drop (the caller's bug calls itself out
   via the readback gate). The getter returns 0 out of bounds. */
void fb_pixel_set(u32 x, u32 y, u32 rgb);
u32  fb_pixel_get(u32 x, u32 y);

/* M6.1: the rect substrate the compositor (and the console scroller)
   builds on. Both clip hard - a partially off-screen op stays
   partially on, an overflowed corner is not a wraparound write - and
   the fill returns its painted-pixel count so the boot gate can
   verify the exact area, not an impression. fb_region_copy moves
   w*h pixels from (sx,sy) to (dx,dy): vertical AND horizontal
   self-overlap are safe (walk direction adapts), the copy readback
   is the scroll/bounce primitive. Zero span or fully off-screen is
   a silent no-op, like every other out-of-bounds here. */
u64  fb_rect_fill(u32 x, u32 y, u32 w, u32 h, u32 rgb);
void fb_region_copy(u32 sx, u32 sy, u32 w, u32 h, u32 dx, u32 dy);
